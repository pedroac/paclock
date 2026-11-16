#define _GNU_SOURCE
#include "announce.h"
#include "hush.h"
#include "auth.h"
#include "config_file.h"
#include "display.h"
#include "field.h"
#include "i18n.h"
#ifndef NO_LAYOUTS
#include "layout.h"
#include "pamtext.h"
#endif
#include "render.h"
#include "settings.h"
#include "length.h"
#include "speech.h"
#include <locale.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define VERSION "0.1.0"

/* What the demo accepts: it checks nothing real. */
#define DEMO_PASSWORD "demo"

typedef struct {
    Settings settings;
    Display *dpy;
    Auth auth;
    Field pass;
    char user[256];
    char message[256];
    Level level;
    bool demo;
#ifndef NO_LAYOUTS
    LayoutPicker layouts;
    bool layout_focus;     /* the layout is being chosen: typing searches it */
    char layout_label[96], layout_found[96];   /* what View points into */
#endif
    bool done;             /* the password was right */
    bool speaking;         /* speech is on and connected */
    Hush hush;             /* what was done to the session, to undo on unlocking */
    Announcer announced;   /* what was last said, to say only what changes */
    bool denied;           /* the message is a refused password */
    bool unlocked;         /* the right password was given: say so, aloud only */
} App;

static void set_message(App *a, Level level, const char *text) {
    snprintf(a->message, sizeof(a->message), "%s", text);
    a->level = text[0] ? level : LEVEL_NONE;
    a->denied = false;
    a->dpy->dirty = true;
}

static bool busy(const App *a) {
    return a->auth.running;
}

/* ---- keyboard layout ---- */

#ifndef NO_LAYOUTS

static bool translate_key(uint32_t key, uint32_t mods, char *out, size_t size, void *data) {
    return display_key_text(((App *)data)->dpy, key, mods, out, size);
}

/* Types with layout number index from now on (0: the compositor's own, whichever group it has
 * active). What is typed already is read again with it: typed with the wrong layout, it was
 * gibberish. Returns false if xkb cannot make the layout, which is then left out of the
 * stepping. */
static bool layout_select(App *a, int index) {
    LayoutPicker *p = &a->layouts;
    const Layout *l = &p->items[index];
    if (index == 0) {
        display_use_group(a->dpy, -1);
    } else if (!display_set_layout(a->dpy, l->layout, l->variant)) {
        p->items[index].unavailable = true;
        return false;
    }
    p->current = index;
    if (field_retype(&a->pass, translate_key, a)) set_message(a, LEVEL_INFO, _("Password read again with this keyboard"));
    a->dpy->dirty = true;
    return true;
}

static void layout_move(App *a, int dir) {
    /* One that cannot be made is marked and stepped past, so the list still goes round. */
    for (int tries = 0; tries < a->layouts.count; tries++) {
        int next = picker_step(&a->layouts, dir);
        if (next < 0 || layout_select(a, next)) break;
    }
    a->dpy->dirty = true;
}

/* Ends the search, applying the layout it found. */
static void layout_settle(App *a) {
    int next = picker_settle(&a->layouts);
    if (next >= 0 && !layout_select(a, next)) set_message(a, LEVEL_ERROR, _("That keyboard is not available"));
    a->layout_focus = false;
    a->dpy->dirty = true;
}

static void layout_cancel(App *a) {
    picker_cancel(&a->layouts);
    a->layout_focus = false;
    a->dpy->dirty = true;
}
#endif /* NO_LAYOUTS */

/* ---- the password ---- */

/* A wrong password: aloud, the keyboard in use comes with it, as it may be the reason. */
static void deny(App *a) {
    set_message(a, LEVEL_ERROR, _("Authentication failed"));
    a->denied = true;
}

static void submit(App *a) {
    if (!a->pass.len) return;

    if (a->demo) {
        bool right = strcmp(a->pass.text, DEMO_PASSWORD) == 0;
        field_clear(&a->pass);
        if (right) a->done = a->unlocked = true;
        else deny(a);
        return;
    }
    set_message(a, LEVEL_NONE, "");
    if (!auth_start(&a->auth, a->user, a->pass.text)) set_message(a, LEVEL_ERROR, _("Cannot check the password"));
    field_clear(&a->pass);   /* the check has its own copy, which it wipes */
    a->dpy->dirty = true;
}

/* The account is locked for a while: what happened, how long if PAM said ("9 minutes left to
 * unlock", on a line of its own), and what to do. */
