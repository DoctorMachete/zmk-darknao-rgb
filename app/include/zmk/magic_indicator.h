/*
 * magic_indicator.h
 *
 * Central-side registry for relocatable BLE/USB status indicators.
 *
 * The central is the only side that knows BLE/USB connection state, so it
 * computes the native indicator color and applies it to the target pixel
 * (locally, or pushed to the peripheral over the existing split pixel channel).
 * A small registry remembers active indicators so they can be repainted live
 * whenever BLE/endpoint/USB state changes.
 */

#pragma once

#include <stdint.h>

/* WHICH values match dt-bindings/zmk/magic-indicator.h (MAGIC_BLE0..4 = 0..4,
 * MAGIC_USB = 64). */
#define ZMK_MAGIC_INDICATOR_USB 64

/*
 * Register (or update) an indicator at GLOBAL pixel `position` showing source
 * `which`, and paint it immediately. Idempotent: re-registering the same
 * (position, which) just repaints. Returns 0 on success, negative errno.
 */
int zmk_magic_indicator_on(uint32_t position, uint8_t which);

/*
 * Remove the indicator at `position` for `which` and clear that pixel.
 * Safe to call if it was never registered.
 */
int zmk_magic_indicator_off(uint32_t position, uint8_t which);

/* Remove ALL indicators (both halves) and clear their pixels. */
int zmk_magic_indicator_clear_all(void);
