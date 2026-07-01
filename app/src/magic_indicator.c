/*
 * magic_indicator.c
 *
 * Central-side registry + live repaint for relocatable BLE/USB indicators.
 * See magic_indicator.h and MAGIC_INDICATOR_DESIGN.md.
 *
 * Color ladders MIRROR the native Magic-layer indicator logic in
 * rgb_underglow.c. The packed 0xRRGGBB values are identical:
 *     white      0xffffff   (connected AND active endpoint)
 *     dull_green 0x00ff68   (connected)
 *     red        0xff0000   (paired / powered-only)
 *     lilac      0x6b1fce   (unused / disconnected)
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/magic_indicator.h>
#include <zmk/rgb_underglow.h>
#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>

LOG_MODULE_REGISTER(magic_indicator, CONFIG_ZMK_LOG_LEVEL);

/*
 * BLE/USB/endpoint state is central-only: this TU is built for the central (or
 * a non-split board), and zmk_endpoints_selected() / zmk_ble_active_profile_index()
 * / zmk_ble_profile_status() DO NOT EXIST on a split peripheral. Same guard the
 * native rgb_underglow.c and behavior_pixel_color.c use. On a peripheral, color
 * resolution is never actually reached (indicators are driven by the central's
 * layer listener and pushed as resolved colors), so the peripheral build just
 * needs a stub that satisfies the linker.
 */
#define MI_HAS_STATE ((!IS_ENABLED(CONFIG_ZMK_SPLIT)) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

/* ---- native color ladder (packed 0xRRGGBB) ---- */
#define MI_WHITE      0xffffff
#define MI_DULL_GREEN 0x00ff68
#define MI_RED        0xff0000
#define MI_LILAC      0x6b1fce

/* ---- registry ---- */
#define MI_MAX 8
struct mi_entry {
    bool active;
    uint32_t position; /* global address (same space as &pixel) */
    uint8_t which;     /* MAGIC_BLE0..4 or MAGIC_USB */
};
static struct mi_entry mi_entries[MI_MAX];

/*
 * Resolve the current native color for a given source.
 * Returns packed 0xRRGGBB. Only meaningful on the central (state lives here).
 */
static uint32_t mi_resolve_color(uint8_t which) {
#if MI_HAS_STATE
    struct zmk_endpoint_instance active_endpoint = zmk_endpoints_selected();

    if (which == ZMK_MAGIC_INDICATOR_USB) {
        enum zmk_usb_conn_state usb_state = zmk_usb_get_conn_state();
        if (usb_state == ZMK_USB_CONN_HID &&
            active_endpoint.transport == ZMK_TRANSPORT_USB) {
            return MI_WHITE;
        } else if (usb_state == ZMK_USB_CONN_HID) {
            return MI_DULL_GREEN;
        } else if (usb_state == ZMK_USB_CONN_POWERED) {
            return MI_RED;
        }
        return MI_LILAC; /* ZMK_USB_CONN_NONE */
    }

#if IS_ENABLED(CONFIG_ZMK_BLE)
    /* BLE profile indicator. `which` is the profile index 0..4. */
    uint8_t i = which;
    int active_ble_profile_index = zmk_ble_active_profile_index();
    int8_t status = zmk_ble_profile_status(i);
    if (status == 2 && active_endpoint.transport == ZMK_TRANSPORT_BLE &&
        active_ble_profile_index == i) {
        return MI_WHITE;       /* connected AND active */
    } else if (status == 2) {
        return MI_DULL_GREEN;  /* connected */
    } else if (status == 1) {
        return MI_RED;         /* paired */
    }
    return MI_LILAC;           /* unused */
#else
    return MI_LILAC;
#endif

#else  /* !MI_HAS_STATE : split peripheral has no endpoint/BLE state APIs */
    ARG_UNUSED(which);
    return MI_LILAC;
#endif
}

