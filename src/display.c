#define _GNU_SOURCE
#include "display.h"
#include "compose.h"
#include "i18n.h"
#include "cursor-shape-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <errno.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>
#include "tunables.h"
#include "length.h"



double output_px_per_mm(const Output *out) {
    return pac_px_per_mm(out->physical_mm, out->mode_px, out->scale);
}

static void set_scale(Output *out, double scale) {
    if (scale < 1) scale = 1;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    if (scale == out->scale) return;
    out->scale = scale;
    out->dpy->dirty = true;
}

/* libwayland aborts when an event reaches a listener entry that is NULL, so every event of an
 * interface needs a function, even the ones that are ignored. */

static void fractional_preferred_scale(void *data, struct wp_fractional_scale_v1 *fs,
                                       uint32_t scale) {
    (void)fs;
    set_scale(data, scale / 120.0);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_preferred_scale,
};

/* Compositors without fractional scaling still report an integer one. */
static void surface_preferred_buffer_scale(void *data, struct wl_surface *surface,
                                           int32_t factor) {
    Output *out = data;
    (void)surface;
    if (!out->fractional_scale) set_scale(out, factor);
}

static void surface_enter(void *data, struct wl_surface *s, struct wl_output *o) {
    (void)data; (void)s; (void)o;
}

static void surface_leave(void *data, struct wl_surface *s, struct wl_output *o) {
    (void)data; (void)s; (void)o;
}

static void surface_preferred_buffer_transform(void *data, struct wl_surface *s, uint32_t t) {
    (void)data; (void)s; (void)t;
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
    .preferred_buffer_scale = surface_preferred_buffer_scale,
    .preferred_buffer_transform = surface_preferred_buffer_transform,
};

/* ---- keyboard ---- */

static void repeat_stop(Display *dpy) {
    struct itimerspec off = {0};
    timerfd_settime(dpy->repeat_fd, 0, &off, NULL);
    dpy->repeat_key = 0;
    /* The held key may have typed a password character: do not keep it. */
    explicit_bzero(&dpy->repeat_event, sizeof(dpy->repeat_event));
}

static void repeat_start(Display *dpy, uint32_t key, const KeyEvent *event) {
    if (dpy->repeat_rate <= 0) return;
    long interval_ns = 1000000000L / dpy->repeat_rate;
    struct itimerspec spec = {
        .it_value = { dpy->repeat_delay / 1000, (dpy->repeat_delay % 1000) * 1000000L },
        .it_interval = { interval_ns / 1000000000L, interval_ns % 1000000000L },
    };
    dpy->repeat_key = key;
    dpy->repeat_event = *event;
    timerfd_settime(dpy->repeat_fd, 0, &spec, NULL);
}

/* The modifier bits mean the same in every keymap, the group only in the compositor's. */
static uint32_t effective_group(const Display *dpy) {
    if (dpy->custom_layout) return 0;
    return dpy->forced_group >= 0 ? (uint32_t)dpy->forced_group : dpy->group;
}

/* Makes keymap (a reference is taken over) the one keys are read with. */
static void use_keymap(Display *dpy, struct xkb_keymap *keymap) {
    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }
    xkb_state_update_mask(state, dpy->mods_depressed, dpy->mods_latched, dpy->mods_locked,
                          0, 0, effective_group(dpy));
    if (dpy->xkb_state) xkb_state_unref(dpy->xkb_state);
    if (dpy->keymap) xkb_keymap_unref(dpy->keymap);
    dpy->keymap = keymap;
    dpy->xkb_state = state;
    repeat_stop(dpy);

    dpy->caps_lock = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                                  XKB_STATE_MODS_LOCKED) > 0;
    dpy->dirty = true;
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int fd, uint32_t size) {
    Display *dpy = data;
    (void)keyboard;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }

    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    struct xkb_keymap *keymap = xkb_keymap_new_from_string(dpy->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;

    if (dpy->sys_keymap) xkb_keymap_unref(dpy->sys_keymap);
    dpy->sys_keymap = keymap;
    /* What was chosen here stays chosen: the line under the password still says so. A group the
     * new keymap does not have goes back to the compositor's. */
    if (dpy->forced_group >= display_group_count(dpy)) dpy->forced_group = -1;
    if (!dpy->custom_layout) use_keymap(dpy, xkb_keymap_ref(keymap));
}

