/*
 * behavior_magic_indicator_group.c   ->  &magic_status_ble (or any instance)
 *
 * Paints a FIXED SET of BLE/USB indicators in a SINGLE behavior invocation,
 * so a layer-listener enter list is ONE queue entry instead of N. This is the
 * low-latency alternative to chaining N &magic_indicator_on behaviors.
 *
 * Node shape:
 *   magic_status_ble: magic_status_ble {
 *       compatible = "zmk,behavior-magic-indicator-group";
 *       #binding-cells = <0>;
 *       positions  = <PIXEL_LH_C2R4 PIXEL_LH_C3R4 PIXEL_LH_C4R4 PIXEL_LH_C5R4 PIXEL_LH_C1R4>;
 *       selection  = <MAGIC_BLE0    MAGIC_BLE1    MAGIC_BLE2    MAGIC_BLE3    MAGIC_USB>;
 *   };
 *
 * `positions` and `selection` MUST be the same length. On press, registers each
 * (position, which) pair via the shared registry (zmk_magic_indicator_on), so
 * colors, cross-half routing, and live updates all work exactly as the single
 * &magic_indicator_on behavior. The single/off/clear behaviors are unaffected.
 *
 * NOTE: the shared registry cap is MI_MAX (8). A group of N uses N slots; keep
 * (group size + any isolated single indicators) <= 8.
 *
 * GLOBAL locality (runs on the central, where BLE/USB state lives).
 */

#define DT_DRV_COMPAT zmk_behavior_magic_indicator_group

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/magic_indicator.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct mi_group_config {
    const uint32_t *positions;
    const uint8_t *selection;
    size_t count;
};

static int mi_group_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct mi_group_config *cfg = dev->config;

    for (size_t i = 0; i < cfg->count; i++) {
        zmk_magic_indicator_on(cfg->positions[i], cfg->selection[i]);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mi_group_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mi_group_driver_api = {
    .binding_pressed = mi_group_pressed,
    .binding_released = mi_group_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

/* Per-instance arrays generated from the node's `positions` / `selection`. */
#define MI_GROUP_INST(n)                                                                           \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, positions) == DT_INST_PROP_LEN(n, selection),                 \
                 "magic-indicator-group: positions and selection must be the same length");        \
    static const uint32_t mi_group_positions_##n[] = DT_INST_PROP(n, positions);                   \
    static const uint8_t mi_group_selection_##n[] = DT_INST_PROP(n, selection);                    \
    static const struct mi_group_config mi_group_config_##n = {                                    \
        .positions = mi_group_positions_##n,                                                        \
        .selection = mi_group_selection_##n,                                                        \
        .count = DT_INST_PROP_LEN(n, positions),                                                    \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &mi_group_config_##n, POST_KERNEL,                 \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mi_group_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MI_GROUP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
