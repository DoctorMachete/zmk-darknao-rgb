# PIXEL_MAGIC_INDICATOR_LAYER_v001

Follow-on to PIXEL_MAGIC_INDICATOR_DESIGN_v001 / _GROUP_v005.

## Problem
Magic indicator and `&pixel` override collided at the same LED (e.g. LH_C6R4):
both wrote the SAME `pixel_overrides[]` array in rgb_underglow.c (magic went
through `zmk_rgb_underglow_set_pixel`). Last-writer-wins, and `&pixel` was
composited "above everything", so toggling ACCENTS or firing
`&magic_indicator_clear` wiped whichever the other owned — nothing survived
underneath because there was only one storage cell.

## Fix (applied — files in this tree)
Gave magic indicators their OWN driver layer, composited ABOVE the `&pixel`
layer. Clearing the magic layer now reveals the `&pixel` pixel beneath.

Edited files:
- app/src/rgb_underglow.c
    * new arrays: magic_pixels[] / magic_pixel_active[] / any_magic_pixel
    * new API:    zmk_rgb_underglow_set_magic_pixel(pos, color)  (color<0 clears)
    * generate_status(): magic layer painted AFTER (above) pixel_overrides
    * overlay gates (persist_overlay + generate_status trigger) include
      any_magic_pixel so a lone indicator still composites (no fast-path escape)
    * clear_pixels(): also wipes the magic layer  [POLICY: remove 3 lines if
      &pixel CLEAR_ALL should leave indicators intact]
- app/include/zmk/rgb_underglow.h
    * declare zmk_rgb_underglow_set_magic_pixel
- app/src/magic_indicator.c
    * mi_apply() local-strip branch now calls set_magic_pixel (was set_pixel);
      split branch UNCHANGED

Priority order now (low -> high): underglow/layer RGB -> battery/USB persist
indicators -> &pixel overrides -> MAGIC indicators.

## Behavior for the reported case (LH_C6R4, local strip)
NAV enter -> indicator paints (magic layer). ACCENTS on -> &pixel pink stored in
override layer but indicator still on top (indicator prevails). ACCENTS off ->
override cleared, indicator unaffected. Leave NAV -> &magic_indicator_clear
clears magic layer -> if pink still active it REAPPEARS, else falls back to RGB.

## Split / RH caveat (NOT fixed — out of scope)
Two-layer model lives in the CENTRAL driver only. The split pixel channel
(zmk_split_central_set_pixel) carries a color, not a layer id, so a RH indicator
colliding with a RH &pixel would still last-writer-wins on the peripheral. All
current indicators are LH, so unreached. Keep indicators LH-only until the split
protocol can distinguish layers.

## Build status
NOT compiled in a Zephyr tree here. Verified: whole-driver brace balance (235/235);
new setter + both composite blocks pass isolated `gcc -fsyntax-only -Wall -Wextra`;
symbol declared/defined/called consistently; old direct set_pixel call removed
from magic_indicator local branch. Risk area if it trips: the GNU designated-init
`(struct led_rgb){r : ...}` in the setter (matches existing set_pixel style — do
NOT rewrite to `.r =`). Capture any error.
