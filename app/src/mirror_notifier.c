/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Fork patch: mirror mode's host notifier (PATCH_MIRROR_MODE.md §6). Sends the mirror state
 * to the host overlay (LayerOverlay_PEEK) as a 0xF4 Raw HID packet through zmk-raw-hid.
 * Wire format: MIRROR_PACKET_0xF4.md.
 *
 *   [0] 0xF4  [1] 6 (payload length)  [2] flags: bit0 = one-shot armed
 *   [3] overlay-id of the instance that is on, 0 = none  [4..7] its layers, LSB first
 *
 * Sent after every mirror change, and after every layer change (forced, so an overlay
 * started while a mirror is on catches up; Raw HID receive is off, so the host can't ask).
 * Settled 8 ms and deduplicated, like the modifier notifier's 0xF2.
 *
 * Threading: the listener and the settle work both run on the system work queue; raw-hid
 * copies hid_buf before raise_raw_hid_sent_event() returns.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <raw_hid/events.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/mirror_mode_changed.h>
#include <zmk/mirror_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MIRROR_NOTIFIER_SETTLE_MS 8

// --- mirror packet layout: copied verbatim by app/tests-host/mirror-notifier/run.sh ---
#define MIRROR_NOTIFIER_MARKER 0xF4
#define MIRROR_NOTIFIER_LEN 6
#define MIRROR_NOTIFIER_FLAG_ONESHOT 0x01

/* Payload bytes [2..7] of the packet. */
static void mirror_notifier_fill(uint8_t payload[MIRROR_NOTIFIER_LEN], bool oneshot,
                                 uint8_t overlay_id, uint32_t layers) {
    payload[0] = oneshot ? MIRROR_NOTIFIER_FLAG_ONESHOT : 0;
    payload[1] = overlay_id;
    payload[2] = (uint8_t)(layers & 0xFF);
    payload[3] = (uint8_t)((layers >> 8) & 0xFF);
    payload[4] = (uint8_t)((layers >> 16) & 0xFF);
    payload[5] = (uint8_t)((layers >> 24) & 0xFF);
}

/* The whole report: marker, length, payload, zero padding. */
static void mirror_notifier_packet(uint8_t *buf, size_t size,
                                   const uint8_t payload[MIRROR_NOTIFIER_LEN]) {
    memset(buf, 0, size);
    buf[0] = MIRROR_NOTIFIER_MARKER;
    buf[1] = MIRROR_NOTIFIER_LEN;
    memcpy(&buf[2], payload, MIRROR_NOTIFIER_LEN);
}
// --- end mirror packet layout ---

BUILD_ASSERT(CONFIG_RAW_HID_REPORT_SIZE >= 2 + MIRROR_NOTIFIER_LEN,
             "Raw HID report too small for the 0xF4 packet");

static uint8_t hid_buf[CONFIG_RAW_HID_REPORT_SIZE];
static uint8_t last_payload[MIRROR_NOTIFIER_LEN];
static bool force_send = true;

static void mirror_notifier_settled(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t payload[MIRROR_NOTIFIER_LEN];
    uint8_t id = zmk_mirror_mode_active_overlay_id();
    /* An instance with overlay-id 0 isn't reported: the host sees "no mirror". */
    mirror_notifier_fill(payload, id != 0 && zmk_mirror_mode_oneshot_armed(), id,
                         id != 0 ? zmk_mirror_mode_active_layers() : 0);

    if (!force_send && memcmp(payload, last_payload, sizeof(payload)) == 0) {
        return;
    }
    force_send = false;
    memcpy(last_payload, payload, sizeof(payload));

    mirror_notifier_packet(hid_buf, sizeof(hid_buf), payload);
    LOG_DBG("0xF4: flags %02X id %d layers 0x%08X", payload[0], payload[1],
            zmk_mirror_mode_active_layers());
    raise_raw_hid_sent_event(
        (struct raw_hid_sent_event){.data = hid_buf, .length = sizeof(hid_buf)});
}

static K_WORK_DELAYABLE_DEFINE(mirror_notifier_work, mirror_notifier_settled);

static int mirror_notifier_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) != NULL) {
        force_send = true;
    }
    k_work_reschedule(&mirror_notifier_work, K_MSEC(MIRROR_NOTIFIER_SETTLE_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(mirror_notifier, mirror_notifier_listener);
ZMK_SUBSCRIPTION(mirror_notifier, zmk_mirror_mode_changed);
ZMK_SUBSCRIPTION(mirror_notifier, zmk_layer_state_changed);
