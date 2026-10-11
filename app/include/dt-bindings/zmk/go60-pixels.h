/*
 * go60-pixels.h
 *
 * GLOBAL pixel addresses for the &pixel behavior, covering BOTH halves.
 *
 *     &pixel PIXEL_LH_C6R1 RED_RGB    // left half, top-left key LED
 *     &pixel PIXEL_RH_C6R1 RED_RGB    // right half, top-outer key LED
 *     &pixel PIXEL_RH_T1   0x00FF00   // right thumb
 *     &pixel PIXEL_LH_C6R1 (-1)       // clear that pixel
 *
 * HOW THESE WERE DERIVED (not guessed):
 *   Each GO60 half drives its own 30-LED strip. The firmware maps each local
 *   physical pixel to a keymap position via the `pixel-lookup` table in
 *   go60_lh.dts / go60_rh.dts (this is the same table the per-layer RGB map
 *   uses, which is why per-layer colors land on the right keys). Inverting that
 *   table gives, for each key, the local physical pixel that lights it.
 *
 *   The &pixel behavior uses a GLOBAL address space:
 *       0..29   -> left  (central) strip, written locally
 *       30..59  -> right (peripheral) strip, sent over the split as (index-30)
 *
 *   So PIXEL_LH_* = local physical index (0..29), and
 *      PIXEL_RH_* = 30 + right-local physical index.
 *
 *   Cross-check: go60_lh.dts's layout comment marks the top-left key's LED as
 *   physical pixel 26; the inverted lookup independently yields 26 for that key.
 *   The right half is mapped by the identical method.
 *
 * NAMING: PIXEL_<half>_C<col>R<row>, columns C6..C1 outer->inner (matching the
 * POS_* macros), rows R1..R4 top->bottom, R5 = inner bottom keys, T1..T3 thumbs.
 *
 * NOTE: indices are NOT contiguous in key order because each strip snakes
 * differently from the keymap grid. Always use these names, not raw +1 math.
 */

#pragma once

/* ===================== LEFT half (global 0..29) ===================== */
#define PIXEL_LH_C6R1 26
#define PIXEL_LH_C5R1 22
#define PIXEL_LH_C4R1 17
#define PIXEL_LH_C3R1 12
#define PIXEL_LH_C2R1 7
#define PIXEL_LH_C1R1 3
#define PIXEL_LH_C6R2 27
#define PIXEL_LH_C5R2 23
#define PIXEL_LH_C4R2 18
#define PIXEL_LH_C3R2 13
#define PIXEL_LH_C2R2 8
#define PIXEL_LH_C1R2 4
#define PIXEL_LH_C6R3 28
#define PIXEL_LH_C5R3 24
#define PIXEL_LH_C4R3 19
#define PIXEL_LH_C3R3 14
#define PIXEL_LH_C2R3 9
#define PIXEL_LH_C1R3 5
#define PIXEL_LH_C6R4 29
#define PIXEL_LH_C5R4 25
#define PIXEL_LH_C4R4 20
#define PIXEL_LH_C3R4 15
#define PIXEL_LH_C2R4 10
#define PIXEL_LH_C1R4 6
/*
 * LH bottom-region LED names CORRECTED against the go60_lh.dts pixel-lookup
 * table (verified: the same decode reproduces the known-correct RH names).
 * Previously LEDs 11/16/21 were mislabeled T1/T2/T3, and the real LH thumb
 * LEDs (0/1/2) were unnamed. LED 11 physically lights key LH_C2R5 (Backspace),
 * NOT the thumb. See go60_lh.dts LED map / pixel-lookup for the source of truth.
 */
#define PIXEL_LH_T1    0    /* real left thumb (was unnamed) */
#define PIXEL_LH_T2    1    /* real left thumb (was unnamed) */
#define PIXEL_LH_T3    2    /* real left thumb (was unnamed) */
#define PIXEL_LH_C2R5  11   /* inner bottom row (was mislabeled PIXEL_LH_T1) */
#define PIXEL_LH_C3R5  16   /* inner bottom row (was mislabeled PIXEL_LH_T2) */
#define PIXEL_LH_C4R5  21   /* inner bottom row (was mislabeled PIXEL_LH_T3) */

/* ===================== RIGHT half (global 30..59) ===================== */
#define PIXEL_RH_C6R1 56
#define PIXEL_RH_C5R1 52
#define PIXEL_RH_C4R1 47
#define PIXEL_RH_C3R1 42
#define PIXEL_RH_C2R1 37
#define PIXEL_RH_C1R1 33
#define PIXEL_RH_C6R2 57
#define PIXEL_RH_C5R2 53
#define PIXEL_RH_C4R2 48
#define PIXEL_RH_C3R2 43
#define PIXEL_RH_C2R2 38
#define PIXEL_RH_C1R2 34
#define PIXEL_RH_C6R3 58
#define PIXEL_RH_C5R3 54
#define PIXEL_RH_C4R3 49
#define PIXEL_RH_C3R3 44
#define PIXEL_RH_C2R3 39
#define PIXEL_RH_C1R3 35
#define PIXEL_RH_C6R4 59
#define PIXEL_RH_C5R4 55
#define PIXEL_RH_C4R4 50
#define PIXEL_RH_C3R4 45
#define PIXEL_RH_C2R4 40
#define PIXEL_RH_C1R4 36
#define PIXEL_RH_C4R5 51
#define PIXEL_RH_C3R5 46
#define PIXEL_RH_C2R5 41
#define PIXEL_RH_T1   30
#define PIXEL_RH_T2   31
#define PIXEL_RH_T3   32

/* ===================== Colors for &pixel =====================
 * IMPORTANT: the keymap's RED_RGB / RED / etc. are #define'd only INSIDE the
 * per-key-RGB section and #undef'd at its end. Anywhere after that (including
 * the layer/keycode listener block) they are UNDEFINED. These PX_* names live
 * here in go60-pixels.h (included at the top, never undef'd), so they are
 * always in scope for &pixel. Names are distinct from the keymap's, so there is
 * no redefinition conflict. You can also just pass a raw 0xRRGGBB literal.
 */
#define PX_RED    0xFF0000
#define PX_ORANGE 0xFF8000
#define PX_YELLOW 0xFFFF00
#define PX_GREEN  0x00FF00
#define PX_CYAN   0x00FFFF
#define PX_BLUE   0x0000FF
#define PX_PURPLE 0x7A00FF
#define PX_MAGENTA 0xFF00FF
#define PX_PINK   0xFF80BF
#define PX_WHITE  0xFFFFFF
#define PX_OFF    0x000000
/* clear (remove override entirely) is the literal (-1), not a color */

/* ===================== Group clear (one binding) =====================
 * Clear all custom &pixel colors on one half or both, in a single binding.
 * The color parameter is ignored for these (pass 0). These values mirror the
 * ZMK_PIXEL_CLEAR_* sentinels in the firmware and must stay in sync with them.
 *
 *   &pixel PIXEL_CLEAR_LEFT  0    // clear all LEFT-half custom colors
 *   &pixel PIXEL_CLEAR_RIGHT 0    // clear all RIGHT-half custom colors
 *   &pixel PIXEL_CLEAR_ALL   0    // clear both halves
 */
#define PIXEL_CLEAR_LEFT  250
#define PIXEL_CLEAR_RIGHT 251
#define PIXEL_CLEAR_ALL   252
