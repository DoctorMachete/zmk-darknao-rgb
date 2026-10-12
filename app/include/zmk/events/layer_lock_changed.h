/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch (PATCH_LAYER_LOCKING.md): raised by keymap.c whenever a layer's lock bit
 * changes. Upstream ZMK re-raises zmk_layer_state_changed instead; this fork keeps that
 * event for real on/off changes only, so layer listeners don't fire `enter` twice.
 * On a locking activation it follows the layer_state_changed(on) event; on a locking
 * deactivation it follows layer_state_changed(off).
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_layer_lock_changed {
    uint8_t layer;
    bool locked;
    int64_t timestamp;
};

ZMK_EVENT_DECLARE(zmk_layer_lock_changed);

static inline int raise_layer_lock_changed(uint8_t layer, bool locked) {
    return raise_zmk_layer_lock_changed((struct zmk_layer_lock_changed){
        .layer = layer, .locked = locked, .timestamp = k_uptime_get()});
}
