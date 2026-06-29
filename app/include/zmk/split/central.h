/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/bluetooth/addr.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)

#include <zmk/ble.h>
#define BLE_PERIPHERAL_COUNT ZMK_SPLIT_BLE_PERIPHERAL_COUNT

#else

#define BLE_PERIPHERAL_COUNT 0

#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_WIRED)
#define WIRED_PERIPHERAL_COUNT 1
#else
#define WIRED_PERIPHERAL_COUNT 0
#endif

#define ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT MAX(BLE_PERIPHERAL_COUNT, WIRED_PERIPHERAL_COUNT)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
#include <zmk/hid_indicators_types.h>
#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

int zmk_split_central_invoke_behavior(uint8_t source, struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event, bool state);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

int zmk_split_central_update_hid_indicator(zmk_hid_indicators_t indicators);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

int zmk_split_central_get_peripheral_battery_level(uint8_t source, uint8_t *level);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

int zmk_split_central_update_layers(uint32_t layers);

/*
 * Push per-pixel RGB overrides and relocatable indicators to the peripheral
 * half(s). Positions are local strip indices on the peripheral. These mirror
 * the local zmk_rgb_underglow_* calls but target the other side of the split.
 */
int zmk_split_central_set_pixel(uint8_t position, uint32_t color);
int zmk_split_central_clear_pixel(uint8_t position);
int zmk_split_central_clear_all_pixels(void);
int zmk_split_central_set_battery_indicator(const uint8_t *positions, uint8_t count);
int zmk_split_central_clear_battery_indicator(void);
int zmk_split_central_set_usb_indicator(uint8_t position);
int zmk_split_central_clear_usb_indicator(void);
