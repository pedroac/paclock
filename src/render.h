#ifndef RENDER_H
#define RENDER_H

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { LEVEL_NONE, LEVEL_INFO, LEVEL_WARN, LEVEL_ERROR } Level;

/* Everything the card shows. Strings are UTF-8 and owned by the caller. */
typedef struct {
    const char *user;               /* shown above the password; NULL: not shown */
    size_t password_chars;
    const char *password_label;     /* shown while the password is empty */
    const char *message;            /* NULL or empty: none */
    Level level;
    bool message_is_denial;         /* the message says the password was refused: said with the keyboard */
    const char *layout;             /* the keyboard layout under the password; NULL: not shown */
    bool layout_focus;              /* the layout is being chosen: typing searches */
    const char *layout_filter;      /* what is typed to find one; empty: nothing */
    const char *layout_found;       /* the layout it finds, NULL if none matches */
    int layout_found_count;         /* how many match: more than one can be stepped through */
    bool busy;                      /* the password is being checked: everything is dimmed */
    bool hide_field;                /* no field (no line, no cursor) while the password is empty: only password_label */
    bool hide_length;               /* the dots do not count the characters: a full row, whatever the length */
} View;

/* contrast = high: black and white, nothing dimmed. Call before the window is made, which
 * needs to know the background. */
void render_set_contrast(bool high);
void render_background(double *r, double *g, double *b);

/* Fonts are files looked up by name in the system font directories, or the given paths
 * (either may be NULL). one_font: only the regular one is loaded, and used for the light text too
 * (font_light is ignored). */
bool render_init(const char *font, const char *font_light, bool one_font);
void render_finish(void);
/* Draw text as filled outlines, not through Cairo's glyph cache: about 260 kB less memory, and the
 * glyphs are not hinted. For resources = low and minimal. */
void render_set_text_paths(bool on);
/* The language of the texts ("ar", "zh_TW", "pt_BR.UTF-8": the part before a colon or a dot is used), so that
 * text is shaped for it and a line with no letter that says which way it goes takes its direction. */
void render_set_language(const char *language);

/* Sizes everything as fractions of the screen's shorter side times text_scale, which is not the
 * user's setting alone: main.c works it out from the screen's real size (layout_for). */
void render_layout(int screen_width, int screen_height, double text_scale, bool show_user,
                   bool show_layout);
/* show_user and show_layout must be whether View.user and View.layout are set. */
void render_card_size(int *width, int *height);
/* Where the card starts, so that the password line is in the middle of the screen. */
int render_card_y(int screen_height);
void render_card(cairo_t *cr, const View *view);

/* What is under a point of the card. */
typedef enum { HIT_NONE, HIT_PASS, HIT_LAYOUT_PREV, HIT_LAYOUT_NEXT } Hit;
typedef struct {
    bool layout;            /* the layout line is shown */
    bool layout_filtering;  /* the layout line is being typed into, and shows a result */
} HitContext;
/* x and y are relative to the card's top left, as sized by the last render_layout. The layout
 * line is split in two: its left half goes back, its right half on. */
Hit render_hit(double x, double y, HitContext ctx);

#endif
