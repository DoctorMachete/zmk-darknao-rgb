/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: &mirror-morph, zmk,behavior-mirror-morph (PATCH_MIRROR_MODE.md §5).
 *
 * Picks one of its bindings from the mirror instance that applies at press time (on, and the
 * top layer in one of its groups): bindings[0] when none does; bindings[1] when any does
 * (no `instances`); bindings[i + 1] for instances[i] (with `instances`). The release goes to
 * the binding that was pressed, tracked per key position, so a mirror switch while the key
 * is held can't strand it.
 */

#define DT_DRV_COMPAT zmk_behavior_mirror_morph

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/mirror_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define MM_MAX_HELD CONFIG_ZMK_BEHAVIOR_MIRROR_MORPH_MAX_HELD

struct behavior_mirror_morph_config {
    const struct zmk_behavior_binding *bindings;
    const struct device *const *instances; /* NULL: any instance picks bindings[1] */
    uint8_t instances_len;
};

struct mirror_morph_slot {
    bool used;
    uint32_t position;
    uint8_t index; /* into bindings */
};

struct behavior_mirror_morph_data {
    struct mirror_morph_slot slots[MM_MAX_HELD];
};

static uint8_t mirror_morph_pick(const struct behavior_mirror_morph_config *cfg) {
    const struct device *inst = zmk_mirror_mode_applying_instance();
    if (inst == NULL) {
        return 0;
    }
    if (cfg->instances == NULL) {
        return 1;
    }
    for (uint8_t i = 0; i < cfg->instances_len; i++) {
        if (cfg->instances[i] == inst) {
            return i + 1;
        }
    }
    return 0;
}

static int on_mirror_morph_binding_pressed(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_mirror_morph_config *cfg = dev->config;
    struct behavior_mirror_morph_data *data = dev->data;

    struct mirror_morph_slot *slot = NULL;
    for (int i = 0; i < MM_MAX_HELD; i++) {
        if (data->slots[i].used && data->slots[i].position == event.position) {
            LOG_ERR("mirror-morph: position %d pressed twice", event.position);
            return -ENOTSUP;
        }
        if (!data->slots[i].used && slot == NULL) {
            slot = &data->slots[i];
        }
    }
    if (slot == NULL) {
        LOG_ERR("mirror-morph: too many keys held (CONFIG_ZMK_BEHAVIOR_MIRROR_MORPH_MAX_HELD)");
        return -ENOMEM;
    }

    uint8_t idx = mirror_morph_pick(cfg);
    *slot = (struct mirror_morph_slot){.used = true, .position = event.position, .index = idx};
    LOG_DBG("mirror-morph: position %d uses binding %d", event.position, idx);
    return zmk_behavior_invoke_binding(&cfg->bindings[idx], event, true);
}

static int on_mirror_morph_binding_released(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_mirror_morph_config *cfg = dev->config;
    struct behavior_mirror_morph_data *data = dev->data;

    for (int i = 0; i < MM_MAX_HELD; i++) {
        struct mirror_morph_slot *slot = &data->slots[i];
        if (slot->used && slot->position == event.position) {
            slot->used = false;
            return zmk_behavior_invoke_binding(&cfg->bindings[slot->index], event, false);
        }
    }
    LOG_ERR("mirror-morph: position %d released without a press", event.position);
    return -ENOTSUP;
}

static const struct behavior_driver_api behavior_mirror_morph_driver_api = {
    .binding_pressed = on_mirror_morph_binding_pressed,
    .binding_released = on_mirror_morph_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define MM_BINDING(idx, n)                                                                         \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)),                  \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param1))),                     \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param2))),                     \
    }

#define MM_INSTANCE(idx, n) DEVICE_DT_GET(DT_INST_PHANDLE_BY_IDX(n, instances, idx))

#define MM_CHECK_INSTANCE(node_id, prop, idx)                                                      \
    BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node_id, prop, idx), zmk_behavior_mirror), \
                 "zmk,behavior-mirror-morph: every `instances` entry must be a "                  \
                 "zmk,behavior-mirror node");

#define MM_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) ==                                                  \
                     1 + DT_INST_PROP_LEN_OR(n, instances, 1),                                     \
                 "zmk,behavior-mirror-morph: `bindings` needs one entry more than `instances` "    \
                 "(exactly 2 without `instances`)");                                               \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, instances),                                               \
                (DT_INST_FOREACH_PROP_ELEM(n, instances, MM_CHECK_INSTANCE)), ())                  \
    static const struct zmk_behavior_binding mm_bindings_##n[] = {                                 \
        LISTIFY(DT_INST_PROP_LEN(n, bindings), MM_BINDING, (, ), n)};                              \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, instances),                                               \
                (static const struct device *const mm_instances_##n[] = {                          \
                     LISTIFY(DT_INST_PROP_LEN(n, instances), MM_INSTANCE, (, ), n)};),             \
                ())                                                                                \
    static const struct behavior_mirror_morph_config mm_config_##n = {                             \
        .bindings = mm_bindings_##n,                                                               \
        .instances = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, instances), (mm_instances_##n), (NULL)), \
        .instances_len = DT_INST_PROP_LEN_OR(n, instances, 0),                                     \
    };                                                                                             \
    static struct behavior_mirror_morph_data mm_data_##n = {};                                     \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &mm_data_##n, &mm_config_##n, POST_KERNEL,              \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_mirror_morph_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MM_INST)

#endif