static void locked(App *a) {
    char hint[PAMTEXT_MAX], timed[PAMTEXT_MAX + 2] = "", text[PAMTEXT_MAX + 160];
    if (pamtext_hint(a->auth.info, hint, sizeof(hint))) snprintf(timed, sizeof(timed), "%s\n", hint);
    snprintf(text, sizeof(text), _("Too many failed attempts.\n%sAsk the admin to unlock or wait."), timed);
    set_message(a, LEVEL_ERROR, text);
}

static void auth_done(App *a) {
    AuthResult result;
    if (!auth_finish(&a->auth, &result)) return;

    if (result == AUTH_OK) a->done = a->unlocked = true;
    else if (result == AUTH_DENIED) deny(a);
    else if (result == AUTH_LOCKED) locked(a);
    else set_message(a, LEVEL_ERROR, _("Cannot check the password"));
    a->dpy->dirty = true;
}

/* ---- input ---- */

static void handle_key(App *a, const KeyEvent *e) {
    /* A demo is a window to look at and leave: Escape leaves, whatever is going on. */
    if (a->demo && e->keysym == XKB_KEY_Escape) {
        a->done = true;
        return;
    }
    if (busy(a)) return;

#ifndef NO_LAYOUTS
    /* From anywhere, so a wrong layout is never a dead end. */
    if (a->layouts.count > 1 && e->keysym == XKB_KEY_space && (e->ctrl || e->logo)) {
        picker_cancel(&a->layouts);
        layout_move(a, e->shift ? -1 : 1);
        return;
    }
    /* Tab or Down chooses the layout: typing then searches, Left and Right step, Enter settles,
     * Up goes back to the password. */
    if (a->layouts.count > 1 && (e->keysym == XKB_KEY_Tab || e->keysym == XKB_KEY_ISO_Left_Tab)) {
        if (a->layout_focus) layout_cancel(a);
        else a->layout_focus = true;
        a->dpy->dirty = true;
        return;
    }
    /* Down moves from the password to the layout, Up back. */
    if (a->layouts.count > 1 && e->keysym == XKB_KEY_Down && !a->layout_focus) {
        a->layout_focus = true;
        a->dpy->dirty = true;
        return;
    }
    if (a->layout_focus) {
        LayoutPicker *p = &a->layouts;
        switch (e->keysym) {
        case XKB_KEY_Up:
            layout_cancel(a);
            a->dpy->dirty = true;
            return;
        case XKB_KEY_Down:
            return;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            layout_settle(a);
            return;
        case XKB_KEY_Escape:
            layout_cancel(a);
            return;
        case XKB_KEY_Left:
        case XKB_KEY_Right:
            layout_move(a, e->keysym == XKB_KEY_Left ? -1 : 1);
            return;
        }
        bool edited = false;
        if (e->keysym == XKB_KEY_BackSpace) {
            field_backspace(&p->filter);
            edited = true;
        } else if (!e->ctrl && !e->alt && !e->logo && (unsigned char)e->text[0] >= 0x20 && e->text[0] != 0x7f) {
            edited = field_append(&p->filter, e->text);
        }
        if (edited) picker_filter_changed(p);
        a->dpy->dirty = true;
        return;
    }
#endif /* NO_LAYOUTS */

    switch (e->keysym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        submit(a);
        return;
    case XKB_KEY_Escape:
        field_clear(&a->pass);
        set_message(a, LEVEL_NONE, "");
        return;
    }

    bool edited = false;
    if (e->keysym == XKB_KEY_BackSpace) {
        if (e->ctrl) field_clear(&a->pass);
        else field_backspace(&a->pass);
        edited = true;
    } else if (e->ctrl && (e->keysym == XKB_KEY_u || e->keysym == XKB_KEY_U)) {
        field_clear(&a->pass);
        edited = true;
    } else if (!e->ctrl && !e->alt && !e->logo && (unsigned char)e->text[0] >= 0x20 &&
               e->text[0] != 0x7f) {
        edited = field_append_key(&a->pass, e->text, e->key, e->mods);
        if (!edited) set_message(a, LEVEL_WARN, _("That is as long as it can be"));
    }
    if (edited) {
        /* A new try: the old error no longer applies. */
        if (a->level != LEVEL_NONE) set_message(a, LEVEL_NONE, "");
        a->dpy->dirty = true;
    }
}

static void fill_view(App *a, View *v);

