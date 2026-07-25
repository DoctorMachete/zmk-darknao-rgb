/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_pixlblink_off

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

/*
 * &pixlblink_off — fixed, non-configurable companion to &pixlblink. Stops the
 * blink at a single position (revealing whatever is beneath: a &pixel override or
 * the underglow). Shipped as one instance by the fork; not user-configurable.
 *
 * param1 = GLOBAL strip position (same address space as &pixlblink / &pixel).
 *
 * GLOBAL locality => runs on the central and relays remote clears.
 */
static int pixlblink_off_init(const struct device *dev) { return 0; }

static int pixlblink_off_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    uint32_t position = binding->param1;
    uint8_t local_count = zmk_rgb_underglow_pixel_count();

    if (position < local_count) {
        LOG_DBG("pixlblink_off (local) pos %u", position);
        zmk_rgb_underglow_clear_pixlblink(position);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t remote_pos = (uint8_t)(position - local_count);
        LOG_DBG("pixlblink_off (remote) global %u -> peripheral pos %u", position, remote_pos);
        zmk_split_central_clear_pixlblink(remote_pos);
#else
        LOG_WRN("pixlblink_off pos %u beyond local strip and no split central available", position);
#endif
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int pixlblink_off_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api pixlblink_off_driver_api = {
    .binding_pressed = pixlblink_off_pressed,
    .binding_released = pixlblink_off_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, pixlblink_off_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &pixlblink_off_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
