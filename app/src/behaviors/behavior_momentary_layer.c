/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_momentary_layer

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/layer_idle_lock.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

// Fork: upstream's `locking;` (PATCH_LAYER_LOCKING.md). Stock &mo is non-locking.
struct behavior_mo_config {
    bool locking;
};

static int mo_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d layer %d", event.position, binding->param1);
    const struct behavior_mo_config *cfg = zmk_behavior_get_binding(binding->behavior_dev)->config;
    int ret = zmk_keymap_layer_activate_ex(binding->param1, cfg->locking);
    // Fork: arm the idle lock, if this layer has one (PATCH_LAYER_IDLE_LOCK.md).
    zmk_layer_idle_lock_momentary_pressed(binding->param1);
    return ret;
}

static int mo_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d layer %d", event.position, binding->param1);
    const struct behavior_mo_config *cfg = zmk_behavior_get_binding(binding->behavior_dev)->config;
    // Fork: disarm the idle lock. If the layer is locked, a non-locking deactivation below is
    // ignored by the keymap (PATCH_LAYER_IDLE_LOCK.md).
    zmk_layer_idle_lock_momentary_released(binding->param1);
    return zmk_keymap_layer_deactivate_ex(binding->param1, cfg->locking);
}

static const struct behavior_driver_api behavior_mo_driver_api = {
    .binding_pressed = mo_keymap_binding_pressed,
    .binding_released = mo_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define MO_INST(n)                                                                                 \
    static const struct behavior_mo_config behavior_mo_config_##n = {                              \
        .locking = DT_INST_PROP_OR(n, locking, false),                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_mo_config_##n, POST_KERNEL,             \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_mo_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MO_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
