#include "render.h"
#include "fontfind.h"
#include "i18n.h"
#include "palette.h"
#include "textlayout.h"
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include <math.h>
#include <stdint.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "tunables.h"

#define EDGE_PADDING 0.020       /* between the text and the ends of the line */

typedef PacColor Color;
typedef PacPalette Palette;

/* The shared look (palette.h). contrast = high is black and white, with nothing dimmed. */
static Palette pal;   /* set by render_set_contrast, before anything is drawn */

static const char *const regular_fonts[] = {
    "NotoSans-Regular.ttf", "DejaVuSans.ttf", "LiberationSans-Regular.ttf", NULL,
};
static const char *const light_fonts[] = {
    "NotoSans-Light.ttf", "DejaVuSans-ExtraLight.ttf", NULL,
};

static FT_Library ft;
static cairo_font_face_t *regular, *light;
static bool text_as_paths;   /* see render_set_text_paths */
static cairo_font_face_t *current;   /* what set_font chose: the font of the text being measured */
static double unit;   /* the screen's shorter side, scaled */
static double card_w, card_h;
/* Where everything is, computed once per layout. */
static struct {
    double top;               /* room above the password line, for the username */
    double row, field_w;
    double layout_y, result_y, message_y;   /* baselines */
} g;

static const cairo_user_data_key_t ft_face_key;

static void done_face(void *face) {
    FT_Done_Face(face);
}

static cairo_font_face_t *load_face(const char *path, const char *const *names, int index) {
    char found[256];
    if (!path || access(path, R_OK) != 0) {
        FontDirs dirs;
        font_dirs_init(&dirs);
        path = font_find(dirs.list, names, found, sizeof(found)) ? found : NULL;
    }
    if (!path) return NULL;

    FT_Face face;
    if (FT_New_Face(ft, path, index, &face) != 0) return NULL;
    cairo_font_face_t *cface = cairo_ft_font_face_create_for_ft_face(face, 0);
    cairo_font_face_set_user_data(cface, &ft_face_key, face, done_face);
    return cface;
}

/* The fonts above cover Latin, Cyrillic and Greek. Letters of other scripts (typed with
 * another keyboard layout, or in a PAM prompt) come from the font of their script, which is
 * only looked up and loaded when such a letter first has to be drawn. */
typedef struct {
    uint32_t lo, hi;
    const char *file;
    int index;           /* the face in a .ttc: Noto CJK is JP, KR, SC, TC, HK; or CJK_BY_LANGUAGE */
} ScriptFont;

/* The ideographs are drawn differently in Japanese, Korean and the two Chinese: the face is the language's. */
#define CJK_BY_LANGUAGE (-1)

static const ScriptFont script_fonts[] = {
    { 0x0530, 0x058F, "NotoSansArmenian-Regular.ttf", 0 },
    { 0x0590, 0x05FF, "NotoSansHebrew-Regular.ttf", 0 },
    { 0xFB1D, 0xFB4F, "NotoSansHebrew-Regular.ttf", 0 },
    { 0x0600, 0x06FF, "NotoSansArabic-Regular.ttf", 0 },
    { 0x0750, 0x077F, "NotoSansArabic-Regular.ttf", 0 },
    { 0x08A0, 0x08FF, "NotoSansArabic-Regular.ttf", 0 },
    { 0xFB50, 0xFDFF, "NotoSansArabic-Regular.ttf", 0 },
    { 0xFE70, 0xFEFF, "NotoSansArabic-Regular.ttf", 0 },
    { 0x0700, 0x074F, "NotoSansSyriac-Regular.ttf", 0 },
    { 0x0900, 0x097F, "NotoSansDevanagari-Regular.ttf", 0 },
    { 0x0980, 0x09FF, "NotoSansBengali-Regular.ttf", 0 },
    { 0x0A00, 0x0A7F, "NotoSansGurmukhi-Regular.ttf", 0 },
    { 0x0A80, 0x0AFF, "NotoSansGujarati-Regular.ttf", 0 },
    { 0x0B00, 0x0B7F, "NotoSansOriya-Regular.ttf", 0 },
    { 0x0B80, 0x0BFF, "NotoSansTamil-Regular.ttf", 0 },
    { 0x0C00, 0x0C7F, "NotoSansTelugu-Regular.ttf", 0 },
    { 0x0C80, 0x0CFF, "NotoSansKannada-Regular.ttf", 0 },
    { 0x0D00, 0x0D7F, "NotoSansMalayalam-Regular.ttf", 0 },
    { 0x0D80, 0x0DFF, "NotoSansSinhala-Regular.ttf", 0 },
    { 0x0E00, 0x0E7F, "NotoSansThai-Regular.ttf", 0 },
    { 0x0E80, 0x0EFF, "NotoSansLao-Regular.ttf", 0 },
    { 0x0F00, 0x0FFF, "NotoSansTibetan-Regular.ttf", 0 },
    { 0x1000, 0x109F, "NotoSansMyanmar-Regular.ttf", 0 },
    { 0x10A0, 0x10FF, "NotoSansGeorgian-Regular.ttf", 0 },
    { 0x2D00, 0x2D2F, "NotoSansGeorgian-Regular.ttf", 0 },
    { 0x1200, 0x139F, "NotoSansEthiopic-Regular.ttf", 0 },
    { 0x1780, 0x17FF, "NotoSansKhmer-Regular.ttf", 0 },
    { 0x1100, 0x11FF, "NotoSansCJK-Regular.ttc", 1 },   /* Hangul */
    { 0x3130, 0x318F, "NotoSansCJK-Regular.ttc", 1 },
    { 0xAC00, 0xD7AF, "NotoSansCJK-Regular.ttc", 1 },
    { 0x2E80, 0x9FFF, "NotoSansCJK-Regular.ttc", CJK_BY_LANGUAGE },   /* kana and ideographs */
    { 0xFF00, 0xFFEF, "NotoSansCJK-Regular.ttc", CJK_BY_LANGUAGE },
    /* Anything else: whatever covers the most. */
    { 0, 0x10FFFF, "DejaVuSans.ttf", 0 },
    { 0, 0x10FFFF, "NotoSansSymbols-Regular.ttf", 0 },
    { 0, 0x10FFFF, "NotoSansSymbols2-Regular.ttf", 0 },
};

