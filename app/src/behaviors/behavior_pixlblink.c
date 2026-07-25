/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pixlblink

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

static int pixlblink_init(const struct device *dev) { return 0; }

/*
 * &pixlblink — sharp single-color blink (color <-> black) of one pixel, on either
 * half. Sibling of &pixel: same GLOBAL addressing and split routing, but writes
 * the separate PIXLBLINK layer and animates a square wave at a fixed firmware
 * frequency (PIXLBLINK_FREQ_CODE). The old &pixel behavior is untouched.
 *
 * param1 = GLOBAL strip position. Indices [0 .. local_count-1] are the local
 *          (central / left) strip; indices >= local_count target the peripheral
 *          (right) strip, sent over the split as (position - local_count).
 * param2 = packed 0xRRGGBB "on" color; a negative value stops/clears the blink.
 *
 * GLOBAL locality => this runs on the central and relays remote pixels.
 */
static int pixlblink_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    int32_t color = (int32_t)binding->param2;
    uint32_t position = binding->param1;

    uint8_t local_count = zmk_rgb_underglow_pixel_count();

    if (position < local_count) {
        LOG_DBG("pixlblink (local) pos %u color 0x%06X", position, binding->param2);
        zmk_rgb_underglow_set_pixlblink(position, color);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t remote_pos = (uint8_t)(position - local_count);
        LOG_DBG("pixlblink (remote) global %u -> peripheral pos %u color 0x%06X", position,
                remote_pos, binding->param2);
        if (color < 0) {
            zmk_split_central_clear_pixlblink(remote_pos);
        } else {
            zmk_split_central_set_pixlblink(remote_pos, (uint32_t)color);
        }
#else
        LOG_WRN("pixlblink pos %u beyond local strip and no split central available", position);
#endif
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int pixlblink_released(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api pixlblink_driver_api = {
    .binding_pressed = pixlblink_pressed,
    .binding_released = pixlblink_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, pixlblink_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &pixlblink_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
