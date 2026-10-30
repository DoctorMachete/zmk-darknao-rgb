/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: mirror mode, zmk,behavior-mirror (PATCH_MIRROR_MODE.md).
 *
 * While an instance is on, a key press on one of its layers acts as its mirror key. There is
 * no layer behind it: a listener on zmk_position_state_changed rewrites the event's position
 * in place, so everything after it (hold-tap, combos, keymap) sees the mirrored key.
 *
 *  - Decision per key press: an instance is on, the highest active layer is in one of its
 *    child groups, and the key isn't in that group's excluded-positions.
 *  - Press and release stay paired: the release goes to whatever the press was mapped to,
 *    even if the mode changed in between.
 *  - A press whose resulting key is already held (two keys mapping to one, or the mirror
 *    key itself held) is swallowed, and so is its release.
 *  - Only one instance on at a time; turning another on switches (old exit, new enter).
 *
 * Link order (app/CMakeLists.txt): with CONFIG_ZMK_MIRROR_MODE_COMBOS this file is listed
 * right after layer_idle_lock.c, ahead of hold-tap and combo, so combos are mirrored. combo.c
 * re-raises some captured events from the start of the chain; it calls
 * zmk_mirror_mode_pass_next() first so they aren't mirrored twice. Without that option the
 * file sits after combo.c and combos keep their physical keys.
 *
 * Threading: position events, behavior callbacks and the binding queue all run on the system
 * work queue, so the state needs no lock.
 */

#define DT_DRV_COMPAT zmk_behavior_mirror

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/mirror.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/mirror_mode_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/mirror_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* phys_out[p]: 0 = key p not pressed (as far as we know), 1..LEN = pressed and sent as
 * position (value - 1), MIR_SWALLOWED = pressed and swallowed. */
#define MIR_SWALLOWED 0xFF
BUILD_ASSERT(ZMK_KEYMAP_LEN < MIR_SWALLOWED, "zmk,behavior-mirror: keymap too large");

#define MIR_IS_GO60 (IS_ENABLED(CONFIG_BOARD_GO60_LH) || IS_ENABLED(CONFIG_BOARD_GO60_RH))

struct mirror_group {
    uint32_t layers; /* bit per layer id */
    const uint8_t *excluded;
    uint8_t excluded_len;
    const uint32_t *combo_pairs; /* `combo-pairs`, NULL if none */
    uint16_t combo_pairs_len;
};

struct mirror_cfg {
    uint8_t index; /* devicetree instance number */
    uint8_t overlay_id;
    const uint8_t *map; /* ZMK_KEYMAP_LEN entries; NULL = built-in GO60 map */
    const struct mirror_group *groups;
    uint8_t groups_len;
    uint32_t layers; /* union of the groups' layers */
    /* Non-const: behavior_keymap_binding_pressed() takes a non-const binding. */
    struct zmk_behavior_binding *enter_bindings;
    struct zmk_behavior_binding *exit_bindings;
    uint8_t enter_bindings_len;
    uint8_t exit_bindings_len;
    bool not_press_release_outputs;
    uint16_t tap_ms;
    uint16_t wait_ms;
};

enum mirror_mode {
    MIR_MODE_LATCHED,
    MIR_MODE_MOMENTARY,
    MIR_MODE_ONESHOT,
};

/* ---- devicetree extraction ------------------------------------------------------------ */

/* enter / exit: prop-parameterised twins of ZMK_KEYMAP_EXTRACT_BINDING, as in caps word and
 * the layer idle-lock. */
#define MIR_EXTRACT_BINDING(idx, n, prop)                                                          \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, prop, idx)),                      \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, prop, idx, param1), (0),              \
                              (DT_INST_PHA_BY_IDX(n, prop, idx, param1))),                         \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, prop, idx, param2), (0),              \
                              (DT_INST_PHA_BY_IDX(n, prop, idx, param2))),                         \
    }

#define MIR_BINDINGS_ARRAY(n, prop, name)                                                          \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop),                                                    \
                (static struct zmk_behavior_binding name[DT_INST_PROP_LEN(n, prop)] = {            \
                     LISTIFY(DT_INST_PROP_LEN(n, prop), MIR_EXTRACT_BINDING, (, ), n, prop)};),    \
                ())

#define MIR_PROP_LEN(n, prop)                                                                      \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop), (DT_INST_PROP_LEN(n, prop)), (0))

#define MIR_BINDINGS_PTR(n, prop, name)                                                            \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop), (name), (NULL))

