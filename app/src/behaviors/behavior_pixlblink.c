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

/*
 * &pixlblink — sharp two-color blink of one pixel, on either half.
 *
 * This is a PRESET behavior: each named instance carries its own colors and
 * frequency as devicetree properties, and is invoked with a single parameter,
 * the position. Example:
 *
 *     blink_recording_macro: blink_recording_macro {
 *         compatible = "zmk,behavior-pixlblink";
 *         #binding-cells = <1>;
 *         frequency = <5>;                 // freq_hz = code / 10 -> 0.5 Hz
 *         color1 = <PX_ORANGE>;
 *         color2 = <PX_RED>;               // omit -> defaults to black (off)
 *     };
 *     // ... &blink_recording_macro PIXEL_LH_C3R2 ...
 *
 * param1 = GLOBAL strip position. Indices [0 .. local_count-1] are the local
 *          (central / left) strip; indices >= local_count target the peripheral
 *          (right) strip, sent over the split as (position - local_count).
 *
 * The colors/frequency come from the instance config (dev->config), NOT from the
 * binding, so the same handler serves every preset. Stop a blink with the fixed
 * &pixlblink_off behavior. GLOBAL locality => runs on the central, relays remote.
 */

struct behavior_pixlblink_config {
    uint32_t color1;   // 0xRRGGBB
    uint32_t color2;   // 0xRRGGBB (may be 0x000000 for an off half-cycle)
    uint8_t frequency; // freq code: freq_hz = code / 10
};

static int pixlblink_init(const struct device *dev) { return 0; }

static int pixlblink_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_pixlblink_config *cfg = dev->config;
    uint32_t position = binding->param1;

    uint8_t local_count = zmk_rgb_underglow_pixel_count();

    if (position < local_count) {
        LOG_DBG("pixlblink (local) pos %u c1=0x%06X c2=0x%06X freq=%u", position, cfg->color1,
                cfg->color2, cfg->frequency);
        zmk_rgb_underglow_set_pixlblink(position, cfg->color1, cfg->color2, cfg->frequency);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t remote_pos = (uint8_t)(position - local_count);
        LOG_DBG("pixlblink (remote) global %u -> peripheral pos %u c1=0x%06X c2=0x%06X freq=%u",
                position, remote_pos, cfg->color1, cfg->color2, cfg->frequency);
        zmk_split_central_set_pixlblink(remote_pos, cfg->color1, cfg->color2, cfg->frequency);
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

#define PB_INST(n)                                                                                 \
    static const struct behavior_pixlblink_config behavior_pixlblink_config_##n = {                \
        .color1 = DT_INST_PROP(n, color1),                                                         \
        .color2 = DT_INST_PROP(n, color2),                                                         \
        .frequency = DT_INST_PROP(n, frequency),                                                   \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, pixlblink_init, NULL, NULL, &behavior_pixlblink_config_##n,          \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
                            &pixlblink_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PB_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
