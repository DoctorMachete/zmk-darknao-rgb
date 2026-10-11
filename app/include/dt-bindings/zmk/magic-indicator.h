/*
 * magic-indicator.h
 *
 * WHICH constants for the &magic_indicator_on / &magic_indicator_off behaviors.
 *
 *   &magic_indicator_on  PIXEL_RH_C1R2 MAGIC_BLE0
 *   &magic_indicator_off PIXEL_RH_C1R2 MAGIC_BLE0
 *   &magic_indicator_on  PIXEL_LH_C6R1 MAGIC_USB
 *
 * Position uses the SAME global address space as the &pixel behavior
 * (go60-pixels.h): 0..local_count-1 = left/central strip, >= local_count =
 * right/peripheral strip. So PIXEL_LH_* / PIXEL_RH_* names work unchanged.
 *
 * The color is NOT specified here — it is derived from live BLE/USB state using
 * the native Magic-layer color ladder (white/dull-green/red/lilac).
 */

#pragma once

/* BLE profile indicators. Values are the profile index 0..4. */
#define MAGIC_BLE0 0
#define MAGIC_BLE1 1
#define MAGIC_BLE2 2
#define MAGIC_BLE3 3
#define MAGIC_BLE4 4

/* USB indicator. Kept distinct from the BLE index range via a high offset so a
 * single param2 value unambiguously selects USB vs a BLE profile. */
#define MAGIC_USB  64
