/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_caps_word

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct caps_word_continue_item {
    uint16_t page;
    uint32_t id;
    uint8_t implicit_modifiers;
};

struct behavior_caps_word_config {
    zmk_mod_flags_t mods;
    /* Indicator / side-effect outputs fired on the activation state edges.
     * Non-const pointers: behavior_keymap_binding_pressed() takes a non-const
     * binding. `continuations[]` is a flexible array member and MUST stay last. */
    struct zmk_behavior_binding *enter_bindings;
    struct zmk_behavior_binding *exit_bindings;
    uint8_t enter_bindings_len;
    uint8_t exit_bindings_len;
    bool not_press_release_outputs;
    uint16_t tap_ms;
    uint16_t wait_ms;
    uint8_t continuations_count;
    struct caps_word_continue_item continuations[];
};

struct behavior_caps_word_data {
    bool active;
};

/* Fire one `enter` / `exit` behavior chain, in order.
 *
 * NOTE: behavior_keymap_binding_pressed(), NOT zmk_behavior_invoke_binding().
 * The latter honours DT locality, and the fork's pixel behaviors are
 * BEHAVIOR_LOCALITY_GLOBAL, so it would additionally ship a split
 * invoke-behavior message to the peripheral -- where the handler would take the
 * non-central branch and warn. Those behaviors relay themselves from inside the
 * handler. This matches what zmk-listeners does, for the same reason. */
static void caps_word_fire(const struct behavior_caps_word_config *config,
                           struct zmk_behavior_binding *bindings, size_t len) {
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
        if (config->not_press_release_outputs) {
            behavior_keymap_binding_pressed(&bindings[i], event);
        } else {
            zmk_behavior_queue_add(&event, bindings[i], true, config->tap_ms);
            zmk_behavior_queue_add(&event, bindings[i], false, config->wait_ms);
        }
    }
}

/* The single chokepoint for caps-word state. Edge-only: a redundant call is a
 * no-op, which also terminates the feedback path if an `enter` binding ever
 * raises a keycode (the keycode listener below would treat it as a word break). */
static void set_caps_word_state(const struct device *dev, bool active) {
    struct behavior_caps_word_data *data = dev->data;
    const struct behavior_caps_word_config *config = dev->config;

    if (data->active == active) {
        return;
    }

    data->active = active;

    if (active) {
        caps_word_fire(config, config->enter_bindings, config->enter_bindings_len);
    } else {
        caps_word_fire(config, config->exit_bindings, config->exit_bindings_len);
    }
}

static void activate_caps_word(const struct device *dev) { set_caps_word_state(dev, true); }

static void deactivate_caps_word(const struct device *dev) { set_caps_word_state(dev, false); }

static int on_caps_word_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_caps_word_data *data = dev->data;

    if (data->active) {
        deactivate_caps_word(dev);
    } else {
        activate_caps_word(dev);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_caps_word_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_caps_word_driver_api = {
    .binding_pressed = on_caps_word_binding_pressed,
    .binding_released = on_caps_word_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

static int caps_word_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_caps_word, caps_word_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_caps_word, zmk_keycode_state_changed);

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static bool caps_word_is_caps_includelist(const struct behavior_caps_word_config *config,
                                          uint16_t usage_page, uint8_t usage_id,
                                          uint8_t implicit_modifiers) {
    for (int i = 0; i < config->continuations_count; i++) {
        const struct caps_word_continue_item *continuation = &config->continuations[i];
        LOG_DBG("Comparing with 0x%02X - 0x%02X (with implicit mods: 0x%02X)", continuation->page,
                continuation->id, continuation->implicit_modifiers);

        if (continuation->page == usage_page && continuation->id == usage_id &&
            (continuation->implicit_modifiers &
             (implicit_modifiers | zmk_hid_get_explicit_mods())) ==
                continuation->implicit_modifiers) {
            LOG_DBG("Continuing capsword, found included usage: 0x%02X - 0x%02X", usage_page,
                    usage_id);
            return true;
        }
    }

    return false;
}

static bool caps_word_is_alpha(uint8_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_A && usage_id <= HID_USAGE_KEY_KEYBOARD_Z);
}

