/*
 * behavior_magic_indicator_on.c   ->  &magic_indicator_on
 *
 * param1 = GLOBAL pixel position (same address space as &pixel: 0..local-1 =
 *          left/central, >= local = right/peripheral).
 * param2 = WHICH (MAGIC_BLE0..4 or MAGIC_USB, from dt-bindings/zmk/magic-indicator.h).
 *
 * Registers the indicator and paints it in the native BLE/USB color. GLOBAL
 * locality so it runs on the central where the state lives.
 */

#define DT_DRV_COMPAT zmk_behavior_magic_indicator_on

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/magic_indicator.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int mi_on_init(const struct device *dev) { return 0; }

static int mi_on_pressed(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    zmk_magic_indicator_on(binding->param1, (uint8_t)binding->param2);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mi_on_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mi_on_driver_api = {
    .binding_pressed = mi_on_pressed,
    .binding_released = mi_on_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, mi_on_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mi_on_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