int display_group_count(const Display *dpy) {
    return dpy->sys_keymap ? (int)xkb_keymap_num_layouts(dpy->sys_keymap) : 0;
}

#ifndef NO_LAYOUTS
int display_current_group(const Display *dpy) {
    int n = display_group_count(dpy);
    return n > 0 && dpy->group < (uint32_t)n ? (int)dpy->group : 0;
}

const char *display_group_name(const Display *dpy, int group) {
    if (!dpy->sys_keymap || group < 0 || group >= display_group_count(dpy)) return NULL;
    const char *name = xkb_keymap_layout_get_name(dpy->sys_keymap, group);
    return name && *name ? name : NULL;
}

void display_use_group(Display *dpy, int group) {
    dpy->custom_layout = false;
    dpy->forced_group = group < display_group_count(dpy) ? group : -1;
    if (dpy->sys_keymap) use_keymap(dpy, xkb_keymap_ref(dpy->sys_keymap));
}

bool display_set_layout(Display *dpy, const char *layout, const char *variant) {
    /* Model and options (caps:escape and the like) stay the ones the environment sets. */
    struct xkb_rule_names names = { .layout = layout, .variant = variant && *variant ? variant : NULL };
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(dpy->xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) return false;
    dpy->custom_layout = true;
    use_keymap(dpy, keymap);
    return true;
}
#endif /* NO_LAYOUTS */

bool display_key_text(Display *dpy, uint32_t key, uint32_t mods, char *out, size_t size) {
    if (!dpy->keymap) return false;
    struct xkb_state *state = xkb_state_new(dpy->keymap);
    if (!state) return false;
    xkb_state_update_mask(state, mods, 0, 0, 0, 0, effective_group(dpy));
    int n = xkb_state_key_get_utf8(state, key + 8, out, size);
    xkb_state_unref(state);
    return n > 0 && (size_t)n < size;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    (void)keyboard; (void)serial; (void)surface;
    repeat_stop(data);
}

