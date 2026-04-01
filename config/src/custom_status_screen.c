/*
 * Custom OLED status screen for Sofle keyboard (ZMK v0.3, LVGL 8)
 *
 * Central (left half) — 128x32 layout:
 *   y= 0  [Layer Name             ] [BT/USB]
 *   y=10  BAT [=========         ]  75%
 *   y=22  WPM  047
 *
 * Peripheral (right half) — 128x32 layout:
 *   y= 0  SOFLE
 *   y=12  BAT [=========         ]  75%
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery.h>
#include <lvgl.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif
#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */

/* ================================================================
 * SHARED: Battery status (both halves show their own level)
 * ================================================================ */

static lv_obj_t *battery_label;

static void set_battery_text(lv_obj_t *label, uint8_t level) {
    char bar[11];
    int filled = (level * 10 + 50) / 100; /* 0–10, rounded */
    for (int i = 0; i < 10; i++) {
        bar[i] = (i < filled) ? '=' : ' ';
    }
    bar[10] = '\0';
    lv_label_set_text_fmt(label, "BAT[%s]%3d%%", bar, level);
}

struct battery_status_state {
    uint8_t level;
};

static void battery_update_cb(struct battery_status_state state) {
    if (!battery_label) return;
    set_battery_text(battery_label, state.level);
}

static struct battery_status_state battery_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){.level = zmk_battery_state_of_charge()};
}

ZMK_DISPLAY_WIDGET_LISTENER(custom_battery, struct battery_status_state,
                             battery_update_cb, battery_get_state)
ZMK_SUBSCRIPTION(custom_battery, zmk_battery_state_changed);

/* ================================================================
 * CENTRAL ONLY: Layer, output, and WPM widgets
 * ================================================================ */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* -- Layer Status -- */

static lv_obj_t *layer_label;

struct layer_status_state {
    uint8_t index;
    const char *name;
};

static void layer_update_cb(struct layer_status_state state) {
    if (!layer_label) return;
    if (state.name) {
        lv_label_set_text(layer_label, state.name);
    } else {
        lv_label_set_text_fmt(layer_label, "L%d", state.index);
    }
}

static struct layer_status_state layer_get_state(const zmk_event_t *eh) {
    uint8_t idx = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = idx,
        .name  = zmk_keymap_layer_name(idx),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(custom_layer, struct layer_status_state,
                             layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(custom_layer, zmk_layer_state_changed);

/* -- Output / Connection Status -- */

static lv_obj_t *output_label;

struct output_status_state {
    struct zmk_endpoint_instance endpoint;
    bool connected;
};

static void output_update_cb(struct output_status_state state) {
    if (!output_label) return;
    switch (state.endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        lv_label_set_text(output_label, "USB");
        break;
    case ZMK_TRANSPORT_BLE:
        lv_label_set_text_fmt(output_label,
            state.connected ? "BT%d+" : "BT%d-",
            state.endpoint.data.ble.profile_index + 1);
        break;
    default:
        lv_label_set_text(output_label, "---");
    }
}

static struct output_status_state output_get_state(const zmk_event_t *eh) {
    struct zmk_endpoint_instance ep = zmk_endpoint_get_selected();
    bool connected = (ep.transport == ZMK_TRANSPORT_USB);
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (ep.transport == ZMK_TRANSPORT_BLE) {
        connected = zmk_ble_active_profile_is_connected();
    }
#endif
    return (struct output_status_state){.endpoint = ep, .connected = connected};
}

ZMK_DISPLAY_WIDGET_LISTENER(custom_output, struct output_status_state,
                             output_update_cb, output_get_state)
ZMK_SUBSCRIPTION(custom_output, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(custom_output, zmk_ble_active_profile_changed);
#endif

/* -- WPM Status -- */

#if IS_ENABLED(CONFIG_ZMK_WPM)

static lv_obj_t *wpm_label;

struct wpm_status_state {
    int wpm;
};

static void wpm_update_cb(struct wpm_status_state state) {
    if (!wpm_label) return;
    lv_label_set_text_fmt(wpm_label, "WPM %3d", state.wpm);
}

static struct wpm_status_state wpm_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(custom_wpm, struct wpm_status_state,
                             wpm_update_cb, wpm_get_state)
ZMK_SUBSCRIPTION(custom_wpm, zmk_wpm_state_changed);

#endif /* CONFIG_ZMK_WPM */
#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */

/* ================================================================
 * Status screen entry point — called once by ZMK display init
 * ================================================================ */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /*
     * Central layout (positions in pixels, 8px font assumed):
     *   y= 0  layer name (left) | output status (right)
     *   y=10  battery text bar
     *   y=22  WPM counter
     */
    layer_label = lv_label_create(screen);
    lv_obj_set_pos(layer_label, 0, 0);
    lv_label_set_text(layer_label, "");

    output_label = lv_label_create(screen);
    lv_obj_set_pos(output_label, 96, 0);
    lv_label_set_text(output_label, "");

    battery_label = lv_label_create(screen);
    lv_obj_set_pos(battery_label, 0, 10);
    lv_label_set_text(battery_label, "BAT[          ]  --%");

#if IS_ENABLED(CONFIG_ZMK_WPM)
    wpm_label = lv_label_create(screen);
    lv_obj_set_pos(wpm_label, 0, 22);
    lv_label_set_text(wpm_label, "WPM   0");
#endif

#else
    /*
     * Peripheral layout:
     *   y= 0  title
     *   y=12  battery text bar
     */
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_pos(title, 0, 0);
    lv_label_set_text(title, "SOFLE");

    battery_label = lv_label_create(screen);
    lv_obj_set_pos(battery_label, 0, 12);
    lv_label_set_text(battery_label, "BAT[          ]  --%");
#endif

    return screen;
}
