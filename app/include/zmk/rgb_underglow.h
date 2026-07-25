/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

int zmk_rgb_underglow_toggle(void);
int zmk_rgb_underglow_get_state(bool *state);
int zmk_rgb_underglow_on(void);
int zmk_rgb_underglow_off(void);
int zmk_rgb_underglow_transient_on(void);
int zmk_rgb_underglow_transient_off(void);
int zmk_rgb_underglow_cycle_effect(int direction);
int zmk_rgb_underglow_calc_effect(int direction);
int zmk_rgb_underglow_select_effect(int effect);
struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction);
int zmk_rgb_underglow_change_hue(int direction);
int zmk_rgb_underglow_change_sat(int direction);
int zmk_rgb_underglow_change_brt(int direction);
int zmk_rgb_underglow_change_spd(int direction);
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int zmk_rgb_underglow_status(void);

/*
 * Persistent per-pixel override.
 *
 * Forces a single LED (by strip position) to a fixed 0xRRGGBB color that is
 * drawn on top of whatever the underglow / indicators are doing, and that
 * persists across underglow on/off until it is explicitly cleared. Passing a
 * negative color clears the override for that position. The strip and ext-power
 * are driven on demand, so the pixel lights even if underglow is currently off.
 */
int zmk_rgb_underglow_set_pixel(uint32_t position, int32_t color);
int zmk_rgb_underglow_clear_pixels(void);
// Magic-indicator layer setter (separate layer, composited above &pixel
// overrides). Written by magic_indicator.c. color < 0 clears at that position.
int zmk_rgb_underglow_set_magic_pixel(uint32_t position, int32_t color);

/*
 * PIXLBLINK layer (&pixlblink behavior). Sharp square-wave blink of a single
 * pixel between `color` and BLACK (off). Separate parallel layer, composited
 * ABOVE &pixel overrides but BELOW magic indicators. color < 0 stops/clears the
 * blink at that position (revealing whatever is beneath). The strip / ext-power
 * are driven on demand and the refresh timer is kept alive while any blink is
 * active, so a blink runs even when underglow is off.
 *
 * Frequency is fixed in firmware for now (not a binding parameter). Encoding:
 *   freq_hz = PIXLBLINK_FREQ_CODE / 10   ->  code 5 = 0.5 Hz, code 10 = 1 Hz.
 * v001 default is 0.5 Hz. (Future: move PIXLBLINK_FREQ_CODE to Kconfig / go60.conf.)
 */
#ifndef PIXLBLINK_FREQ_CODE
#define PIXLBLINK_FREQ_CODE 5 /* 0.5 Hz */
#endif
int zmk_rgb_underglow_set_pixlblink(uint32_t position, int32_t color);

/*
 * Sentinel "positions" for the &pixel behavior to clear groups of overrides in
 * one binding (the param2 color is ignored for these):
 *   &pixel ZMK_PIXEL_CLEAR_LEFT  0   -> clear all overrides on the local/left half
 *   &pixel ZMK_PIXEL_CLEAR_RIGHT 0   -> clear all overrides on the peripheral/right half
 *   &pixel ZMK_PIXEL_CLEAR_ALL   0   -> clear both halves
 * Values are far above any real global strip index (0..59) so they never collide.
 */
#define ZMK_PIXEL_CLEAR_LEFT  250
#define ZMK_PIXEL_CLEAR_RIGHT 251
#define ZMK_PIXEL_CLEAR_ALL   252

// Number of physical LEDs on THIS controller's local strip (the split boundary
// for global pixel addressing).
uint8_t zmk_rgb_underglow_pixel_count(void);

/*
 * Persistent, relocatable status indicators. Unlike set_pixel (fixed color),
 * these recompute their colors live each refresh so they track the current
 * battery level / USB state, and persist across underglow on/off until cleared.
 *
 * Battery: pass an array of `count` strip positions (the block lights as a
 * charge ramp, green/yellow/red by level). Pass count 0 (or the clear helper)
 * to disable. USB: a single position colored by connection state.
 *
 * Positions are physical strip indices on the local half (0..STRIP_NUM_PIXELS-1).
 */
int zmk_rgb_underglow_set_battery_indicator(const uint8_t *positions, uint8_t count);
int zmk_rgb_underglow_clear_battery_indicator(void);
int zmk_rgb_underglow_set_usb_indicator(uint32_t position);
int zmk_rgb_underglow_clear_usb_indicator(void);
