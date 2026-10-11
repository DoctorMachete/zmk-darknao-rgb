/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: idle-lock for momentary layers (PATCH_LAYER_IDLE_LOCK.md).
 *
 * A layer turned on by &mo (directly, or as the hold of a hold-tap) is locked on when the
 * &mo key has been held for `idle-ms` with no other key held or pressed. The `enter` chain
 * then fires (an indicator). Releasing the &mo key no longer turns the layer off; &tog or
 * &to does, and that fires the `exit` chain.
 *
 * Threading: position events, layer events, behavior callbacks and the idle timer all run
 * on the system work queue, so the state needs no lock. The one exception is the pointing
 * callback (input thread), which only restarts timers; see lil_input_cb().
 */

#define DT_DRV_COMPAT zmk_layer_idle_lock

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/layer_idle_lock.h>
#include <zmk/matrix.h>

#if IS_ENABLED(CONFIG_INPUT)
#include <zephyr/input/input.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define LIL_HELD_WORDS DIV_ROUND_UP(ZMK_KEYMAP_LEN, 32)

enum lil_phase {
    LIL_IDLE,   /* layer not held by &mo, or held and released again */
    LIL_ARMED,  /* &mo is holding the layer; the idle timer runs while nothing else is held */
    LIL_LOCKED, /* timer expired: the layer is locked and `enter` has fired */
};

struct lil_cfg {
    zmk_keymap_layer_id_t layer;
    int idle_ms;
    /* Non-const: behavior_keymap_binding_pressed() takes a non-const binding. */
    struct zmk_behavior_binding *enter_bindings;
    struct zmk_behavior_binding *exit_bindings;
    uint8_t enter_bindings_len;
    uint8_t exit_bindings_len;
    bool not_press_release_outputs;
};

struct lil_state {
    const struct lil_cfg *cfg;
    enum lil_phase phase;
    /* Positions already held when the layer was armed (at least the &mo key). Holding
     * them is not activity; a baseline key leaves the set once released, so pressing it
     * again counts like any other key. */
    uint32_t baseline[LIL_HELD_WORDS];
    struct k_work_delayable work;
};

/* Every key position currently held, both halves (the central sees all of them). */
static uint32_t held[LIL_HELD_WORDS];

/* ---- devicetree extraction ------------------------------------------------------------
 * ZMK_KEYMAP_EXTRACT_BINDING is hardcoded to the `bindings` property, so these are
 * prop-parameterised twins for the child nodes' `enter` / `exit`. Same construct as
 * zmk-listeners/src/listener_bindings.h and the caps-word patch. */

#define LIL_EXTRACT_BINDING(idx, node, prop)                                                       \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_PHANDLE_BY_IDX(node, prop, idx)),                        \
        .param1 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, param1), (0),                \
                              (DT_PHA_BY_IDX(node, prop, idx, param1))),                           \
        .param2 = COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, param2), (0),                \
                              (DT_PHA_BY_IDX(node, prop, idx, param2))),                           \
    }

#define LIL_BINDINGS_ARRAY(node, prop, name)                                                       \
    COND_CODE_1(DT_NODE_HAS_PROP(node, prop),                                                      \
                (static struct zmk_behavior_binding name[DT_PROP_LEN(node, prop)] = {LISTIFY(      \
                     DT_PROP_LEN(node, prop), LIL_EXTRACT_BINDING, (, ), node, prop)};),           \
                ())

#define LIL_PROP_LEN(node, prop)                                                                   \
    COND_CODE_1(DT_NODE_HAS_PROP(node, prop), (DT_PROP_LEN(node, prop)), (0))

#define LIL_BINDINGS_PTR(node, prop, name) COND_CODE_1(DT_NODE_HAS_PROP(node, prop), (name), (NULL))

