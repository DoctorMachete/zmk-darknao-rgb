/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_battery_indicator

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct battery_indicator_config {
    const uint8_t *positions;
    size_t positions_len;
};

// param1 != 0 -> show the battery block at the configured positions
// param1 == 0 -> hide it
//
// Positions are GLOBAL indices: < local_count -> local strip, >= local_count
// -> peripheral strip (sent as position - local_count). A block should live
// entirely on one half; routing is decided by the first position.
static int battery_indicator_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct battery_indicator_config *cfg = dev->config;

    uint8_t local_count = zmk_rgb_underglow_pixel_count();
    bool remote = (cfg->positions_len > 0 && cfg->positions[0] >= local_count);

    if (binding->param1) {
        LOG_DBG("battery indicator on (%d positions, %s)", (int)cfg->positions_len,
                remote ? "remote" : "local");
        if (!remote) {
            zmk_rgb_underglow_set_battery_indicator(cfg->positions, cfg->positions_len);
        } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
            uint8_t remapped[8];
            size_t n = cfg->positions_len > 8 ? 8 : cfg->positions_len;
            for (size_t i = 0; i < n; i++) {
                remapped[i] = (cfg->positions[i] >= local_count)
                                  ? (uint8_t)(cfg->positions[i] - local_count)
                                  : cfg->positions[i];
            }
            zmk_split_central_set_battery_indicator(remapped, n);
#else
            LOG_WRN("battery indicator targets remote half but no split central");
#endif
        }
    } else {
        LOG_DBG("battery indicator off (%s)", remote ? "remote" : "local");
        if (!remote) {
            zmk_rgb_underglow_clear_battery_indicator();
        } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
            zmk_split_central_clear_battery_indicator();
#endif
        }
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int battery_indicator_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api battery_indicator_driver_api = {
    .binding_pressed = battery_indicator_pressed,
    .binding_released = battery_indicator_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define BATTERY_INDICATOR_INST(n)                                                                  \
    static const uint8_t battery_indicator_positions_##n[] = DT_INST_PROP(n, positions);           \
    static const struct battery_indicator_config battery_indicator_config_##n = {                  \
        .positions = battery_indicator_positions_##n,                                              \
        .positions_len = DT_INST_PROP_LEN(n, positions),                                           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &battery_indicator_config_##n, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &battery_indicator_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BATTERY_INDICATOR_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