/* Build-time checks, one per element. */
#define MIR_CHECK_MAP_ENTRY(node_id, prop, idx)                                                    \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LEN,                              \
                 "zmk,behavior-mirror: a `mirror-positions` entry is not a key position");
#define MIR_CHECK_LAYER(node_id, prop, idx)                                                        \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LAYERS_LEN,                       \
                 "zmk,behavior-mirror: a group's `layers` entry is not a layer of this keymap");
#define MIR_CHECK_EXCLUDED(node_id, prop, idx)                                                     \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LEN,                              \
                 "zmk,behavior-mirror: an `excluded-positions` entry is not a key position");

/* Per child group. */
#define MIR_LAYER_BIT(node_id, prop, idx) | BIT(DT_PROP_BY_IDX(node_id, prop, idx))
#define MIR_GROUP_LAYERS(child) ((uint32_t)(0 DT_FOREACH_PROP_ELEM(child, layers, MIR_LAYER_BIT)))

/* combo-pairs: entries are key positions or separators; one MIRROR_WITH per pair, one
 * MIRROR_NEXT between pairs; no empty combo (separator first, last or doubled). The order of
 * the separators is checked when the list is walked (a mistake disables it, logged). */
#define MIR_IS_SEP(v) ((v) == MIRROR_WITH || (v) == MIRROR_NEXT)
#define MIR_PAIR_AT(child, i) DT_PROP_BY_IDX(child, combo_pairs, i)
#define MIR_CHECK_PAIR_ELEM(node_id, prop, idx)                                                    \
    BUILD_ASSERT(DT_PROP_BY_IDX(node_id, prop, idx) < ZMK_KEYMAP_LEN ||                            \
                     MIR_IS_SEP(DT_PROP_BY_IDX(node_id, prop, idx)),                               \
                 "zmk,behavior-mirror: `combo-pairs` entries are key positions, MIRROR_WITH "      \
                 "or MIRROR_NEXT");
#define MIR_COUNT_WITH(node_id, prop, idx) +(DT_PROP_BY_IDX(node_id, prop, idx) == MIRROR_WITH)
#define MIR_COUNT_NEXT(node_id, prop, idx) +(DT_PROP_BY_IDX(node_id, prop, idx) == MIRROR_NEXT)
#define MIR_CHECK_PAIR_ADJ(i, child)                                                               \
    BUILD_ASSERT(!(MIR_IS_SEP(MIR_PAIR_AT(child, i)) &&                                            \
                   MIR_IS_SEP(MIR_PAIR_AT(child, UTIL_INC(i)))),                                   \
                 "zmk,behavior-mirror: `combo-pairs` has an empty combo (two separators in a row)");
#define MIR_PAIRS_CHECKS(child)                                                                    \
    DT_FOREACH_PROP_ELEM(child, combo_pairs, MIR_CHECK_PAIR_ELEM)                                  \
    BUILD_ASSERT((0 DT_FOREACH_PROP_ELEM(child, combo_pairs, MIR_COUNT_WITH)) ==                   \
                     (0 DT_FOREACH_PROP_ELEM(child, combo_pairs, MIR_COUNT_NEXT)) + 1,             \
                 "zmk,behavior-mirror: `combo-pairs` needs one MIRROR_WITH per pair and "          \
                 "MIRROR_NEXT between pairs");                                                     \
    BUILD_ASSERT(!MIR_IS_SEP(MIR_PAIR_AT(child, 0)) &&                                             \
                     !MIR_IS_SEP(MIR_PAIR_AT(child, UTIL_DEC(DT_PROP_LEN(child, combo_pairs)))),   \
                 "zmk,behavior-mirror: `combo-pairs` can't start or end with a separator");        \
    LISTIFY(UTIL_DEC(DT_PROP_LEN(child, combo_pairs)), MIR_CHECK_PAIR_ADJ, (), child)

#define MIR_GROUP_CHECKS(child)                                                                    \
    DT_FOREACH_PROP_ELEM(child, layers, MIR_CHECK_LAYER)                                           \
    COND_CODE_1(DT_NODE_HAS_PROP(child, excluded_positions),                                       \
                (DT_FOREACH_PROP_ELEM(child, excluded_positions, MIR_CHECK_EXCLUDED)), ())         \
    COND_CODE_1(DT_NODE_HAS_PROP(child, combo_pairs), (MIR_PAIRS_CHECKS(child)), ())

