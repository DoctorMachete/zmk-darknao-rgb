/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pixel_color

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

static int pixel_color_init(const struct device *dev) { return 0; }

/*
 * Global pixel addressing across the split.
 *
 * param1 = GLOBAL strip position. Indices [0 .. local_count-1] are the local
 *          (central / left) strip; indices >= local_count target the peripheral
 *          (right) strip, sent over the split as (position - local_count).
 * param2 = packed 0xRRGGBB color; a negative value clears that pixel.
 *
 * Behaviors with GLOBAL locality run on the central, so this routing always
 * executes there and relays remote pixels to the peripheral.
 */
static int pixel_color_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    int32_t color = (int32_t)binding->param2;
    uint32_t position = binding->param1;

    uint8_t local_count = zmk_rgb_underglow_pixel_count();

    // Group-clear sentinels (param2 ignored).
    if (position == ZMK_PIXEL_CLEAR_LEFT || position == ZMK_PIXEL_CLEAR_ALL) {
        LOG_DBG("pixel clear: left half");
        zmk_rgb_underglow_clear_pixels();
    }
    if (position == ZMK_PIXEL_CLEAR_RIGHT || position == ZMK_PIXEL_CLEAR_ALL) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        LOG_DBG("pixel clear: right half");
        zmk_split_central_clear_all_pixels();
#else
        LOG_DBG("pixel clear right requested but no split central");
#endif
    }
    if (position == ZMK_PIXEL_CLEAR_LEFT || position == ZMK_PIXEL_CLEAR_RIGHT ||
        position == ZMK_PIXEL_CLEAR_ALL) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (position < local_count) {
        LOG_DBG("pixel (local) pos %u color 0x%06X", position, binding->param2);
        zmk_rgb_underglow_set_pixel(position, color);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t remote_pos = (uint8_t)(position - local_count);
        LOG_DBG("pixel (remote) global %u -> peripheral pos %u color 0x%06X", position, remote_pos,
                binding->param2);
        if (color < 0) {
            zmk_split_central_clear_pixel(remote_pos);
        } else {
            zmk_split_central_set_pixel(remote_pos, (uint32_t)color);
        }
#else
        LOG_WRN("pixel pos %u beyond local strip and no split central available", position);
#endif
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int pixel_color_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api pixel_color_driver_api = {
    .binding_pressed = pixel_color_pressed,
    .binding_released = pixel_color_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, pixel_color_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &pixel_color_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