/* One slot per entry of the table: it cannot hold more distinct files than that. */
#define MAX_LOADED (sizeof(script_fonts) / sizeof(script_fonts[0]))

typedef struct {
    const char *file;
    int index;
    cairo_font_face_t *face;   /* NULL: looked for and not found */
    FT_Face ft;
} Loaded;

static Loaded loaded[MAX_LOADED];
static int loaded_count;

static FT_Face ft_of(cairo_font_face_t *face) {
    return face ? cairo_font_face_get_user_data(face, &ft_face_key) : NULL;
}

/* Loads a font file on first use, and remembers a miss too: searching the font directories
 * is not free. */
static Loaded *font_lazy(const char *file, int index) {
    for (int i = 0; i < loaded_count; i++)
        if (strcmp(loaded[i].file, file) == 0 && loaded[i].index == index)
            return loaded[i].face ? &loaded[i] : NULL;
    if ((size_t)loaded_count == MAX_LOADED) return NULL;

    Loaded *l = &loaded[loaded_count++];
    const char *names[] = { file, NULL };
    l->file = file;
    l->index = index;
    l->face = load_face(NULL, names, index);
    l->ft = ft_of(l->face);
    return l->face ? l : NULL;
}

void render_set_contrast(bool high) {
    pal = high ? PAC_PALETTE_HIGH : PAC_PALETTE_NORMAL;
}

void render_background(double *r, double *green, double *b) {
    *r = pal.background.r;
    *green = pal.background.g;
    *b = pal.background.b;
}