#define LIL_CFG_INST(node)                                                                         \
    LIL_BINDINGS_ARRAY(node, enter, lil_enter_##node)                                              \
    LIL_BINDINGS_ARRAY(node, exit, lil_exit_##node)                                                \
    BUILD_ASSERT(DT_PROP(node, layer) < ZMK_KEYMAP_LAYERS_LEN,                                     \
                 "zmk,layer-idle-lock: `layer` is not a layer of this keymap");                    \
    BUILD_ASSERT(LIL_PROP_LEN(node, enter) <= UINT8_MAX && LIL_PROP_LEN(node, exit) <= UINT8_MAX,  \
                 "zmk,layer-idle-lock: too many enter/exit bindings");                             \
    BUILD_ASSERT(DT_PROP(node, idle_ms) >= 0, "zmk,layer-idle-lock: `idle-ms` must be >= 0");      \
    static const struct lil_cfg lil_cfg_##node = {                                                 \
        .layer = DT_PROP(node, layer),                                                             \
        .idle_ms = DT_PROP(node, idle_ms),                                                         \
        .enter_bindings = LIL_BINDINGS_PTR(node, enter, lil_enter_##node),                         \
        .enter_bindings_len = LIL_PROP_LEN(node, enter),                                           \
        .exit_bindings = LIL_BINDINGS_PTR(node, exit, lil_exit_##node),                            \
        .exit_bindings_len = LIL_PROP_LEN(node, exit),                                             \
        .not_press_release_outputs = DT_PROP(node, not_press_release_outputs),                     \
    };

DT_INST_FOREACH_CHILD(0, LIL_CFG_INST)

#define LIL_STATE_INIT(node) {.cfg = &lil_cfg_##node, .phase = LIL_IDLE},

static struct lil_state states[] = {DT_INST_FOREACH_CHILD(0, LIL_STATE_INIT)};

#define LIL_COUNT ARRAY_SIZE(states)
#define LIL_TAP_MS DT_INST_PROP(0, tap_ms)
#define LIL_WAIT_MS DT_INST_PROP(0, wait_ms)

/* ---- helpers --------------------------------------------------------------------------- */

static struct lil_state *lil_find(zmk_keymap_layer_id_t layer) {
    for (size_t i = 0; i < LIL_COUNT; i++) {
        if (states[i].cfg->layer == layer) {
            return &states[i];
        }
    }
    return NULL;
}

/* Is any key held that was not already held when the layer was armed? */
static bool lil_busy(const struct lil_state *st) {
    for (size_t w = 0; w < LIL_HELD_WORDS; w++) {
        if (held[w] & ~st->baseline[w]) {
            return true;
        }
    }
    return false;
}

/* Fire one `enter` / `exit` chain, in order.
 *
 * NOTE: behavior_keymap_binding_pressed(), NOT zmk_behavior_invoke_binding(), on the
 * not-press-release path: the fork's pixel behaviors are BEHAVIOR_LOCALITY_GLOBAL and relay
 * themselves to the right half, so the locality-aware call would add a redundant split
 * invoke (constraint #9). Same code as the caps-word patch and zmk-listeners. */
static void lil_fire(const struct lil_cfg *cfg, struct zmk_behavior_binding *bindings, size_t len) {
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
            zmk_behavior_queue_add(&event, bindings[i], true, LIL_TAP_MS);
            zmk_behavior_queue_add(&event, bindings[i], false, LIL_WAIT_MS);
        }
    }
}

static void lil_disarm(struct lil_state *st) {
    k_work_cancel_delayable(&st->work);
    st->phase = LIL_IDLE;
}

/* ---- idle timer ------------------------------------------------------------------------ */

static void lil_timeout(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct lil_state *st = CONTAINER_OF(dwork, struct lil_state, work);

    /* Re-check: the timer may have been queued just before a key event changed things. */
    if (st->phase != LIL_ARMED || lil_busy(st)) {
        return;
    }

    int ret = zmk_keymap_layer_lock(st->cfg->layer);
    if (ret < 0) {
        LOG_WRN("layer %d: idle lock not taken (%d)", st->cfg->layer, ret);
        st->phase = LIL_IDLE;
        return;
    }

    st->phase = LIL_LOCKED;
    LOG_DBG("layer %d locked after %d ms idle", st->cfg->layer, st->cfg->idle_ms);
    lil_fire(st->cfg, st->cfg->enter_bindings, st->cfg->enter_bindings_len);
}

/* ---- &mo hooks ------------------------------------------------------------------------- */

void zmk_layer_idle_lock_momentary_pressed(zmk_keymap_layer_id_t layer) {
    struct lil_state *st = lil_find(layer);
    if (st == NULL) {
        return;
    }

    /* Already locked: stays locked. As upstream, &mo cannot release a lock. */
    if (st->phase == LIL_LOCKED) {
        return;
    }

    if (!zmk_keymap_layer_active(layer)) {
        return;
    }

    memcpy(st->baseline, held, sizeof(held));
    st->phase = LIL_ARMED;
    k_work_reschedule(&st->work, K_MSEC(st->cfg->idle_ms));
    LOG_DBG("layer %d armed, %d ms", layer, st->cfg->idle_ms);
}

void zmk_layer_idle_lock_momentary_released(zmk_keymap_layer_id_t layer) {
    struct lil_state *st = lil_find(layer);
    if (st == NULL) {
        return;
    }

    if (st->phase == LIL_ARMED) {
        lil_disarm(st);
        LOG_DBG("layer %d released before the idle lock", layer);
    }
    /* LIL_LOCKED: nothing to do. The keymap ignores the deactivation &mo makes next. */
}

/* ---- event listeners ------------------------------------------------------------------- */

static void lil_on_position(const struct zmk_position_state_changed *ev) {
    if (ev->position < ZMK_KEYMAP_LEN) {
        WRITE_BIT(held[ev->position / 32], ev->position % 32, ev->state);
    }

    for (size_t i = 0; i < LIL_COUNT; i++) {
        struct lil_state *st = &states[i];
        if (st->phase != LIL_ARMED) {
            continue;
        }

        /* A released baseline key leaves the baseline. */
        for (size_t w = 0; w < LIL_HELD_WORDS; w++) {
            st->baseline[w] &= held[w];
        }

        /* Any key event is activity: stop the clock while something new is held,
         * otherwise restart it from now. */
        if (lil_busy(st)) {
            k_work_cancel_delayable(&st->work);
        } else {
            k_work_reschedule(&st->work, K_MSEC(st->cfg->idle_ms));
        }
    }
}

static void lil_on_layer(const struct zmk_layer_state_changed *ev) {
    if (ev->state) {
        return;
    }

    struct lil_state *st = lil_find(ev->layer);
    if (st == NULL) {
        return;
    }

    /* A locked layer only gets here through &tog / &to (or another unlocking path). */
    bool was_locked = (st->phase == LIL_LOCKED);
    lil_disarm(st);
    if (was_locked) {
        LOG_DBG("layer %d unlocked", ev->layer);
        lil_fire(st->cfg, st->cfg->exit_bindings, st->cfg->exit_bindings_len);
    }
}

static int lil_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL) {
        lil_on_position(pos);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_layer_state_changed *lay = as_zmk_layer_state_changed(eh);
    if (lay != NULL) {
        lil_on_layer(lay);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#if IS_ENABLED(CONFIG_INPUT)
/* Pointing activity (trackpads on either half, mouse-key behaviors) also counts: moving the
 * cursor while holding the layer key must not lock the layer. This runs on the input
 * thread, not the system work queue. It only restarts the timers of ARMED children; k_work
 * calls are thread-safe, and lil_timeout() re-checks phase and held keys before locking,
 * so a reschedule racing a disarm or a key press cannot lock anything by itself. */
static void lil_input_cb(struct input_event *evt) {
    ARG_UNUSED(evt);

    for (size_t i = 0; i < LIL_COUNT; i++) {
        struct lil_state *st = &states[i];
        if (st->phase == LIL_ARMED) {
            k_work_reschedule(&st->work, K_MSEC(st->cfg->idle_ms));
        }
    }
}

INPUT_CALLBACK_DEFINE(NULL, lil_input_cb);
#endif /* IS_ENABLED(CONFIG_INPUT) */

/* This file is listed ahead of the hold-tap, combo and keymap sources in CMakeLists.txt,
 * and subscriptions run in link order, so this listener sees every key press and release
 * as it happens, before any of those can capture and later replay it. */
ZMK_LISTENER(layer_idle_lock, lil_listener);
ZMK_SUBSCRIPTION(layer_idle_lock, zmk_position_state_changed);
ZMK_SUBSCRIPTION(layer_idle_lock, zmk_layer_state_changed);

static int lil_init(void) {
    for (size_t i = 0; i < LIL_COUNT; i++) {
        k_work_init_delayable(&states[i].work, lil_timeout);
    }
    return 0;
}

SYS_INIT(lil_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else /* no zmk,layer-idle-lock node: the hooks are still referenced by &mo */

void zmk_layer_idle_lock_momentary_pressed(zmk_keymap_layer_id_t layer) {}
void zmk_layer_idle_lock_momentary_released(zmk_keymap_layer_id_t layer) {}

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