/*
 * Apply a resolved color to a global pixel position. Routing copied from
 * behavior_pixel_color.c: local strip written directly, peripheral strip
 * pushed over the split. color < 0 clears.
 */
static void mi_apply(uint32_t position, int32_t color) {
    uint8_t local_count = zmk_rgb_underglow_pixel_count();

    if (position < local_count) {
        zmk_rgb_underglow_set_pixel(position, color);
    } else {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        uint8_t remote_pos = (uint8_t)(position - local_count);
        if (color < 0) {
            zmk_split_central_clear_pixel(remote_pos);
        } else {
            zmk_split_central_set_pixel(remote_pos, (uint32_t)color);
        }
#else
        LOG_WRN("magic indicator pos %u beyond local strip, no split central", position);
#endif
    }
}

static void mi_paint_entry(const struct mi_entry *e) {
    uint32_t color = mi_resolve_color(e->which);
    LOG_DBG("paint indicator pos %u which %u -> 0x%06X", e->position, e->which, color);
    mi_apply(e->position, (int32_t)color);
}

/* Repaint every active indicator (called on any relevant state change).
 * Only used by the state-change listener, which exists on central/non-split
 * only; guard to avoid an unused-function warning on a split peripheral. */
#if MI_HAS_STATE
static void mi_repaint_all(void) {
    for (int i = 0; i < MI_MAX; i++) {
        if (mi_entries[i].active) {
            mi_paint_entry(&mi_entries[i]);
        }
    }
}
#endif

int zmk_magic_indicator_on(uint32_t position, uint8_t which) {
    /* update existing, else take a free slot */
    int free_idx = -1;
    for (int i = 0; i < MI_MAX; i++) {
        if (mi_entries[i].active && mi_entries[i].position == position &&
            mi_entries[i].which == which) {
            mi_paint_entry(&mi_entries[i]);
            return 0;
        }
        if (!mi_entries[i].active && free_idx < 0) {
            free_idx = i;
        }
    }
    if (free_idx < 0) {
        LOG_WRN("magic indicator registry full (%d)", MI_MAX);
        return -ENOMEM;
    }
    mi_entries[free_idx].active = true;
    mi_entries[free_idx].position = position;
    mi_entries[free_idx].which = which;
    mi_paint_entry(&mi_entries[free_idx]);
    return 0;
}

int zmk_magic_indicator_off(uint32_t position, uint8_t which) {
    for (int i = 0; i < MI_MAX; i++) {
        if (mi_entries[i].active && mi_entries[i].position == position &&
            mi_entries[i].which == which) {
            mi_entries[i].active = false;
            mi_apply(position, -1); /* clear pixel */
            return 0;
        }
    }
    /* Not found: still clear the pixel defensively. */
    mi_apply(position, -1);
    return 0;
}

int zmk_magic_indicator_clear_all(void) {
    for (int i = 0; i < MI_MAX; i++) {
        if (mi_entries[i].active) {
            mi_apply(mi_entries[i].position, -1);
            mi_entries[i].active = false;
        }
    }
    return 0;
}

/* ---- live update: recompute colors on state changes ----
 * Only meaningful where the state APIs exist (central / non-split). On a split
 * peripheral there is nothing to subscribe to and the event symbols may not be
 * built, so compile the listener out entirely there.
 */
#if MI_HAS_STATE

static int mi_event_listener(const zmk_event_t *eh) {
    /* Any of these may change an indicator's color. Cheapest correct response
     * is to repaint all active indicators. */
    mi_repaint_all();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(magic_indicator, mi_event_listener);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(magic_indicator, zmk_ble_active_profile_changed);
#endif
/* endpoint_changed.c is always built; usb_conn_state_changed.c needs USB stack. */
ZMK_SUBSCRIPTION(magic_indicator, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(magic_indicator, zmk_usb_conn_state_changed);
#endif

#endif /* MI_HAS_STATE */