#define MIR_GROUP_EXCLUDED_ARRAY(child)                                                            \
    COND_CODE_1(DT_NODE_HAS_PROP(child, excluded_positions),                                       \
                (static const uint8_t mir_excl_##child[] = DT_PROP(child, excluded_positions);),   \
                ())                                                                                \
    COND_CODE_1(DT_NODE_HAS_PROP(child, combo_pairs),                                              \
                (static const uint32_t mir_pairs_##child[] = DT_PROP(child, combo_pairs);), ())

#define MIR_GROUP_ENTRY(child)                                                                     \
    {                                                                                              \
        .layers = MIR_GROUP_LAYERS(child),                                                         \
        .excluded = COND_CODE_1(DT_NODE_HAS_PROP(child, excluded_positions), (mir_excl_##child),   \
                                (NULL)),                                                           \
        .excluded_len = DT_PROP_LEN_OR(child, excluded_positions, 0),                              \
        .combo_pairs =                                                                             \
            COND_CODE_1(DT_NODE_HAS_PROP(child, combo_pairs), (mir_pairs_##child), (NULL)),        \
        .combo_pairs_len = DT_PROP_LEN_OR(child, combo_pairs, 0),                                  \
    },

#define MIR_GROUP_COUNT(child) +1
#define MIR_GROUP_SUM(child) + (uint64_t)MIR_GROUP_LAYERS(child)
#define MIR_GROUP_OR(child) | MIR_GROUP_LAYERS(child)

#define MIR_INST(n)                                                                                \
    BUILD_ASSERT(DT_INST_NODE_HAS_PROP(n, mirror_positions) || MIR_IS_GO60,                        \
                 "zmk,behavior-mirror: `mirror-positions` is required on this board "              \
                 "(the built-in default map is for the GO60 only)");                               \
    BUILD_ASSERT(DT_INST_NODE_HAS_PROP(n, mirror_positions) || ZMK_KEYMAP_LEN == 60,               \
                 "zmk,behavior-mirror: the built-in GO60 map needs a 60-key keymap");              \
    BUILD_ASSERT(DT_INST_PROP_LEN_OR(n, mirror_positions, ZMK_KEYMAP_LEN) == ZMK_KEYMAP_LEN,       \
                 "zmk,behavior-mirror: `mirror-positions` must have one entry per key position");  \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, mirror_positions),                                        \
                (DT_INST_FOREACH_PROP_ELEM(n, mirror_positions, MIR_CHECK_MAP_ENTRY)), ())         \
    BUILD_ASSERT((0 DT_INST_FOREACH_CHILD(n, MIR_GROUP_COUNT)) > 0,                                \
                 "zmk,behavior-mirror: needs at least one child group with `layers`");             \
    BUILD_ASSERT((uint64_t)(0 DT_INST_FOREACH_CHILD(n, MIR_GROUP_SUM)) ==                          \
                     (uint64_t)(0 DT_INST_FOREACH_CHILD(n, MIR_GROUP_OR)),                         \
                 "zmk,behavior-mirror: a layer is listed in two groups of the same instance");     \
    BUILD_ASSERT(DT_INST_PROP(n, overlay_id) >= 0 && DT_INST_PROP(n, overlay_id) <= 255,           \
                 "zmk,behavior-mirror: `overlay-id` must be 0..255");                              \
    DT_INST_FOREACH_CHILD(n, MIR_GROUP_CHECKS)                                                     \
    DT_INST_FOREACH_CHILD(n, MIR_GROUP_EXCLUDED_ARRAY)                                             \
    static const struct mirror_group mir_groups_##n[] = {                                          \
        DT_INST_FOREACH_CHILD(n, MIR_GROUP_ENTRY)};                                                \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, mirror_positions),                                        \
                (static const uint8_t mir_map_##n[] = DT_INST_PROP(n, mirror_positions);), ())     \
    MIR_BINDINGS_ARRAY(n, enter, mir_enter_##n)                                                    \
    MIR_BINDINGS_ARRAY(n, exit, mir_exit_##n)                                                      \
    static const struct mirror_cfg mir_cfg_##n = {                                                 \
        .index = n,                                                                                \
        .overlay_id = DT_INST_PROP(n, overlay_id),                                                 \
        .map = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, mirror_positions), (mir_map_##n), (NULL)),     \
        .groups = mir_groups_##n,                                                                  \
        .groups_len = ARRAY_SIZE(mir_groups_##n),                                                  \
        .layers = (uint32_t)(0 DT_INST_FOREACH_CHILD(n, MIR_GROUP_OR)),                            \
        .enter_bindings = MIR_BINDINGS_PTR(n, enter, mir_enter_##n),                               \
        .enter_bindings_len = MIR_PROP_LEN(n, enter),                                              \
        .exit_bindings = MIR_BINDINGS_PTR(n, exit, mir_exit_##n),                                  \
        .exit_bindings_len = MIR_PROP_LEN(n, exit),                                                \
        .not_press_release_outputs = DT_INST_PROP(n, not_press_release_outputs),                   \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
    };

DT_INST_FOREACH_STATUS_OKAY(MIR_INST)

#define MIR_CFG_PTR(n) &mir_cfg_##n,
static const struct mirror_cfg *const mir_cfgs[] = {DT_INST_FOREACH_STATUS_OKAY(MIR_CFG_PTR)};

/* ---- state ----------------------------------------------------------------------------- */

static const struct mirror_cfg *active; /* NULL = mirror mode off */
static enum mirror_mode active_mode;
static uint8_t oneshot_key; /* physical key (+1) that is using up the one-shot; 0 = none */

static uint8_t phys_out[ZMK_KEYMAP_LEN]; /* see MIR_SWALLOWED */
static bool out_held[ZMK_KEYMAP_LEN];    /* this position is held as some key's output */
/* The last press that produced this position was mirrored. Kept after the release, because a
 * combo can fire on a key's release (a longer combo was still pending). */
static bool out_last_mirrored[ZMK_KEYMAP_LEN];

static struct {
    bool armed;
    uint32_t position;
    bool state;
    int64_t timestamp;
} pass_next;

/* ---- helpers --------------------------------------------------------------------------- */

/* Built-in plain GO60 mirror (MIRROR_MAP_GO60 in dt-bindings/zmk/mirror.h). Only reached on
 * the GO60: elsewhere the build requires `mirror-positions`. */
static uint8_t mirror_default_map(uint8_t p) {
    if (p < 48) {
        return (p / 12) * 12 + (11 - p % 12);
    }
    if (p < 54) {
        return 48 + 53 - p;
    }
    return 54 + 59 - p;
}

static uint8_t mirror_lookup(const struct mirror_cfg *cfg, uint8_t p) {
    return cfg->map != NULL ? cfg->map[p] : mirror_default_map(p);
}

static const struct mirror_group *mirror_group_for_layer(const struct mirror_cfg *cfg,
                                                         zmk_keymap_layer_id_t layer) {
    for (int i = 0; i < cfg->groups_len; i++) {
        if (cfg->groups[i].layers & BIT(layer)) {
            return &cfg->groups[i];
        }
    }
    return NULL;
}

static bool mirror_excluded(const struct mirror_group *g, uint8_t p) {
    for (int i = 0; i < g->excluded_len; i++) {
        if (g->excluded[i] == p) {
            return true;
        }
    }
    return false;
}

/* Fire one enter / exit chain. behavior_keymap_binding_pressed() rather than
 * zmk_behavior_invoke_binding(): the pixel behaviors relay themselves (constraint #9). */
static void mirror_fire(const struct mirror_cfg *cfg, struct zmk_behavior_binding *bindings,
                        size_t len) {
    if (len == 0 || bindings == NULL) {
        return;
    }

    struct zmk_behavior_binding_event event = {
        .position = INT32_MAX,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    for (size_t i = 0; i < len; i++) {
        if (cfg->not_press_release_outputs) {
            behavior_keymap_binding_pressed(&bindings[i], event);
        } else {
            zmk_behavior_queue_add(&event, bindings[i], true, cfg->tap_ms);
            zmk_behavior_queue_add(&event, bindings[i], false, cfg->wait_ms);
        }
    }
}

static void mirror_raise_changed(const struct mirror_cfg *cfg, bool on) {
    raise_zmk_mirror_mode_changed((struct zmk_mirror_mode_changed){
        .instance = cfg->index,
        .overlay_id = cfg->overlay_id,
        .active = on,
        .oneshot = on && active_mode == MIR_MODE_ONESHOT,
        .timestamp = k_uptime_get(),
    });
}

/* The only place the mode changes. cfg == NULL turns mirror mode off. */
static void mirror_set_state(const struct mirror_cfg *cfg, enum mirror_mode mode) {
    if (cfg != NULL && cfg == active) {
        if (mode != active_mode) {
            LOG_DBG("instance %d stays on, mode %d to %d", cfg->index, active_mode, mode);
            active_mode = mode;
            oneshot_key = 0;
            mirror_raise_changed(cfg, true);
        }
        return;
    }

    if (active != NULL) {
        const struct mirror_cfg *old = active;
        active = NULL;
        oneshot_key = 0;
        LOG_DBG("instance %d off", old->index);
        mirror_fire(old, old->exit_bindings, old->exit_bindings_len);
        mirror_raise_changed(old, false);
    }

    if (cfg != NULL) {
        active = cfg;
        active_mode = mode;
        oneshot_key = 0;
        LOG_DBG("instance %d on, mode %d", cfg->index, mode);
        mirror_fire(cfg, cfg->enter_bindings, cfg->enter_bindings_len);
        mirror_raise_changed(cfg, true);
    }
}

/* ---- public API ------------------------------------------------------------------------ */

uint8_t zmk_mirror_mode_active_overlay_id(void) { return active != NULL ? active->overlay_id : 0; }

uint32_t zmk_mirror_mode_active_layers(void) { return active != NULL ? active->layers : 0; }

bool zmk_mirror_mode_oneshot_armed(void) {
    return active != NULL && active_mode == MIR_MODE_ONESHOT;
}

#if IS_ENABLED(CONFIG_ZMK_MIRROR_MODE_COMBOS)
void zmk_mirror_mode_pass_next(const struct zmk_position_state_changed *ev) {
    pass_next.armed = true;
    pass_next.position = ev->position;
    pass_next.state = ev->state;
    pass_next.timestamp = ev->timestamp;
}

/* Same set of positions (combo key-positions have no duplicates). */
static bool mirror_same_combo(const uint32_t *member, int member_len, const int32_t *positions,
                              int len) {
    if (member_len != len) {
        return false;
    }
    for (int i = 0; i < member_len; i++) {
        bool found = false;
        for (int j = 0; j < len && !found; j++) {
            found = (positions[j] >= 0 && (uint32_t)positions[j] == member[i]);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

int zmk_mirror_mode_combo_partner(const int32_t *positions, int len, int32_t *partner, int cap) {
    if (active == NULL || len <= 0) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        if (positions[i] < 0 || positions[i] >= ZMK_KEYMAP_LEN ||
            !out_last_mirrored[positions[i]]) {
            return 0; /* this combo wasn't reached through mirrored keys */
        }
    }

    zmk_keymap_layer_id_t top = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
    const struct mirror_group *g = mirror_group_for_layer(active, top);
    if (g == NULL || g->combo_pairs == NULL) {
        return 0;
    }

    /* Walk <A... WITH B... (NEXT C... WITH D...)*>. */
    const uint32_t *v = g->combo_pairs;
    int n = g->combo_pairs_len;
    int i = 0;
    while (i < n) {
        int a0 = i;
        while (i < n && !MIR_IS_SEP(v[i])) {
            i++;
        }
        int a_len = i - a0;
        if (a_len == 0 || i >= n || v[i] != MIRROR_WITH) {
            break;
        }
        i++;
        int b0 = i;
        while (i < n && !MIR_IS_SEP(v[i])) {
            i++;
        }
        int b_len = i - b0;
        if (b_len == 0 || (i < n && v[i] != MIRROR_NEXT)) {
            break;
        }
        i++;

        const uint32_t *other = NULL;
        int other_len = 0;
        if (mirror_same_combo(&v[a0], a_len, positions, len)) {
            other = &v[b0];
            other_len = b_len;
        } else if (mirror_same_combo(&v[b0], b_len, positions, len)) {
            other = &v[a0];
            other_len = a_len;
        }
        if (other != NULL) {
            if (other_len > cap) {
                return 0;
            }
            for (int k = 0; k < other_len; k++) {
                partner[k] = (int32_t)other[k];
            }
            return other_len;
        }
    }

    if (i < n) {
        LOG_ERR("instance %d: `combo-pairs` separators out of order at entry %d, ignored",
                active->index, i);
    }
    return 0;
}
#endif

/* ---- the position listener ------------------------------------------------------------- */

static int mirror_on_press(struct zmk_position_state_changed *ev, uint8_t p) {
    uint8_t q = p;

    if (active != NULL) {
        zmk_keymap_layer_id_t top =
            zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
        const struct mirror_group *g = mirror_group_for_layer(active, top);
        if (g != NULL && !mirror_excluded(g, p)) {
            q = mirror_lookup(active, p);
        }
    }

    if (out_held[q]) {
        /* Rule: that key is already held through another physical key. Ignore this press
         * and, below, its release, so nothing is released early. */
        LOG_DBG("position %d (as %d) swallowed: %d already held", p, q, q);
        phys_out[p] = MIR_SWALLOWED;
        return ZMK_EV_EVENT_HANDLED;
    }

    out_held[q] = true;
    phys_out[p] = q + 1;
    out_last_mirrored[q] = (q != p);

    if (q != p) {
        LOG_DBG("position %d acts as %d", p, q);
        ev->position = q;
        if (active_mode == MIR_MODE_ONESHOT && oneshot_key == 0) {
            oneshot_key = p + 1;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int mirror_on_release(struct zmk_position_state_changed *ev, uint8_t p) {
    uint8_t s = phys_out[p];

    if (s == 0) {
        return ZMK_EV_EVENT_BUBBLE; /* press not seen (held since before boot) */
    }
    phys_out[p] = 0;

    if (s == MIR_SWALLOWED) {
        LOG_DBG("position %d release swallowed", p);
        return ZMK_EV_EVENT_HANDLED;
    }

    uint8_t q = s - 1;
    out_held[q] = false;
    if (q != p) {
        LOG_DBG("position %d release acts as %d", p, q);
        ev->position = q;
    }

    if (oneshot_key == p + 1) {
        oneshot_key = 0;
        if (active != NULL && active_mode == MIR_MODE_ONESHOT) {
            LOG_DBG("one-shot used");
            mirror_set_state(NULL, MIR_MODE_LATCHED);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int mirror_position_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (pass_next.armed) {
        pass_next.armed = false;
        if (pass_next.position == ev->position && pass_next.state == ev->state &&
            pass_next.timestamp == ev->timestamp) {
            return ZMK_EV_EVENT_BUBBLE; /* a combo re-raise of an event already handled */
        }
        LOG_WRN("expected a combo re-raise of %d, got %d", pass_next.position, ev->position);
    }

    if (ev->position >= ZMK_KEYMAP_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    return ev->state ? mirror_on_press(ev, (uint8_t)ev->position)
                     : mirror_on_release(ev, (uint8_t)ev->position);
}

ZMK_LISTENER(mirror_mode, mirror_position_listener);
ZMK_SUBSCRIPTION(mirror_mode, zmk_position_state_changed);

/* ---- the behavior ---------------------------------------------------------------------- */

static int mirror_binding_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct mirror_cfg *cfg = dev->config;
    bool on = (active == cfg);

    switch (binding->param1) {
    case MIRROR_TOG:
        mirror_set_state(on ? NULL : cfg, MIR_MODE_LATCHED);
        break;
    case MIRROR_ON:
        mirror_set_state(cfg, MIR_MODE_LATCHED);
        break;
    case MIRROR_OFF:
        if (on) {
            mirror_set_state(NULL, MIR_MODE_LATCHED);
        }
        break;
    case MIRROR_MO:
        if (!on) {
            mirror_set_state(cfg, MIR_MODE_MOMENTARY);
        }
        break;
    case MIRROR_ONESHOT:
        if (!on) {
            mirror_set_state(cfg, MIR_MODE_ONESHOT);
        }
        break;
    default:
        LOG_WRN("unknown mirror parameter %d", binding->param1);
        return -ENOTSUP;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mirror_binding_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct mirror_cfg *cfg = dev->config;

    if (binding->param1 == MIRROR_MO && active == cfg && active_mode == MIR_MODE_MOMENTARY) {
        mirror_set_state(NULL, MIR_MODE_LATCHED);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mirror_driver_api = {
    .binding_pressed = mirror_binding_pressed,
    .binding_released = mirror_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int mirror_init(const struct device *dev) {
    const struct mirror_cfg *cfg = dev->config;

    /* overlay-id should be unique, or the host can't tell instances apart. */
    for (int i = 0; i < ARRAY_SIZE(mir_cfgs); i++) {
        if (mir_cfgs[i] != cfg && cfg->overlay_id != 0 &&
            mir_cfgs[i]->overlay_id == cfg->overlay_id) {
            LOG_WRN("instances %d and %d share overlay-id %d", cfg->index, mir_cfgs[i]->index,
                    cfg->overlay_id);
        }
    }
    return 0;
}

#define MIR_DEVICE(n)                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, mirror_init, NULL, NULL, &mir_cfg_##n, POST_KERNEL,                 \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mirror_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MIR_DEVICE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