static bool caps_word_is_numeric(uint8_t usage_id) {
    return (usage_id >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
            usage_id <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
}

static void caps_word_enhance_usage(const struct behavior_caps_word_config *config,
                                    struct zmk_keycode_state_changed *ev) {
    if (ev->usage_page != HID_USAGE_KEY || !caps_word_is_alpha(ev->keycode)) {
        return;
    }

    LOG_DBG("Enhancing usage 0x%02X with modifiers: 0x%02X", ev->keycode, config->mods);
    ev->implicit_modifiers |= config->mods;
}

static int caps_word_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];

        struct behavior_caps_word_data *data = dev->data;
        if (!data->active) {
            continue;
        }

        const struct behavior_caps_word_config *config = dev->config;

        caps_word_enhance_usage(config, ev);

        if (!caps_word_is_alpha(ev->keycode) && !caps_word_is_numeric(ev->keycode) &&
            !is_mod(ev->usage_page, ev->keycode) &&
            !caps_word_is_caps_includelist(config, ev->usage_page, ev->keycode,
                                           ev->implicit_modifiers)) {
            LOG_DBG("Deactivating caps_word for 0x%02X - 0x%02X", ev->usage_page, ev->keycode);
            deactivate_caps_word(dev);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define CAPS_WORD_LABEL(i, _n) DT_INST_LABEL(i)

#define PARSE_BREAK(i)                                                                             \
    {.page = ZMK_HID_USAGE_PAGE(i), .id = ZMK_HID_USAGE_ID(i), .implicit_modifiers = SELECT_MODS(i)}

#define BREAK_ITEM(i, n) PARSE_BREAK(DT_INST_PROP_BY_IDX(n, continue_list, i))

/* ZMK_KEYMAP_EXTRACT_BINDING is hardcoded to the `bindings` property, so these are
 * prop-parameterised twins on DT_INST_*, because `enter` / `exit` live on the
 * instance node itself rather than on a child. Same trick as
 * zmk-listeners/src/listener_bindings.h. */
#define CW_EXTRACT_BINDING(idx, n, prop)                                                           \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, prop, idx)),                      \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, prop, idx, param1), (0),              \
                              (DT_INST_PHA_BY_IDX(n, prop, idx, param1))),                         \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, prop, idx, param2), (0),              \
                              (DT_INST_PHA_BY_IDX(n, prop, idx, param2))),                         \
    }

#define CW_BINDINGS_ARRAY(n, prop, name)                                                           \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop),                                                    \
                (static struct zmk_behavior_binding name[DT_INST_PROP_LEN(n, prop)] = {            \
                     LISTIFY(DT_INST_PROP_LEN(n, prop), CW_EXTRACT_BINDING, (, ), n, prop)};),     \
                ())

#define CW_PROP_LEN(n, prop)                                                                       \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop), (DT_INST_PROP_LEN(n, prop)), (0))

#define CW_BINDINGS_PTR(n, prop, name)                                                             \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop), (name), (NULL))

#define KP_INST(n)                                                                                 \
    CW_BINDINGS_ARRAY(n, enter, behavior_caps_word_enter_##n)                                      \
    CW_BINDINGS_ARRAY(n, exit, behavior_caps_word_exit_##n)                                        \
    static struct behavior_caps_word_data behavior_caps_word_data_##n = {.active = false};         \
    static const struct behavior_caps_word_config behavior_caps_word_config_##n = {                \
        .mods = DT_INST_PROP_OR(n, mods, MOD_LSFT),                                                \
        .enter_bindings = CW_BINDINGS_PTR(n, enter, behavior_caps_word_enter_##n),                 \
        .enter_bindings_len = CW_PROP_LEN(n, enter),                                               \
        .exit_bindings = CW_BINDINGS_PTR(n, exit, behavior_caps_word_exit_##n),                    \
        .exit_bindings_len = CW_PROP_LEN(n, exit),                                                 \
        .not_press_release_outputs = DT_INST_PROP(n, not_press_release_outputs),                   \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .continuations = {LISTIFY(DT_INST_PROP_LEN(n, continue_list), BREAK_ITEM, (, ), n)},       \
        .continuations_count = DT_INST_PROP_LEN(n, continue_list),                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_caps_word_data_##n,                           \
                            &behavior_caps_word_config_##n, POST_KERNEL,                           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_caps_word_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif
