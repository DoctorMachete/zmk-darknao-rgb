/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch (PATCH_MIRROR_MODE.md): raised whenever a mirror instance turns on or off, or
 * the active one changes mode (a one-shot made permanent by MIRROR_ON). Switching from A
 * to B raises A off, then B on.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_mirror_mode_changed {
    uint8_t instance;   // devicetree instance number of the zmk,behavior-mirror node
    uint8_t overlay_id; // its `overlay-id` (0 = not reported to the host)
    bool active;
    bool oneshot; // on as a one-shot that hasn't been used yet
    int64_t timestamp;
};

ZMK_EVENT_DECLARE(zmk_mirror_mode_changed);
