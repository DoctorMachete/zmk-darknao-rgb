/*
 * behavior_magic_indicator_off.c   ->  &magic_indicator_off
 *
 * param1 = GLOBAL pixel position, param2 = WHICH. Deregisters the indicator and
 * clears that pixel (locally or on the peripheral, by address).
 */

#define DT_DRV_COMPAT zmk_behavior_magic_indicator_off

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/magic_indicator.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int mi_off_init(const struct device *dev) { return 0; }

static int mi_off_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    zmk_magic_indicator_off(binding->param1, (uint8_t)binding->param2);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mi_off_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mi_off_driver_api = {
    .binding_pressed = mi_off_pressed,
    .binding_released = mi_off_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, mi_off_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mi_off_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
