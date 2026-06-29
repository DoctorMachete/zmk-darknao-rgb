/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_usb_indicator

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

struct usb_indicator_config {
    uint8_t position;
};

// param1 != 0 -> show the USB output indicator at the configured position
// param1 == 0 -> hide it
//
// position is a GLOBAL index: < local_count -> local strip, >= local_count ->
// peripheral strip (sent as position - local_count).
static int usb_indicator_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct usb_indicator_config *cfg = dev->config;

    uint8_t local_count = zmk_rgb_underglow_pixel_count();
    bool remote = (cfg->position >= local_count);

    if (binding->param1) {
        LOG_DBG("usb indicator on (position %d, %s)", cfg->position, remote ? "remote" : "local");
        if (!remote) {
            zmk_rgb_underglow_set_usb_indicator(cfg->position);
        } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
            zmk_split_central_set_usb_indicator((uint8_t)(cfg->position - local_count));
#else
            LOG_WRN("usb indicator targets remote half but no split central");
#endif
        }
    } else {
        LOG_DBG("usb indicator off (%s)", remote ? "remote" : "local");
        if (!remote) {
            zmk_rgb_underglow_clear_usb_indicator();
        } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
            zmk_split_central_clear_usb_indicator();
#endif
        }
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int usb_indicator_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api usb_indicator_driver_api = {
    .binding_pressed = usb_indicator_pressed,
    .binding_released = usb_indicator_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define USB_INDICATOR_INST(n)                                                                      \
    static const struct usb_indicator_config usb_indicator_config_##n = {                          \
        .position = DT_INST_PROP(n, position),                                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &usb_indicator_config_##n, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &usb_indicator_driver_api);

DT_INST_FOREACH_STATUS_OKAY(USB_INDICATOR_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