static void setup_compose(Display *dpy);

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    Display *dpy = data;
    (void)keyboard; (void)serial; (void)time;
    if (!dpy->xkb_state) return;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (key == dpy->repeat_key) repeat_stop(dpy);
        return;
    }

    xkb_keycode_t code = key + 8;   /* evdev to XKB */
    xkb_keysym_t sym = xkb_state_key_get_one_sym(dpy->xkb_state, code);
    KeyEvent event = { .keysym = sym, .key = key,
                       .mods = dpy->mods_depressed | dpy->mods_latched | dpy->mods_locked };
    bool composed = false;

    /* Dead keys and Compose sequences ("~" then "a" types "ã") */
    if (!dpy->compose_tried && dpy->xkb && compose_may_start(sym)) {
        dpy->compose_tried = true;
        setup_compose(dpy);
    }
    if (dpy->compose_state) {
        xkb_compose_state_feed(dpy->compose_state, sym);
        switch (xkb_compose_state_get_status(dpy->compose_state)) {
        case XKB_COMPOSE_COMPOSING:
            return;
        case XKB_COMPOSE_CANCELLED:
            xkb_compose_state_reset(dpy->compose_state);
            return;
        case XKB_COMPOSE_COMPOSED:
            event.keysym = xkb_compose_state_get_one_sym(dpy->compose_state);
            xkb_compose_state_get_utf8(dpy->compose_state, event.text, sizeof(event.text));
            xkb_compose_state_reset(dpy->compose_state);
            composed = true;
            event.key = 0;
            break;
        case XKB_COMPOSE_NOTHING:
            break;
        }
    }
    if (!composed) xkb_state_key_get_utf8(dpy->xkb_state, code, event.text, sizeof(event.text));

    struct xkb_state *xs = dpy->xkb_state;
    event.ctrl = xkb_state_mod_name_is_active(xs, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    event.alt = xkb_state_mod_name_is_active(xs, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
    event.shift = xkb_state_mod_name_is_active(xs, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    event.logo = xkb_state_mod_name_is_active(xs, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0;

    if (dpy->on_key) dpy->on_key(&event, dpy->on_key_data);
    if (!composed && xkb_keymap_key_repeats(dpy->keymap, code)) repeat_start(dpy, key, &event);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {
    Display *dpy = data;
    (void)keyboard; (void)serial;
    dpy->mods_depressed = depressed;
    dpy->mods_latched = latched;
    dpy->mods_locked = locked;
    dpy->group = group;
    if (!dpy->xkb_state) return;
    xkb_state_update_mask(dpy->xkb_state, depressed, latched, locked, 0, 0, effective_group(dpy));
    if (dpy->forced_group < 0 && !dpy->custom_layout) dpy->dirty = true;   /* the layout shown follows it */

    bool caps = xkb_state_mod_name_is_active(dpy->xkb_state, XKB_MOD_NAME_CAPS,
                                             XKB_STATE_MODS_LOCKED) > 0;
    if (caps != dpy->caps_lock) {
        dpy->caps_lock = caps;
        dpy->dirty = true;
    }
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay) {
    Display *dpy = data;
    (void)keyboard;
    dpy->repeat_rate = rate;
    dpy->repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* ---- pointer ---- */

static void set_cursor(Display *dpy, bool clickable) {
    dpy->pointer_clickable = clickable;
    if (!dpy->cursor_shape) return;
    wp_cursor_shape_device_v1_set_shape(dpy->cursor_shape, dpy->pointer_serial,
        clickable ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

/* What is under the pointer: asks the handler, which says whether it can be clicked. */
static bool pointer_over(Display *dpy, bool press) {
    return dpy->on_pointer && dpy->pointer_out &&
           dpy->on_pointer(dpy->pointer_out, dpy->pointer_x, dpy->pointer_y, press,
                           dpy->on_pointer_data);
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    Display *dpy = data;
    (void)pointer;
    dpy->pointer_serial = serial;
    dpy->pointer_out = NULL;
    for (Output *out = dpy->outputs; out; out = out->next)
        if (out->surface == surface) dpy->pointer_out = out;
    dpy->pointer_x = wl_fixed_to_double(x);
    dpy->pointer_y = wl_fixed_to_double(y);
    set_cursor(dpy, pointer_over(dpy, false));
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    Display *dpy = data;
    (void)pointer; (void)serial; (void)surface;
    dpy->pointer_out = NULL;
    /* Nothing is under a pointer that is gone. */
    if (dpy->on_pointer) dpy->on_pointer(NULL, -1, -1, false, dpy->on_pointer_data);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y) {
    Display *dpy = data;
    (void)pointer; (void)time;
    dpy->pointer_x = wl_fixed_to_double(x);
    dpy->pointer_y = wl_fixed_to_double(y);
    bool over = pointer_over(dpy, false);
    if (over != dpy->pointer_clickable) set_cursor(dpy, over);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    Display *dpy = data;
    (void)pointer; (void)serial; (void)time;
    if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    pointer_over(dpy, true);
    /* What is under the pointer may have changed. */
    set_cursor(dpy, pointer_over(dpy, false));
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v) {
    (void)data; (void)p; (void)t; (void)axis; (void)v;
}
static void pointer_frame(void *data, struct wl_pointer *p) { (void)data; (void)p; }
static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t s) {
    (void)data; (void)p; (void)s;
}
static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis) {
    (void)data; (void)p; (void)t; (void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t d) {
    (void)data; (void)p; (void)axis; (void)d;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    Display *dpy = data;
    bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;

    if (has_keyboard && !dpy->keyboard) {
        dpy->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(dpy->keyboard, &keyboard_listener, dpy);
    } else if (!has_keyboard && dpy->keyboard) {
        repeat_stop(dpy);                    /* no key-up will come for a key that is held */
        wl_keyboard_release(dpy->keyboard);
        dpy->keyboard = NULL;
    }

    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !dpy->pointer) {
        dpy->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(dpy->pointer, &pointer_listener, dpy);
        if (dpy->cursor_shape_manager)
            dpy->cursor_shape = wp_cursor_shape_manager_v1_get_pointer(dpy->cursor_shape_manager,
                                                                       dpy->pointer);
    } else if (!has_pointer && dpy->pointer) {
        if (dpy->cursor_shape) wp_cursor_shape_device_v1_destroy(dpy->cursor_shape);
        wl_pointer_release(dpy->pointer);
        dpy->cursor_shape = NULL;
        dpy->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ---- outputs ---- */

/* The compositor has said how big the screen is: cover it with the background. */
static void output_configured(Output *out, uint32_t width, uint32_t height) {
    Display *dpy = out->dpy;
    out->width = width > 0 ? width : 1280;
    out->height = height > 0 ? height : 720;

    wp_viewport_set_destination(out->viewport, out->width, out->height);
    wl_surface_attach(out->surface, dpy->background, 0, 0);
    wl_surface_damage_buffer(out->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(out->surface);

    out->configured = true;
    dpy->dirty = true;
}

static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *surface,
                                   uint32_t serial, uint32_t width, uint32_t height) {
    ext_session_lock_surface_v1_ack_configure(surface, serial);
    output_configured(data, width, height);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height, struct wl_array *states) {
    Output *out = data;
    (void)toplevel; (void)states;
    /* 0 means "your choice". */
    out->width = width > 0 ? width : 1280;
    out->height = height > 0 ? height : 720;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    ((Output *)data)->dpy->running = false;
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h) {
    (void)data; (void)t; (void)w; (void)h;
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *c) {
    (void)data; (void)t; (void)c;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities = toplevel_wm_capabilities,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    Output *out = data;
    xdg_surface_ack_configure(surface, serial);
    output_configured(out, out->width, out->height);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void panel_init(Panel *panel, Display *dpy, struct wl_surface *parent);
static void panel_finish(Panel *panel);

/* Gives the output its surface: a lock surface, or in a demo a fullscreen window. */
static void output_open(Output *out) {
    Display *dpy = out->dpy;
    /* wl_output.scale arrives before the surface exists for the outputs of the first roundtrips:
     * apply it now, unless the surface's fractional scale will say it. */
    if (!dpy->fractional_scale_manager && wl_compositor_get_version(dpy->compositor) < 6)
        set_scale(out, out->wl_scale);
    out->surface = wl_compositor_create_surface(dpy->compositor);
    wl_surface_add_listener(out->surface, &surface_listener, out);
    if (dpy->fractional_scale_manager) {
        out->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            dpy->fractional_scale_manager, out->surface);
        wp_fractional_scale_v1_add_listener(out->fractional_scale, &fractional_scale_listener, out);
    }
    out->viewport = wp_viewporter_get_viewport(dpy->viewporter, out->surface);

    if (dpy->demo) {
        out->xdg_surface = xdg_wm_base_get_xdg_surface(dpy->wm_base, out->surface);
        xdg_surface_add_listener(out->xdg_surface, &xdg_surface_listener, out);
        out->toplevel = xdg_surface_get_toplevel(out->xdg_surface);
        xdg_toplevel_add_listener(out->toplevel, &toplevel_listener, out);
        xdg_toplevel_set_title(out->toplevel, "paclock");
        xdg_toplevel_set_app_id(out->toplevel, "paclock");
        xdg_toplevel_set_fullscreen(out->toplevel, NULL);
    } else {
        out->lock_surface = ext_session_lock_v1_get_lock_surface(dpy->lock, out->surface,
                                                                  out->wl_output);
        ext_session_lock_surface_v1_add_listener(out->lock_surface, &lock_surface_listener, out);
    }

    panel_init(&out->card, dpy, out->surface);
    /* A window needs this empty commit to get its first configure. A lock surface must not
     * have it: it already gets its configure, and committing before a buffer is attached is a
     * protocol error that kills the client, leaving the screen locked with nothing to unlock it. */
    if (dpy->demo) wl_surface_commit(out->surface);
}

static void output_close(Output *out) {
    if (out->dpy->pointer_out == out) out->dpy->pointer_out = NULL;
    panel_finish(&out->card);
    if (out->toplevel) xdg_toplevel_destroy(out->toplevel);
    if (out->xdg_surface) xdg_surface_destroy(out->xdg_surface);
    if (out->lock_surface) ext_session_lock_surface_v1_destroy(out->lock_surface);
    if (out->fractional_scale) wp_fractional_scale_v1_destroy(out->fractional_scale);
    if (out->viewport) wp_viewport_destroy(out->viewport);
    if (out->surface) wl_surface_destroy(out->surface);
    if (out->wl_output) {
        /* release only exists from version 3 */
        if (wl_output_get_version(out->wl_output) >= 3) wl_output_release(out->wl_output);
        else wl_output_destroy(out->wl_output);
    }
    free(out);
}

/* Before wl_surface version 6 the integer scale is the output's. Every event needs a function. */
static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
                            int32_t ph, int32_t subpixel, const char *make, const char *model,
                            int32_t transform) {
    Output *out = data;
    (void)o; (void)x; (void)y; (void)ph; (void)subpixel; (void)make; (void)model; (void)transform;
    out->physical_mm = pw;
}
static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
                        int32_t refresh) {
    Output *out = data;
    (void)o; (void)h; (void)refresh;
    if (flags & WL_OUTPUT_MODE_CURRENT) out->mode_px = w;
}
/* The size and mode arrive together, after the surface may have been drawn: draw again. */
static void output_done(void *data, struct wl_output *o) {
    Output *out = data;
    (void)o;
    out->dpy->dirty = true;
}
static void output_scale(void *data, struct wl_output *o, int32_t factor) {
    Output *out = data;
    (void)o;
    out->wl_scale = factor;   /* kept for output_open, which may run after this */
    if (!out->fractional_scale && out->dpy->compositor && wl_compositor_get_version(out->dpy->compositor) < 6)
        set_scale(out, factor);
}
static void output_name(void *data, struct wl_output *o, const char *name) {
    (void)data; (void)o; (void)name;
}
static void output_description(void *data, struct wl_output *o, const char *description) {
    (void)data; (void)o; (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static Output *output_add(Display *dpy, struct wl_output *wl_output, uint32_t global) {
    Output *out = calloc(1, sizeof(Output));
    if (!out) return NULL;
    out->dpy = dpy;
    out->wl_output = wl_output;
    out->global = global;
    out->wl_scale = 1;
    out->scale = 1;
    if (wl_output) wl_output_add_listener(wl_output, &output_listener, out);   /* NULL in a demo */
    out->next = dpy->outputs;
    dpy->outputs = out;
    return out;
}

/* ---- the lock ---- */

static void lock_locked(void *data, struct ext_session_lock_v1 *lock) {
    (void)lock;
    ((Display *)data)->locked = true;
}

static void lock_finished(void *data, struct ext_session_lock_v1 *lock) {
    Display *dpy = data;
    (void)lock;
    dpy->lock_finished = true;
    dpy->running = false;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_locked,
    .finished = lock_finished,
};

/* ---- registry ---- */

#define BIND(iface, ver) wl_registry_bind(registry, name, &iface##_interface, \
                                          version < (ver) ? version : (ver))

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version) {
    Display *dpy = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        dpy->compositor = BIND(wl_compositor, 6);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        dpy->subcompositor = BIND(wl_subcompositor, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        dpy->shm = BIND(wl_shm, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        dpy->wm_base = BIND(xdg_wm_base, 2);
        xdg_wm_base_add_listener(dpy->wm_base, &wm_base_listener, dpy);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        dpy->viewporter = BIND(wp_viewporter, 1);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        dpy->fractional_scale_manager = BIND(wp_fractional_scale_manager_v1, 1);
    } else if (strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
        dpy->single_pixel = BIND(wp_single_pixel_buffer_manager_v1, 1);
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        dpy->lock_manager = BIND(ext_session_lock_manager_v1, 1);
    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        dpy->cursor_shape_manager = BIND(wp_cursor_shape_manager_v1, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0 && !dpy->seat) {
        dpy->seat = BIND(wl_seat, 5);
        wl_seat_add_listener(dpy->seat, &seat_listener, dpy);
    } else if (strcmp(interface, wl_output_interface.name) == 0 && !dpy->demo) {
        Output *out = output_add(dpy, BIND(wl_output, 3), name);
        /* A screen plugged in while locked is covered at once. */
        if (out && dpy->lock) output_open(out);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    Display *dpy = data;
    (void)registry;
    for (Output **link = &dpy->outputs; *link; link = &(*link)->next) {
        if ((*link)->global != name) continue;
        Output *gone = *link;
        *link = gone->next;
        output_close(gone);
        return;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static uint32_t to_u32(double v) {
    return (uint32_t)(v * UINT32_MAX);
}

static void setup_compose(Display *dpy) {
#ifdef NO_I18N
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
#else
    const char *locale = setlocale(LC_CTYPE, NULL);
#endif
    struct xkb_compose_table *table = NULL;
    /* The "C" locale has no compose file; the US one still gives the common dead keys. */
    if (locale) table = xkb_compose_table_new_from_locale(dpy->xkb, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!table)
        table = xkb_compose_table_new_from_locale(dpy->xkb, "en_US.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!table) return;
    dpy->compose_table = table;
    dpy->compose_state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
}

Display *display_create(bool demo, double r, double g, double b) {
    Display *dpy = calloc(1, sizeof(Display));
    if (!dpy) return NULL;
    dpy->demo = demo;
    dpy->forced_group = -1;
    dpy->repeat_rate = DEFAULT_REPEAT_RATE;
    dpy->repeat_delay = DEFAULT_REPEAT_DELAY;
    dpy->repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    dpy->display = wl_display_connect(NULL);
    if (!dpy->display) {
        fprintf(stderr, _("paclock: cannot connect to the compositor (is WAYLAND_DISPLAY set?)\n"));
        display_destroy(dpy);
        return NULL;
    }

    dpy->registry = wl_display_get_registry(dpy->display);
    wl_registry_add_listener(dpy->registry, &registry_listener, dpy);
    wl_display_roundtrip(dpy->display);
    /* The outputs were announced with the globals; their names come in a second round. */
    wl_display_roundtrip(dpy->display);

    if (dpy->repeat_fd < 0) {
        fprintf(stderr, "paclock: cannot create a timer\n");
        display_destroy(dpy);
        return NULL;
    }
    if (!dpy->compositor || !dpy->subcompositor || !dpy->shm || !dpy->viewporter ||
        !dpy->single_pixel || !dpy->seat || (demo ? !dpy->wm_base : !dpy->lock_manager)) {
        fprintf(stderr, demo ? "paclock: the compositor lacks a protocol paclock needs "
                               "(compositor, subcompositor, shm, viewporter, single-pixel-buffer, "
                               "seat or xdg-shell)\n"
                             : "paclock: the compositor lacks a protocol paclock needs "
                               "(compositor, subcompositor, shm, viewporter, single-pixel-buffer, "
                               "seat or ext-session-lock)\n");
        display_destroy(dpy);
        return NULL;
    }

    dpy->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    /* The compose table is made on the first key that may start a sequence (compose.h). */

    dpy->background = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
        dpy->single_pixel, to_u32(r), to_u32(g), to_u32(b), UINT32_MAX);

    dpy->running = true;
    if (demo) {
        Output *out = output_add(dpy, NULL, 0);
        if (!out) {
            display_destroy(dpy);
            return NULL;
        }
        output_open(out);
        while (dpy->running && !out->configured)
            if (wl_display_dispatch(dpy->display) < 0) break;
    } else {
        dpy->lock = ext_session_lock_manager_v1_lock(dpy->lock_manager);
        ext_session_lock_v1_add_listener(dpy->lock, &lock_listener, dpy);
        for (Output *out = dpy->outputs; out; out = out->next) output_open(out);
        /* The screen is locked once the compositor has every lock surface. */
        while (!dpy->locked && !dpy->lock_finished)
            if (wl_display_dispatch(dpy->display) < 0) break;
        if (!dpy->locked) {
            fprintf(stderr, _("paclock: the compositor did not lock the screen (is another "
                              "screen locker running?)\n"));
            display_destroy(dpy);
            return NULL;
        }
    }
    if (!dpy->running) {
        display_destroy(dpy);
        return NULL;
    }
    /* Compositors may only announce the scale once the surface is mapped. */
    wl_display_roundtrip(dpy->display);
    return dpy;
}

void display_set_pointer_handler(Display *dpy, PointerHandler handler, void *data) {
    dpy->on_pointer = handler;
    dpy->on_pointer_data = data;
}

void display_set_key_handler(Display *dpy, KeyHandler handler, void *data) {
    dpy->on_key = handler;
    dpy->on_key_data = data;
}

void display_unlock(Display *dpy) {
    if (!dpy->lock || !dpy->locked || dpy->unlocked) return;
    ext_session_lock_v1_unlock_and_destroy(dpy->lock);
    dpy->lock = NULL;
    dpy->unlocked = true;
    /* The compositor must have the request before this process is gone. */
    wl_display_roundtrip(dpy->display);
}

void display_destroy(Display *dpy) {
    if (!dpy) return;

    /* Still locked: the lock objects stay, so the screen does. Only a lock that never took
     * hold may be destroyed. */
    bool keep_lock = dpy->locked && !dpy->unlocked;
    while (dpy->outputs) {
        Output *out = dpy->outputs;
        dpy->outputs = out->next;
        if (keep_lock) {
            free(out);   /* the connection ends below; the compositor tears the rest down */
            continue;
        }
        output_close(out);
    }
    if (keep_lock) {
        if (dpy->display) wl_display_flush(dpy->display);
        goto disconnect;
    }

    if (dpy->lock) ext_session_lock_v1_destroy(dpy->lock);
    if (dpy->lock_manager) ext_session_lock_manager_v1_destroy(dpy->lock_manager);
    if (dpy->compose_state) xkb_compose_state_unref(dpy->compose_state);
    if (dpy->compose_table) xkb_compose_table_unref(dpy->compose_table);
    if (dpy->xkb_state) xkb_state_unref(dpy->xkb_state);
    if (dpy->keymap) xkb_keymap_unref(dpy->keymap);
    if (dpy->sys_keymap) xkb_keymap_unref(dpy->sys_keymap);
    if (dpy->xkb) xkb_context_unref(dpy->xkb);
    if (dpy->repeat_fd >= 0) close(dpy->repeat_fd);

    if (dpy->fractional_scale_manager)
        wp_fractional_scale_manager_v1_destroy(dpy->fractional_scale_manager);
    if (dpy->background) wl_buffer_destroy(dpy->background);
    if (dpy->keyboard) wl_keyboard_release(dpy->keyboard);
    if (dpy->cursor_shape) wp_cursor_shape_device_v1_destroy(dpy->cursor_shape);
    if (dpy->cursor_shape_manager) wp_cursor_shape_manager_v1_destroy(dpy->cursor_shape_manager);
    if (dpy->pointer) wl_pointer_release(dpy->pointer);
    if (dpy->seat) wl_seat_release(dpy->seat);
    if (dpy->wm_base) xdg_wm_base_destroy(dpy->wm_base);
    if (dpy->single_pixel) wp_single_pixel_buffer_manager_v1_destroy(dpy->single_pixel);
    if (dpy->viewporter) wp_viewporter_destroy(dpy->viewporter);
    if (dpy->shm) wl_shm_destroy(dpy->shm);
    if (dpy->subcompositor) wl_subcompositor_destroy(dpy->subcompositor);
    if (dpy->compositor) wl_compositor_destroy(dpy->compositor);
    if (dpy->registry) wl_registry_destroy(dpy->registry);
    if (dpy->display) wl_display_flush(dpy->display);

disconnect:
    if (dpy->display) wl_display_disconnect(dpy->display);
    free(dpy);
}

int display_poll(Display *dpy, struct pollfd *extra, int count, int timeout_ms) {
    struct wl_display *d = dpy->display;
    if (count > DISPLAY_MAX_EXTRA_FDS) count = DISPLAY_MAX_EXTRA_FDS;

    while (wl_display_prepare_read(d) != 0) {
        if (wl_display_dispatch_pending(d) < 0) return -1;
    }

    /* When the outgoing buffer is full, flush returns EAGAIN. Poll for POLLOUT on the
     * Wayland fd so the loop wakes up as soon as the socket drains. */
    bool need_flush = false;
    if (wl_display_flush(d) < 0) {
        if (errno != EAGAIN) {
            wl_display_cancel_read(d);
            return -1;
        }
        need_flush = true;
    }

    struct pollfd fds[2 + DISPLAY_MAX_EXTRA_FDS] = {
        { .fd = wl_display_get_fd(d), .events = POLLIN | (need_flush ? POLLOUT : 0) },
        { .fd = dpy->repeat_fd, .events = POLLIN },
    };
    memcpy(fds + 2, extra, count * sizeof(*extra));

    int ret = poll(fds, 2 + count, timeout_ms);
    if (ret < 0) {
        wl_display_cancel_read(d);
        return errno == EINTR ? 0 : -1;
    }

    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(d) < 0) return -1;
    } else {
        wl_display_cancel_read(d);
        if (fds[0].revents & (POLLERR | POLLHUP)) return -1;
    }
    if (wl_display_dispatch_pending(d) < 0) return -1;

    if (fds[1].revents & POLLIN) {
        uint64_t expirations = 0;
        if (read(dpy->repeat_fd, &expirations, sizeof(expirations)) > 0 && dpy->repeat_key &&
            dpy->on_key) {
            /* A stalled loop should not replay a backlog of key presses. */
            for (uint64_t i = 0; i < expirations && i < 4; i++)
                dpy->on_key(&dpy->repeat_event, dpy->on_key_data);
        }
    }

    for (int i = 0; i < count; i++) extra[i].revents = fds[2 + i].revents;
    return 0;
}

/* ---- panels ---- */

static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    Panel *panel = data;
    panel->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void panel_free_buffer(Panel *panel) {
    if (panel->image) cairo_surface_destroy(panel->image);
    if (panel->buffer) wl_buffer_destroy(panel->buffer);
    if (panel->data) munmap(panel->data, panel->size);
    panel->image = NULL;
    panel->buffer = NULL;
    panel->data = NULL;
    panel->width = panel->height = 0;
}

static bool panel_alloc_buffer(Panel *panel, int width, int height) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    /* -1 on error; guard before casting to size_t, which would give a huge mmap. */
    if (stride < 0) return false;
    size_t size = (size_t)stride * (size_t)height;

    int fd = memfd_create("paclock", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return false;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(panel->dpy->shm, fd, size);
    panel->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                              WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_buffer_add_listener(panel->buffer, &buffer_listener, panel);
    panel->data = data;
    panel->size = size;
    panel->width = width;
    panel->height = height;
    panel->image = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
                                                       width, height, stride);
    return true;
}

static void panel_init(Panel *panel, Display *dpy, struct wl_surface *parent) {
    memset(panel, 0, sizeof(*panel));
    panel->dpy = dpy;
    panel->parent = parent;
    panel->surface = wl_compositor_create_surface(dpy->compositor);
    panel->subsurface = wl_subcompositor_get_subsurface(dpy->subcompositor,
                                                        panel->surface, parent);
    wl_subsurface_set_desync(panel->subsurface);
    panel->viewport = wp_viewporter_get_viewport(dpy->viewporter, panel->surface);

    /* Nothing is clickable here. */
    struct wl_region *empty = wl_compositor_create_region(dpy->compositor);
    wl_surface_set_input_region(panel->surface, empty);
    wl_region_destroy(empty);
}

static void panel_finish(Panel *panel) {
    if (!panel->dpy) return;
    panel_free_buffer(panel);
    if (panel->viewport) wp_viewport_destroy(panel->viewport);
    if (panel->subsurface) wl_subsurface_destroy(panel->subsurface);
    if (panel->surface) wl_surface_destroy(panel->surface);
    memset(panel, 0, sizeof(*panel));
}

cairo_t *panel_begin(Panel *panel, int width, int height) {
    if (width <= 0 || height <= 0) return NULL;

    /* Some compositors keep a surface's last buffer until it is replaced and never release it,
     * which on the lock surfaces would freeze the card after its first frame. So a buffer
     * still held is abandoned: the compositor keeps its contents, and this frame gets a new one. */
    if (panel->busy) {
        panel_free_buffer(panel);
        panel->busy = false;
    }

    if (width != panel->width || height != panel->height) {
        panel_free_buffer(panel);
        if (!panel_alloc_buffer(panel, width, height)) return NULL;
    }

    cairo_t *cr = cairo_create(panel->image);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return cr;
}

void panel_end(Panel *panel, cairo_t *cr, int x, int y, int dest_w, int dest_h) {
    cairo_destroy(cr);
    cairo_surface_flush(panel->image);

    if (dest_w != panel->dest_w || dest_h != panel->dest_h) {
        panel->dest_w = dest_w;
        panel->dest_h = dest_h;
        wp_viewport_set_destination(panel->viewport, dest_w, dest_h);
    }

    wl_surface_attach(panel->surface, panel->buffer, 0, 0);
    wl_surface_damage_buffer(panel->surface, 0, 0, panel->width, panel->height);
    wl_surface_commit(panel->surface);
    panel->busy = true;

    if (x != panel->x || y != panel->y) {
        panel->x = x;
        panel->y = y;
        wl_subsurface_set_position(panel->subsurface, x, y);
    }
    /* The parent is committed every time, with damage: some compositors (Hyprland) redraw a lock
     * surface only when the lock surface itself commits, not when one of its subsurfaces does,
     * so the card would show its first frame and then never change. The parent's buffer is a
     * single pixel, so this costs nothing. */
    wl_surface_damage_buffer(panel->parent, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(panel->parent);
}
