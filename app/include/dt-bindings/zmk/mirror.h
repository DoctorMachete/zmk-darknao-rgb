/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: mirror mode, zmk,behavior-mirror (PATCH_MIRROR_MODE.md).
 */

#pragma once

/* The binding's one parameter: &mirror_x MIRROR_TOG */
#define MIRROR_TOG 0     /* toggle this instance */
#define MIRROR_ON 1      /* on (switches off any other instance) */
#define MIRROR_OFF 2     /* off, if this instance is the one on */
#define MIRROR_MO 3      /* on while the key is held */
#define MIRROR_ONESHOT 4 /* on for the next key that actually gets mirrored */

/*
 * GO60 maps for `mirror-positions` (and `combo-positions`). Entry p = the key that pressing
 * key p acts as.
 * Key numbers: rows 1-4 are 0-11, 12-23, 24-35, 36-47; row 5 is 48-53
 * (LH_C4R5 LH_C3R5 LH_C2R5 RH_C2R5 RH_C3R5 RH_C4R5); thumbs 54-59
 * (LH_T1 LH_T2 LH_T3 RH_T3 RH_T2 RH_T1).
 *
 * MIRROR_MAP_GO60 is also the built-in default on GO60 when `mirror-positions` is omitted.
 */
#define MIRROR_MAP_GO60                                                                            \
    11 10 9 8 7 6 5 4 3 2 1 0                                                                      \
    23 22 21 20 19 18 17 16 15 14 13 12                                                            \
    35 34 33 32 31 30 29 28 27 26 25 24                                                            \
    47 46 45 44 43 42 41 40 39 38 37 36                                                            \
    53 52 51 50 49 48                                                                              \
    59 58 57 56 55 54

/*
 * Same, but row 1 (F-row) swaps halves in order instead of reflecting: F1 stays leftmost.
 * Each half keeps its order, so a left-half layout like `11 10 8 9 7 6` is NOT this map:
 * write such maps out in full.
 */
#define MIRROR_MAP_GO60_FROW_SWAP                                                                  \
    6 7 8 9 10 11 0 1 2 3 4 5                                                                      \
    23 22 21 20 19 18 17 16 15 14 13 12                                                            \
    35 34 33 32 31 30 29 28 27 26 25 24                                                            \
    47 46 45 44 43 42 41 40 39 38 37 36                                                            \
    53 52 51 50 49 48                                                                              \
    59 58 57 56 55 54
