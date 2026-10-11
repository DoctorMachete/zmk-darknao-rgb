/*
 * behavior_magic_indicator_clear.c   ->  &magic_indicator_clear
 *
 * No parameters. Removes ALL active indicators (both halves) and clears their
 * pixels. GLOBAL locality.
 */

#define DT_DRV_COMPAT zmk_behavior_magic_indicator_clear

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/magic_indicator.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int mi_clear_init(const struct device *dev) { return 0; }

static int mi_clear_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    zmk_magic_indicator_clear_all();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mi_clear_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mi_clear_driver_api = {
    .binding_pressed = mi_clear_pressed,
    .binding_released = mi_clear_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, mi_clear_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mi_clear_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
