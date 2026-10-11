/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: idle-lock for momentary layers (PATCH_LAYER_IDLE_LOCK.md).
 *
 * &mo calls these two hooks around its own layer activate / deactivate. Without a
 * `zmk,layer-idle-lock` node (or on a split peripheral) they compile to nothing.
 */

#pragma once

#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_LAYER_IDLE_LOCK)

/* Call after the layer has been activated: arms the idle timer if `layer` is configured. */
void zmk_layer_idle_lock_momentary_pressed(zmk_keymap_layer_id_t layer);

/* Call before the layer is deactivated: disarms a pending timer. A layer that is already
 * locked stays locked; the keymap then ignores the deactivation that follows. */
void zmk_layer_idle_lock_momentary_released(zmk_keymap_layer_id_t layer);

#else

static inline void zmk_layer_idle_lock_momentary_pressed(zmk_keymap_layer_id_t layer) {}
static inline void zmk_layer_idle_lock_momentary_released(zmk_keymap_layer_id_t layer) {}

#endif
