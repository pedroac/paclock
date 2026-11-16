#ifndef DISPLAY_H
#define DISPLAY_H

#include <cairo/cairo.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "tunables.h"

typedef struct Display Display;
typedef struct Output Output;

typedef struct {
    uint32_t keysym;     /* XKB_KEY_* */
    char text[8];        /* what the key types (compose and dead keys applied); may be empty */
    uint32_t key;        /* the key: an evdev code, 0 for a Compose result */
    uint32_t mods;       /* the modifiers held, as an XKB mask */
    bool ctrl, alt, shift, logo;
} KeyEvent;

typedef void (*KeyHandler)(const KeyEvent *event, void *data);
/* The pointer moved over (press false) or left-clicked (press true) the point x, y, in the
 * logical pixels of an output; out is NULL when it left. Returns whether something clickable
 * is there. */
typedef bool (*PointerHandler)(Output *out, double x, double y, bool press, void *data);

/* A surface on top of the background: a buffer shown through a viewport, so it can be drawn
 * at the output's scale and still be placed in logical pixels. */
typedef struct {
    Display *dpy;
    struct wl_surface *parent;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    struct wp_viewport *viewport;
    struct wl_buffer *buffer;
    cairo_surface_t *image;
    void *data;
    size_t size;
    int width, height;      /* buffer size in pixels */
    int dest_w, dest_h;     /* size on screen in logical pixels */
    int x, y;
    bool busy;
} Panel;

/* One screen. Locked, every one has a lock surface; in a demo there is one window. */
struct Output {
    Display *dpy;
    struct wl_output *wl_output;           /* NULL in a demo */
    uint32_t global;                       /* its name in the registry */
    struct wl_surface *surface;
    struct ext_session_lock_surface_v1 *lock_surface;
    struct xdg_surface *xdg_surface;       /* demo only */
    struct xdg_toplevel *toplevel;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;
    Panel card;
    uint32_t width, height;                /* logical size */
    int physical_mm, mode_px;              /* what wl_output says: panel width in mm, and in pixels; 0 until known */
    int wl_scale;                          /* the integer scale wl_output reported; 1 until it does */
    /* Buffer pixels per logical pixel. Panels are drawn at this scale, so text stays sharp on
     * HiDPI outputs. */
    double scale;
    bool configured;
    Output *next;
};


struct Display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wp_cursor_shape_device_v1 *cursor_shape;
    uint32_t pointer_serial;       /* of the last enter: needed to set the cursor */
    Output *pointer_out;           /* the screen the pointer is on, NULL if none */
    double pointer_x, pointer_y;
    bool pointer_clickable;
    PointerHandler on_pointer;
    void *on_pointer_data;
    struct xdg_wm_base *wm_base;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_single_pixel_buffer_manager_v1 *single_pixel;
    struct ext_session_lock_manager_v1 *lock_manager;
    struct ext_session_lock_v1 *lock;
    struct wl_buffer *background;          /* shared by every output */

    Output *outputs;
    bool demo;                     /* a window to look at, not a lock */
    bool locked;                   /* the compositor says the session is locked */
    bool unlocked;                 /* ...and was unlocked again by us */
    bool lock_finished;            /* the compositor ended the lock, or never gave it */

    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    uint32_t mods_depressed, mods_latched, mods_locked, group;
    struct xkb_keymap *sys_keymap;   /* the compositor's; keymap is this or a chosen layout */
    int forced_group;                /* a group of sys_keymap chosen here; -1: the compositor's */
    bool custom_layout;              /* keymap is one made here from a layout name */
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;
    bool compose_tried;            /* the table is made on the first key that may start a sequence */

    KeyHandler on_key;
    void *on_key_data;
    int repeat_fd;                 /* timerfd that fires while a key is held */
    int repeat_rate, repeat_delay; /* keys per second (0: no repeat), milliseconds */
    uint32_t repeat_key;           /* evdev code of the held key, 0 if none */
    KeyEvent repeat_event;

    bool caps_lock;
    bool dirty;                    /* something changed: redraw */
    bool running;
};

/* Locks the session and returns once the compositor says it is locked: a lock surface on
 * every screen, covering it. With demo, opens a fullscreen window instead, which locks
 * nothing. Returns NULL (having said why on stderr) if there is no compositor, it lacks a
 * protocol paclock needs, or it refuses the lock. Background is the flat color behind
 * everything. */
Display *display_create(bool demo, double r, double g, double b);
/* Lets go of the lock, so that the screen shows the session again. */
/* Logical pixels per millimetre of this output (96 dpi if it does not say, or says nonsense). */
double output_px_per_mm(const Output *out);

void display_unlock(Display *dpy);
/* Closes the connection. A lock that was not let go of stays: the screen stays locked. */
void display_destroy(Display *dpy);
void display_set_key_handler(Display *dpy, KeyHandler handler, void *data);
void display_set_pointer_handler(Display *dpy, PointerHandler handler, void *data);

/* The layouts of the compositor's keymap (its groups), the one it has active now, and the name
 * xkb gives a group (NULL if there is none). */
int display_group_count(const Display *dpy);
#ifndef NO_LAYOUTS
int display_current_group(const Display *dpy);
const char *display_group_name(const Display *dpy, int group);
/* Types with group of the compositor's keymap from now on, or follows the compositor's own
 * again when group is -1. */
void display_use_group(Display *dpy, int group);
/* Types with a layout made here ("pt", variant "intl"). Returns false if xkb has no such one. */
bool display_set_layout(Display *dpy, const char *layout, const char *variant);
#endif
/* What the key (an evdev code) types now, with modifiers mods, in the layout in use. */
bool display_key_text(Display *dpy, uint32_t key, uint32_t mods, char *out, size_t size);

/* Waits up to timeout_ms (-1: forever) for Wayland events, a repeating key or one of the
 * extra descriptors (revents are filled in). Returns -1 on error. */
int display_poll(Display *dpy, struct pollfd *extra, int count, int timeout_ms);

/* width and height are buffer pixels. Returns NULL if the buffer could not be made. A buffer the
 * compositor still holds is abandoned, not waited for. */
cairo_t *panel_begin(Panel *panel, int width, int height);
/* x, y and dest_w, dest_h are logical: the buffer is stretched to dest_w x dest_h. */
void panel_end(Panel *panel, cairo_t *cr, int x, int y, int dest_w, int dest_h);

#endif
