/*
 * Custom OLED status screen for Sofle keyboard (ZMK v0.3, LVGL 8)
 *
 * Central (left half) — 128x32 layout:
 *   y= 0  [Layer Name         ] [Output]
 *   y=10  [===Battery Bar====] [BAT%]
 *   y=22  [WPM NNN           ]
 *
 * Peripheral (right half) — 128x32 layout:
 *   y= 0  SOFLE
 *   y=10  [===Battery Bar====] [BAT%]
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
 * SHARED: Battery status (both halves show their own battery level)
 * ================================================================ */

static lv_obj_t *battery_label;
static lv_obj_t *battery_bar;

struct battery_status_state {
    uint8_t level;
};

static void battery_update_cb(struct battery_status_state state) {
    lv_label_set_text_fmt(battery_label, "%3d%%", state.level);
    lv_bar_set_value(battery_bar, state.level, LV_ANIM_OFF);
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
 * Helpers: create styled widgets for monochrome OLED
 * ================================================================ */

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (w > 0) {
        lv_obj_set_width(label, w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    }
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_battery_bar(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    /* Outer container: black fill, white 1px border, no rounding */
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 1, LV_PART_MAIN);

    /* Indicator: solid white fill, no rounding */
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);

    return bar;
}

/* ================================================================
 * Status screen entry point — called once by ZMK display init
 * ================================================================ */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Clean black OLED background */
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /*
     * Central layout:
     *   x=  0, y= 0, w=80  Layer name
     *   x= 82, y= 0, w=46  Output / BT status
     *   x=  0, y=10, w=94  Battery bar (h=8)
     *   x= 97, y=10, w=31  Battery percentage
     *   x=  0, y=22, w=128 WPM counter
     */
    layer_label   = make_label(screen,  0,  0, 80, "...");
    output_label  = make_label(screen, 82,  0, 46, "...");
    battery_bar   = make_battery_bar(screen, 0, 10, 94, 8);
    battery_label = make_label(screen, 97, 10, 31, "---");
#if IS_ENABLED(CONFIG_ZMK_WPM)
    wpm_label     = make_label(screen,  0, 22, 128, "WPM   0");
#endif

#else
    /*
     * Peripheral layout:
     *   x=  0, y= 0, w=128  Title
     *   x=  0, y=10, w=94   Battery bar (h=8)
     *   x= 97, y=10, w=31   Battery percentage
     */
    make_label(screen, 0, 0, 128, "SOFLE");
    battery_bar   = make_battery_bar(screen, 0, 10, 94, 8);
    battery_label = make_label(screen, 97, 10, 31, "---");
#endif

    return screen;
}
