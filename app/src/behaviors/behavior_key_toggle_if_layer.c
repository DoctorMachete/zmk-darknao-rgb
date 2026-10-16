/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: &kt_iflayer KEY LAYER (PATCH_KEY_TOGGLE_IF_LAYER.md).
 *
 * &kt with a condition: on press, if LAYER is active, toggle KEY exactly as &kt would
 * (`toggle-mode` "flip" by default, or "on" / "off"); if LAYER is not active, do nothing.
 * The layer is checked at press time. "Active" is zmk_keymap_layer_active(): on by any
 * means (&mo, &tog, &to, a conditional layer, the idle lock), and the default layer always.
 * Release does nothing, as with &kt.
 */

#define DT_DRV_COMPAT zmk_behavior_key_toggle_if_layer

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Same order as the `toggle-mode` enum in the binding (and as &kt). */
enum kt_iflayer_toggle_mode {
    KT_IFLAYER_ON,
    KT_IFLAYER_OFF,
    KT_IFLAYER_FLIP,
};

struct behavior_kt_iflayer_config {
    enum kt_iflayer_toggle_mode toggle_mode;
};

static int kt_iflayer_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const uint32_t key = binding->param1;
    const uint32_t layer = binding->param2;

    if (layer >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_WRN("position %d: layer %u is not a layer of this keymap, ignored", event.position,
                layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (!zmk_keymap_layer_active((zmk_keymap_layer_id_t)layer)) {
        LOG_DBG("position %d keycode 0x%02X: layer %u off, not toggled", event.position, key,
                layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    LOG_DBG("position %d keycode 0x%02X: layer %u on, toggling", event.position, key, layer);

    const struct behavior_kt_iflayer_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;
    switch (cfg->toggle_mode) {
    case KT_IFLAYER_ON:
        return raise_zmk_keycode_state_changed_from_encoded(key, true, event.timestamp);
    case KT_IFLAYER_OFF:
        return raise_zmk_keycode_state_changed_from_encoded(key, false, event.timestamp);
    case KT_IFLAYER_FLIP: {
        const bool pressed = zmk_hid_is_pressed(key);
        return raise_zmk_keycode_state_changed_from_encoded(key, !pressed, event.timestamp);
    }
    default:
        return -ENOTSUP;
    }
}

static int kt_iflayer_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param1_values[] = {
    {
        .display_name = "Key",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
    },
};

static const struct behavior_parameter_value_metadata param2_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param1_values,
    .param1_values_len = ARRAY_SIZE(param1_values),
    .param2_values = param2_values,
    .param2_values_len = ARRAY_SIZE(param2_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_driver_api behavior_kt_iflayer_driver_api = {
    .binding_pressed = kt_iflayer_binding_pressed,
    .binding_released = kt_iflayer_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define KT_IFLAYER_INST(n)                                                                         \
    static const struct behavior_kt_iflayer_config behavior_kt_iflayer_config_##n = {              \
        .toggle_mode = DT_ENUM_IDX(DT_DRV_INST(n), toggle_mode),                                   \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_kt_iflayer_config_##n, POST_KERNEL,     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kt_iflayer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KT_IFLAYER_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