/* The language the texts are in: what was set, else the environment's, the way gettext looks. */
static const char *message_language(const Settings *s) {
    if (s->language[0]) return s->language;
    static const char *const vars[] = { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" };
    for (size_t i = 0; i < sizeof vars / sizeof vars[0]; i++) {
        const char *v = getenv(vars[i]);
        if (v && v[0]) return v;
    }
    return NULL;
}

/* The text scale the card is laid out with. */
static double card_scale(const App *a) {
    return a->settings.text_scale * (a->settings.resources == RESOURCES_MINIMAL ? MINIMAL_SCALE : 1);
}

/* Lays the card out for this screen. Everything is a fraction of one length: REFERENCE_MM of the
 * screen when it says how large it is, so text is as large to read on every screen, else the
 * screen's shorter side. text_size_min and text_size_max limit the smallest text on the card, in
 * a unit that means the same on every screen (mm, in, pt) or the screen's (%); the rest of the
 * card keeps its proportions. The card never grows past what text_scale allows of the shorter
 * side, so it fits a small screen whatever the limits say. */
static void layout_for(const App *a, const Output *out, const View *view) {
    const Settings *s = &a->settings;
    double whole = out->width < out->height ? out->width : out->height;
    double known = pac_px_per_mm_known(out->physical_mm, out->mode_px, out->scale);
    double base = known > 0 ? REFERENCE_MM * known : whole;
    double smallest = (view->layout ? LAYOUT_SIZE : MESSAGE_SIZE) * base * card_scale(a);
    double px = pac_length_clamp(smallest, s->has_text_size_min ? &s->text_size_min : NULL,
                                 s->has_text_size_max ? &s->text_size_max : NULL,
                                 output_px_per_mm(out), whole);
    double scale = card_scale(a) * base / whole * px / smallest;
    if (scale > PAC_TEXT_SCALE_MAX) scale = PAC_TEXT_SCALE_MAX;
    render_layout(out->width, out->height, scale, view->user != NULL, view->layout != NULL);
}

static bool on_pointer(Output *out, double x, double y, bool press, void *data) {
    App *a = data;
    if (!out || busy(a)) return false;

    /* The card is placed in this screen's own size, so its layout is worked out again for it. */
    View view;
    fill_view(a, &view);
    layout_for(a, out, &view);
    int w, h;
    render_card_size(&w, &h);
    HitContext ctx = {
#ifndef NO_LAYOUTS
        .layout = a->layouts.count > 1,
        .layout_filtering = a->layout_focus && a->layouts.filter.len > 0,
#endif
    };
    Hit hit = render_hit(x - ((int)out->width - w) / 2, y - render_card_y(out->height), ctx);
    if (hit == HIT_NONE) return false;
    if (!press) return true;

    switch (hit) {
    case HIT_PASS:
#ifndef NO_LAYOUTS
        /* Back to the password: a search that was typed is settled on what it found. */
        if (a->layout_focus) layout_settle(a);
#endif
        break;
    case HIT_LAYOUT_PREV:
    case HIT_LAYOUT_NEXT:
#ifndef NO_LAYOUTS
        /* Clicking a side of the layout picks the neighbour on that side. */
        a->layout_focus = true;
        layout_move(a, hit == HIT_LAYOUT_PREV ? -1 : 1);
#endif
        break;
    case HIT_NONE:
        break;
    }
    a->dpy->dirty = true;
    return true;
}

static void on_key(const KeyEvent *e, void *data) {
    handle_key(data, e);
}

/* ---- drawing ---- */

static void fill_view(App *a, View *v) {
    *v = (View){
        .user = a->settings.show_user && a->settings.resources == RESOURCES_NORMAL &&
                a->settings.privacy < PRIVACY_HIGH && a->user[0] ? a->user : NULL,
        .password_chars = field_chars(&a->pass),
        .password_label = a->settings.privacy >= PRIVACY_HIGH ? _("Enter your password") : _("Password"),
        .hide_field = a->settings.privacy >= PRIVACY_HIGH,
        .hide_length = a->settings.privacy >= PRIVACY_PARANOID,
        .message = a->message,
        .level = a->level,
        .message_is_denial = a->denied && a->message[0],
        .busy = busy(a),
    };
#ifndef NO_LAYOUTS
    const LayoutPicker *p = &a->layouts;
    if (p->count > 1) {
        /* Number 0 is the compositor's own, whose name is the group it has active. */
        const Layout *l = &p->items[p->current];
        const char *name = p->current == 0 ? display_group_name(a->dpy, display_current_group(a->dpy)) : l->name;
        if (!name) name = _("Default");
        snprintf(a->layout_label, sizeof(a->layout_label), _("%s keyboard"), p->current == 0 ? i18n_layout_name(name) : name);
        v->layout = a->layout_label;
        v->layout_focus = a->layout_focus;
        v->layout_filter = p->filter.text;
        v->layout_found_count = picker_match_count(p);
        if (p->pick) {
            snprintf(a->layout_found, sizeof(a->layout_found), _("%s keyboard"), p->items[p->pick].name);
            v->layout_found = a->layout_found;
        }
    }
#endif
    if (!a->message[0]) {
        if (busy(a)) {
            v->message = _("Checking…");
            v->level = LEVEL_INFO;
        } else if (a->dpy->caps_lock) {
            v->message = _("Caps Lock is on:\nYour password might not be accepted.");
            v->level = LEVEL_WARN;
        } else if (a->settings.privacy >= PRIVACY_PARANOID && !v->password_chars) {
            /* The length is not shown, and without a word the typing could seem to do nothing. */
            v->message = _("Press Enter when done.\nThe length is not shown.");
            v->level = LEVEL_INFO;
        }
    }
}

/* Says, if speech is on, what has changed on the screen. */
static void speak_changes(App *a, const View *v) {
    if (!a->speaking) return;
    char out[512];
    announce_changes(&a->announced, v, out, sizeof(out));
    if (out[0]) speech_say(out);
}

/* Every screen shows the same card, centered on it. */
static void redraw(App *a) {
    Display *dpy = a->dpy;
    View view;
    fill_view(a, &view);
    speak_changes(a, &view);

    for (Output *out = dpy->outputs; out; out = out->next) {
        if (!out->configured) continue;
        layout_for(a, out, &view);
        int w, h;
        render_card_size(&w, &h);
        int pw = (int)(w * out->scale + 0.5), ph = (int)(h * out->scale + 0.5);

        cairo_t *cr = panel_begin(&out->card, pw, ph);
        if (!cr) continue;   /* the buffer could not be made: try again on the next redraw */

        cairo_scale(cr, pw / (double)w, ph / (double)h);
        render_card(cr, &view);
        panel_end(&out->card, cr, ((int)out->width - w) / 2, render_card_y(out->height), w, h);
    }
    dpy->dirty = false;
}

/* ---- options ---- */

static void usage(FILE *out) {
    fprintf(out,
        "usage: paclock [-C CONTRAST] [-l LANGUAGE] [--config FILE] [--demo]\n"
        "\n"
        "A screen lock for Wayland. Type your password and press Enter.\n"
        "\n"
        "  -C, --contrast C  normal, or high for black and white with nothing dimmed\n"
        "  -l, --language L  the language of the texts, e.g. pt or pt_BR, instead of the environment's\n"
        "      --config FILE read only this configuration file\n"
        "      --demo        a window instead of a lock: the password is \"demo\"; Esc leaves\n"
        "  -h, --help        this text\n"
        "  -V, --version     the version\n"
        "\n"
        "Settings are read from /etc/pac/paclock.conf, ~/.config/pac/paclock.conf and ./paclock.conf;\n"
        "see paclock.conf.\n");
}

/* The password is in this process's memory: keep it out of any core dump. */
static void protect_memory(void) {
    struct rlimit none = { 0, 0 };
    setrlimit(RLIMIT_CORE, &none);
}

static bool find_user(char *out, size_t size) {
    const struct passwd *pw = getpwuid(getuid());
    if (!pw || strlen(pw->pw_name) >= size) return false;
    snprintf(out, size, "%s", pw->pw_name);
    return true;
}

int main(int argc, char **argv) {
#ifdef NO_I18N
    /* No setlocale: it maps the whole locale archive (2 MB resident). Only the locale's name is
     * needed, for the compose table, and that comes from the environment. */
#else
    setlocale(LC_ALL, "");
#endif
    i18n_init();
    protect_memory();

    static App app;   /* holds the password: zero-initialised and wiped on exit */
    const char *config_path = NULL;

    /* The config file first, so that the command line overrides it. */
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--config") == 0) config_path = argv[i + 1];
    Settings *settings = &app.settings;
    settings_defaults(settings);
    if (config_path) {
        if (pac_config_load_file("paclock", config_path, settings_set, settings) < 0)
            fprintf(stderr, _("paclock: cannot read %s\n"), config_path);
    } else {
        pac_config_load("paclock", settings_set, settings);
    }

    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--demo") == 0) app.demo = true;
        else if (strcmp(argv[i], "--config") == 0 && has_value) i++;
        else if ((strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--contrast") == 0) && has_value) {
            if (!settings_set("contrast", argv[++i], settings)) goto bad_usage;
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--language") == 0) && has_value) {
            if (!settings_set("language", argv[++i], settings)) goto bad_usage;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            puts("paclock " VERSION);
            return 0;
        } else {
            goto bad_usage;
        }
    }

    if (!i18n_set_language(settings->language))
        fprintf(stderr, _("paclock: cannot use the language \"%s\"\n"), settings->language);

    if (app.demo) find_user(app.user, sizeof(app.user));

    /* Checked before the screen is locked: a lock that nothing can open locks you out. */
    if (!app.demo) {
        if (!find_user(app.user, sizeof(app.user))) {
            fprintf(stderr, _("paclock: cannot tell who is logged in\n"));
            return 1;
        }
        if (!auth_service_installed()) {
            fprintf(stderr, _("paclock: the PAM service file %s/pam.d/paclock is missing, so no "
                              "password could unlock the screen; not locking (make install "
                              "puts it there)\n"), SYSCONFDIR);
            return 1;
        }
    }

    app.auth.wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    /* SIGINT and SIGTERM arrive as a readable descriptor, so the loop can end cleanly. */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sig_fd = signalfd(-1, &mask, SFD_CLOEXEC);
    /* Checked before the screen is locked: without them a check could never report back. */
    if (app.auth.wake < 0 || sig_fd < 0) {
        fprintf(stderr, "paclock: cannot create an event descriptor\n");
        return 1;
    }

    /* Everything that can fail comes before the screen is locked: past that point, giving up
     * would leave it locked with no field to type in. */
    if (!render_init(settings->font[0] ? settings->font : NULL,
                     settings->font_light[0] ? settings->font_light : NULL,
                     settings->resources == RESOURCES_MINIMAL)) {
        fprintf(stderr, _("paclock: cannot start FreeType\n"));
        return 1;
    }
    render_set_text_paths(settings->resources != RESOURCES_NORMAL);
    render_set_language(message_language(settings));
    render_set_contrast(settings->high_contrast);
    /* Before the lock too: after it, nothing may wait for a speech server. */
    if (settings->speech) {
        app.speaking = speech_open("paclock", settings->language);
    }
    double r, g, b;
    render_background(&r, &g, &b);
    app.dpy = display_create(app.demo, r, g, b);
    if (!app.dpy) {
        speech_close();
        render_finish();
        return 1;
    }
#ifndef NO_LAYOUTS
    /* The compositor's own layout first, then the others to choose from. */
    app.layouts.count = 1 + (settings->resources != RESOURCES_NORMAL || settings->privacy >= PRIVACY_HIGH ? 0 : layouts_load(app.layouts.items + 1, MAX_LAYOUTS, settings->layouts));
#endif
    display_set_key_handler(app.dpy, on_key, &app);
    display_set_pointer_handler(app.dpy, on_pointer, &app);
    /* The first frame before the programs run, which can take a while. The demo locks nothing, so
     * it leaves the session alone too. */
    redraw(&app);
    if (!app.demo) hush_start(&app.hush, settings);

    while (app.dpy->running && !app.done) {
        if (app.dpy->dirty) redraw(&app);

        struct pollfd fds[2] = {
            { .fd = app.auth.wake, .events = POLLIN },
            { .fd = sig_fd, .events = POLLIN },
        };
        if (display_poll(app.dpy, fds, 2, -1) < 0) break;
        if (fds[0].revents & POLLIN) auth_done(&app);
        if (fds[1].revents & POLLIN) break;
    }

    /* Only the right password lets go of the lock. Ending any other way (a signal, the
     * compositor going away) leaves the screen locked. */
    int status = app.done ? 0 : 1;
    if (app.done) {
        display_unlock(app.dpy);
        if (!app.demo) hush_stop(&app.hush);
    }
    auth_abort(&app.auth);
    field_clear(&app.pass);
    /* The one thing said that is not on the screen: it is gone. Said after unlocking, so that
     * waiting for it never keeps the screen locked. */
    if (app.speaking && app.unlocked) {
        speech_say(_("Unlocked"));
        speech_finish(SPEECH_UNLOCK_WAIT_MS);
    }
    speech_close();
    render_finish();
    display_destroy(app.dpy);
    return status;

bad_usage:
    usage(stderr);
    return 2;
}
