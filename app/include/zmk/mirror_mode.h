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
#include <zmk/events/position_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_MIRROR)

/* `overlay-id` of the instance that is on, 0 if none (or if it has no overlay-id). */
uint8_t zmk_mirror_mode_active_overlay_id(void);

/* Layers the instance that is on mirrors on (bit per layer id), 0 if none. */
uint32_t zmk_mirror_mode_active_layers(void);

/* True while the instance that is on was turned on by MIRROR_ONESHOT. */
bool zmk_mirror_mode_oneshot_armed(void);

#endif

/*
 * Called by combo.c right before it re-raises a position event from the start of the
 * listener chain. With combos mirrored, the mirror listener runs before combo and has
 * already rewritten that event, so it must let the re-raise through untouched.
 */
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_MIRROR) && IS_ENABLED(CONFIG_ZMK_MIRROR_MODE_COMBOS)
void zmk_mirror_mode_pass_next(const struct zmk_position_state_changed *ev);

/*
 * Called by combo.c when a combo fires. If every key of the combo (`positions`, `len`) was
 * last pressed through mirroring, and the active instance's group for the top layer lists
 * this combo in `combo-pairs`, copy the partner combo's key positions to `partner` and
 * return how many there are. Otherwise (or if they don't fit in `cap`) return 0.
 */
int zmk_mirror_mode_combo_partner(const int32_t *positions, int len, int32_t *partner, int cap);
#else
static inline void zmk_mirror_mode_pass_next(const struct zmk_position_state_changed *ev) {
    ARG_UNUSED(ev);
}
#endif
