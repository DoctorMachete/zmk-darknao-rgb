/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: mirror mode (PATCH_MIRROR_MODE.md).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_MIRROR)

/* `overlay-id` of the instance that is on, 0 if none (or if it has no overlay-id). */
uint8_t zmk_mirror_mode_active_overlay_id(void);

/* Layers the instance that is on mirrors on (bit per layer id), 0 if none. */
uint32_t zmk_mirror_mode_active_layers(void);

/* True while the instance that is on was turned on by MIRROR_ONESHOT. */
bool zmk_mirror_mode_oneshot_armed(void);

#endif

/*
 * Called by combo.c on every key press, for combo matching only: the position a combo
 * should see for physical key `position`. While an instance applies (it is on and the top
 * layer is in one of its groups, the key not excluded there), that is the instance's
 * `combo-positions` entry (its `mirror-positions` entry if it has no `combo-positions`);
 * otherwise the position itself.
 */
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_MIRROR)
int32_t zmk_mirror_mode_combo_position(int32_t position);

/*
 * Called by hold-tap, which runs before the mirror listener and so sees physical keys: the
 * position the event for physical key `position` will have once mirrored. For a press, the
 * key it acts as right now; for a release, the key its press acted as.
 */
int32_t zmk_mirror_mode_key_position(int32_t position, bool pressed);

/*
 * The instance that applies right now (on, and the top layer in one of its groups), or NULL.
 * Used by &mirror-morph (PATCH_MIRROR_MODE.md §5).
 */
struct device;
const struct device *zmk_mirror_mode_applying_instance(void);
#else
struct device;
static inline int32_t zmk_mirror_mode_combo_position(int32_t position) { return position; }
static inline int32_t zmk_mirror_mode_key_position(int32_t position, bool pressed) {
    ARG_UNUSED(pressed);
    return position;
}
static inline const struct device *zmk_mirror_mode_applying_instance(void) { return NULL; }
#endif