bool render_init(const char *font, const char *font_light, bool one_font) {
    pal = PAC_PALETTE_NORMAL;
    if (FT_Init_FreeType(&ft) != 0) return false;
    regular = load_face(font, regular_fonts, 0);
    /* One font file is one less mapped: the regular face does for the field too. */
    if (!one_font) light = load_face(font_light ? font_light : font, light_fonts, 0);
    if (!light) light = regular ? cairo_font_face_reference(regular) : NULL;
    /* Without any font file, fall back to Cairo's own lookup (fontconfig). */
    if (!regular) {
        fputs(_("paclock: no font file found; using Cairo's font lookup\n"), stderr);
        regular = cairo_toy_font_face_create("sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        light = cairo_font_face_reference(regular);
    }
    return true;
}

void render_finish(void) {
    pac_text_finish();   /* the shaping holds the faces: before they are freed */
    for (int i = 0; i < loaded_count; i++)
        if (loaded[i].face) cairo_font_face_destroy(loaded[i].face);
    loaded_count = 0;
    if (regular) cairo_font_face_destroy(regular);
    if (light) cairo_font_face_destroy(light);
    regular = light = NULL;
    FT_Done_FreeType(ft);
}

void render_layout(int screen_width, int screen_height, double text_scale, bool show_user,
                   bool show_layout) {
    unit = (screen_width < screen_height ? screen_width : screen_height) * text_scale;
    /* Keep the card on a narrow screen however large the text is. */
    if (unit > screen_width * 1.6) unit = screen_width * 1.6;

    g.top = show_user ? USER_ROOM * unit : 0;
    g.row = FIELD_ROW * unit;
    g.field_w = FIELD_WIDTH * unit;
    g.message_y = g.row + MESSAGE_OFFSET * unit;
    /* The layout is only for somebody stuck with the wrong one: well below the message. */
    g.layout_y = g.message_y + LAYOUT_OFFSET * unit;
    g.result_y = g.layout_y + LAYOUT_RESULT_OFFSET * unit;
    card_w = ceil(CARD_WIDTH * unit);
    card_h = ceil(g.top + (show_layout ? g.result_y + LAYOUT_SIZE * unit * 0.5 : g.message_y + (MESSAGE_LINES - 1) * MESSAGE_SIZE * unit * 1.45) +
                  BOTTOM_MARGIN * unit);
}

int render_card_y(int screen_height) {
    /* The password line sits in the middle of the screen; the username is above it and the
     * message hangs below. */
    double y = screen_height / 2.0 - g.row / 2 - g.top;
    if (y + card_h > screen_height) y = screen_height - card_h;   /* a short screen */
    return y < 0 ? 0 : (int)y;
}

void render_card_size(int *width, int *height) {
    *width = (int)card_w;
    *height = (int)card_h;
}

static void set_color(cairo_t *cr, Color c, double alpha) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

static void set_font(cairo_t *cr, cairo_font_face_t *face, double size) {
    current = face;
    cairo_set_font_face(cr, face);
    cairo_set_font_size(cr, size * unit);
}

/* The language the texts are in, for the shaping and for the direction of a line with no letter that
 * says it: "ar", "zh-TW". The part before ':' '.' or '@' of what is set; '_' is '-'. */
static char text_language[32];

void render_set_language(const char *language) {
    size_t n = 0;
    for (; language && language[n] && !strchr(":.@", language[n]) && n < sizeof(text_language) - 1; n++)
        text_language[n] = language[n] == '_' ? '-' : language[n];
    text_language[n] = '\0';
}

/* Which face of Noto CJK draws the ideographs: they are drawn differently in Japanese, Korean and
 * the two Chinese. The faces of the collection: JP, KR, SC, TC, HK. */
static int cjk_face_index(void) {
    if (strncmp(text_language, "ko", 2) == 0) return 1;
    if (strncmp(text_language, "zh", 2) == 0) {
        if (strstr(text_language, "TW") || strstr(text_language, "Hant")) return 3;
        if (strstr(text_language, "HK") || strstr(text_language, "MO")) return 4;
        return 2;
    }
    return 0;
}

/* The face to draw a code point with: the current one if it has the letter, else the one of its
 * script (textlayout.h). NULL if nobody has it: it is drawn as the current font's "missing" box. */
static FT_Face pick_face(uint32_t cp, void *data) {
    (void)data;
    FT_Face primary = ft_of(current);
    if (primary && FT_Get_Char_Index(primary, cp)) return primary;

    for (size_t i = 0; i < sizeof(script_fonts) / sizeof(script_fonts[0]); i++) {
        const ScriptFont *s = &script_fonts[i];
        if (cp < s->lo || cp > s->hi) continue;
        int index = s->index == CJK_BY_LANGUAGE ? cjk_face_index() : s->index;
        Loaded *l = font_lazy(s->file, index);
        if (l && FT_Get_Char_Index(l->ft, cp)) return l->ft;
    }
    return NULL;
}

/* The cairo face of an FT face that textlayout picked. */
static cairo_font_face_t *cairo_face_of(FT_Face face) {
    if (ft_of(current) == face) return current;
    for (int i = 0; i < loaded_count; i++)
        if (loaded[i].ft == face && loaded[i].face) return loaded[i].face;
    return current;
}

static PacTextOptions text_options(void) {
    /* The texts are in the language of the interface: its direction, whatever letter a message starts with. */
    return (PacTextOptions){ pick_face, NULL, ft_of(current), text_language[0] ? text_language : NULL,
                             pac_text_language_rtl(text_language) ? PAC_TEXT_RTL : PAC_TEXT_LTR };
}

void render_set_text_paths(bool on) {
    text_as_paths = on;
}

/* Cairo keeps a glyph cache for show_glyphs (about 260 kB of memory the first time); the outline of
 * a glyph, filled, does not use it. */
static void draw_glyphs(cairo_t *cr, const cairo_glyph_t *glyphs, int count) {
    if (!text_as_paths) {
        cairo_show_glyphs(cr, glyphs, count);
        return;
    }
    cairo_glyph_path(cr, glyphs, count);
    cairo_fill(cr);
}

/* Lays out s (shaped, and in the order of its scripts: textlayout.h), each letter in the font that
 * has it, from the current point; draws it if asked. Returns the width. */
static double run_text(cairo_t *cr, const char *s, bool draw) {
    if (!ft_of(current)) {   /* Cairo's own font: let it do the work */
        cairo_text_extents_t e;
        cairo_text_extents(cr, s, &e);
        if (draw && text_as_paths) {
            double x, y;
            cairo_get_current_point(cr, &x, &y);
            cairo_text_path(cr, s);
            cairo_fill(cr);
            cairo_move_to(cr, x + e.x_advance, y);
        } else if (draw) {
            cairo_show_text(cr, s);
        }
        return e.x_advance;
    }

    static PacTextLine line;
    PacTextOptions options = text_options();
    if (!pac_text_layout(&options, s, &line)) return 0;
    cairo_matrix_t matrix;
    cairo_get_font_matrix(cr, &matrix);
    double size = matrix.xx;   /* the font size: the layout is in em */

    if (draw && line.count) {
        double x0, y0;
        cairo_get_current_point(cr, &x0, &y0);
        cairo_glyph_t glyphs[PAC_TEXT_GLYPHS_MAX];
        for (size_t start = 0; start < line.count;) {
            size_t end = start;
            FT_Face face = line.glyphs[start].face;
            for (; end < line.count && line.glyphs[end].face == face; end++)
                glyphs[end - start] = (cairo_glyph_t){ line.glyphs[end].glyph, x0 + line.glyphs[end].x * size,
                                                       y0 + line.glyphs[end].y * size };
            cairo_set_font_face(cr, cairo_face_of(face));
            draw_glyphs(cr, glyphs, (int)(end - start));
            start = end;
        }
        cairo_set_font_face(cr, current);
        cairo_move_to(cr, x0 + line.width * size, y0);
    }
    return line.width * size;
}

static double text_width(cairo_t *cr, const char *s) {
    return run_text(cr, s, false);
}

static void show_text(cairo_t *cr, const char *s) {
    run_text(cr, s, true);
}

/* Copies text into out, shortened with an ellipsis until it fits max_w: at the end for
 * labels, at the start for what is being typed (the end is where the cursor is). It is cut
 * between clusters, never between a letter and its marks. */
static void fit(cairo_t *cr, const char *text, double max_w, bool keep_end, char *out, size_t size) {
    snprintf(out, size, "%s", text);
    if (text_width(cr, out) <= max_w) return;

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", text);
    for (;;) {
        int written;
        if (keep_end) {
            size_t skip = pac_text_next_cluster(tmp, 0);
            memmove(tmp, tmp + skip, strlen(tmp + skip) + 1);
            written = snprintf(out, size, "…%s", tmp);
        } else {
            tmp[pac_text_prev_cluster(tmp, strlen(tmp))] = '\0';
            written = snprintf(out, size, "%s…", tmp);
        }
        /* What does not fit in `out` is cut: the width of what is left is what is measured. */
        if (written < 0 || !tmp[0] || text_width(cr, out) <= max_w) return;
    }
}

static void draw_centered(cairo_t *cr, const char *s, double cx, double baseline) {
    cairo_move_to(cr, cx - text_width(cr, s) / 2, baseline);
    show_text(cr, s);
}

static void draw_rule(cairo_t *cr, double cx, double y, bool focused, double alpha) {
    double w = g.field_w, t = LINE_WIDTH * unit;
    set_color(cr, focused ? pal.text : pal.rule, alpha);
    cairo_rectangle(cr, cx - w / 2, y - t, w, t);
    cairo_fill(cr);
}

static void draw_cursor(cairo_t *cr, double x, double baseline, double alpha) {
    double h = FIELD_SIZE * unit * 0.72, t = LINE_WIDTH * unit;
    set_color(cr, pal.text, alpha);
    cairo_rectangle(cr, x, baseline - h * 0.92, t, h);
    cairo_fill(cr);
}

/* One field: its dots (or label while empty), a cursor when focused, and the line below. */
static void draw_field(cairo_t *cr, double cx, double top, bool focused, double alpha,
                       const char *label, size_t dots, bool hide_empty) {
    double row = g.row;
    double baseline = top + row * 0.72;
    double room = g.field_w - 2 * EDGE_PADDING * unit;
    bool empty = dots == 0;

    set_font(cr, light, FIELD_SIZE);
    double end_x = cx;

    if (empty) {
        char shown[256];
        /* A prompt wider than the field (a long translation, a wide script) is set smaller, down to
         * LABEL_MIN_SHRINK of its size, before any of it is cut off: the verb at its end is what says what to do. */
        double width = text_width(cr, label);
        if (width > room) set_font(cr, light, FIELD_SIZE * fmax(room / width * 0.97, LABEL_MIN_SHRINK));   /* a little less, so rounding does not tip it over */
        fit(cr, label, room, false, shown, sizeof(shown));
        set_color(cr, focused ? pal.label_focus : pal.label, alpha);
        draw_centered(cr, shown, cx, baseline);
        /* The cursor waits where the typing will start. */
        end_x = cx - text_width(cr, shown) / 2 - 0.004 * unit;
    } else {
        /* Only as many dots as fit: the newest stay in view. */
        size_t fits = (size_t)(room / (DOT_PITCH * unit));
        size_t shown = dots < fits ? dots : fits;
        double pitch = DOT_PITCH * unit, r = DOT_RADIUS * unit;
        double start = cx - shown * pitch / 2 + pitch / 2;
        set_color(cr, pal.text, alpha);
        for (size_t i = 0; i < shown; i++) {
            cairo_arc(cr, start + i * pitch, baseline - FIELD_SIZE * unit * 0.30, r, 0, 2 * M_PI);
            cairo_fill(cr);
        }
        end_x = cx + shown * pitch / 2;
    }

    if (empty && hide_empty) return;   /* only the label: the field shows when the typing starts */
    if (focused) draw_cursor(cr, empty ? end_x : end_x + 0.004 * unit, baseline, alpha);
    draw_rule(cr, cx, top + row, focused, alpha);
}

/* A message of up to MESSAGE_LINES lines: the line breaks in it are kept, and one without any that
 * is too long goes on two. An explanation needs room. */
static void draw_message(cairo_t *cr, const char *text, double cx) {
    char lines[MESSAGE_LINES][256] = {{0}}, all[512];
    int count = 0;
    snprintf(all, sizeof(all), "%s", text);

    char *start = all;
    while (count < MESSAGE_LINES) {
        char *newline = count < MESSAGE_LINES - 1 ? strchr(start, '\n') : NULL;
        if (newline) *newline = '\0';
        snprintf(lines[count++], sizeof(lines[0]), "%s", start);
        if (!newline) break;
        start = newline + 1;
    }
    /* More breaks than there are lines (a translation may have them) are spaces. */
    for (char *p = lines[count - 1]; *p; p++)
        if (*p == '\n') *p = ' ';

    char *first = lines[0], *second = lines[1];
    if (count == 1 && text_width(cr, first) > g.field_w) {
        /* A line with no break that is too long goes on two: where the script allows (a space, between
         * ideographs), else after the last letter that fits. */
        PacTextOptions options = text_options();
        cairo_matrix_t matrix;
        cairo_get_font_matrix(cr, &matrix);
        size_t cut = pac_text_fit(&options, first, g.field_w / matrix.xx, NULL);
        if (cut > 0 && cut < strlen(first)) {
            const char *rest = first + cut;
            while (*rest == ' ') rest++;
            snprintf(second, sizeof(lines[1]), "%s", rest);
            first[cut] = '\0';
            for (size_t n = strlen(first); n > 0 && first[n - 1] == ' ';) first[--n] = '\0';
            count = 2;
        }
    }
    char shown[256];
    for (int i = 0; i < count; i++) {
        if (!lines[i][0]) continue;
        fit(cr, lines[i], g.field_w, false, shown, sizeof(shown));
        draw_centered(cr, shown, cx, g.message_y + i * MESSAGE_SIZE * unit * 1.45);
    }
}

/* A choice: the text, with arrows around it while it is the one being changed. */
static void draw_choice(cairo_t *cr, const char *text, double y, double size, Color rest,
                        bool focused, bool choice, double alpha) {
    char shown[256];
    set_font(cr, regular, size);
    double gap = 0.012 * unit, cx = card_w / 2;
    double arrows = choice && focused ? 2 * (text_width(cr, "‹") + gap) : 0;
    fit(cr, text, g.field_w - arrows, false, shown, sizeof(shown));

    double w = text_width(cr, shown);
    set_color(cr, focused ? pal.text : rest, alpha);
    draw_centered(cr, shown, cx, y);
    if (choice && focused) {
        cairo_move_to(cr, cx - w / 2 - gap - text_width(cr, "‹"), y);
        show_text(cr, "‹");
        cairo_move_to(cr, cx + w / 2 + gap, y);
        show_text(cr, "›");
    }
}

/* What was typed to find a layout, with a cursor. The layout it found goes below. */
static void draw_layout_filter(cairo_t *cr, double y, const char *filter, double alpha) {
    set_font(cr, regular, LAYOUT_SIZE);
    char shown[256];
    fit(cr, filter, g.field_w, true, shown, sizeof(shown));   /* the end is where the cursor is */
    double w = text_width(cr, shown), x = card_w / 2 - w / 2;
    set_color(cr, pal.text, alpha);
    cairo_move_to(cr, x, y);
    show_text(cr, shown);
    double bar_h = LAYOUT_SIZE * unit * 0.95, t = LINE_WIDTH * unit;
    cairo_rectangle(cr, x + w + 0.004 * unit, y - bar_h * 0.85, t, bar_h);
    cairo_fill(cr);
}

void render_card(cairo_t *cr, const View *v) {
    double cx = card_w / 2;
    double alpha = v->busy ? pal.busy_alpha : 1.0;

    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
    cairo_set_font_options(cr, options);
    cairo_font_options_destroy(options);

    if (v->user) {
        char shown[256];
        set_font(cr, regular, USER_SIZE);
        set_color(cr, pal.dim, alpha);
        fit(cr, v->user, g.field_w, false, shown, sizeof(shown));
        draw_centered(cr, shown, cx, g.top - USER_GAP * unit);
    }
    /* Everything else is placed from the password line down. */
    cairo_translate(cr, 0, g.top);

    /* A full row of dots (they only show as many as fit) when the length must not show. */
    size_t dots = v->hide_length && v->password_chars ? (size_t)-1 : v->password_chars;
    draw_field(cr, cx, 0, !v->layout_focus, alpha, v->password_label, dots, v->hide_field);

    bool filtering = v->layout_filter && v->layout_filter[0] && v->layout_focus;
    if (v->layout && filtering) {
        draw_layout_filter(cr, g.layout_y, v->layout_filter, alpha);
        /* The result is a line of its own, below: the layout found, or that there is none. */
        if (v->layout_found)
            draw_choice(cr, v->layout_found, g.result_y, LAYOUT_SIZE, pal.dim, true,
                        v->layout_found_count > 1, alpha);
        else
            draw_choice(cr, _("No keyboard matches"), g.result_y, LAYOUT_SIZE, pal.warn, false, false, alpha);
    } else if (v->layout) {
        draw_choice(cr, v->layout, g.layout_y, LAYOUT_SIZE, pal.faint, v->layout_focus, true, alpha);
        /* Nothing says that typing searches: say it, where the result would show. */
        if (v->layout_focus)
            draw_choice(cr, _("Type to search"), g.result_y, LAYOUT_SIZE, pal.faint, false, false, alpha);
    }

    if (v->message && v->message[0]) {
        set_font(cr, regular, MESSAGE_SIZE);
        set_color(cr, v->level == LEVEL_ERROR ? pal.error : v->level == LEVEL_WARN ? pal.warn : pal.dim, 1.0);
        draw_message(cr, v->message, cx);
    }
}

Hit render_hit(double x, double y, HitContext ctx) {
    y -= g.top;
    double half = g.field_w / 2, cx = card_w / 2;
    if (x < cx - half || x > cx + half) return HIT_NONE;

    if (y >= 0 && y < g.row + LAYOUT_SIZE * unit * 0.5) return HIT_PASS;

    /* A line of text is hit within a line's height around it. */
    double reach = LAYOUT_SIZE * unit * 1.4, lift = LAYOUT_SIZE * unit * 0.3;
    /* While a layout is being searched for, its line and the result below it both count. */
    double top = g.layout_y - lift - reach;
    double bottom = (ctx.layout_filtering ? g.result_y : g.layout_y) - lift + reach;
    if (ctx.layout && y > top && y < bottom) return x < cx ? HIT_LAYOUT_PREV : HIT_LAYOUT_NEXT;
    return HIT_NONE;
}
