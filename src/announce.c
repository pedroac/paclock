#include "announce.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>

/* Appends text to what is about to be said, after a stop. */
static void part(char *out, size_t size, const char *text) {
    if (!text[0]) return;
    size_t n = strlen(out);
    snprintf(out + n, size - n, "%s%s", n ? ". " : "", text);
    for (char *p = out + n; *p; p++)
        if (*p == '\n') *p = ' ';   /* a line break on the screen is a pause aloud: the sentences have their stops */
}

static void remember(char *to, size_t size, const char *text) {
    snprintf(to, size, "%s", text);
}

void announce_changes(Announcer *st, const View *v, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    const char *message = v->message ? v->message : "";
    /* The line under the password: the layout, or what a search found (or that it found nothing). */
    bool filtering = v->layout_focus && v->layout_filter && v->layout_filter[0];
    const char *choice = !v->layout ? "" : !filtering ? v->layout : v->layout_found ? v->layout_found : _("No keyboard matches");

    if (!st->introduced) {
        st->introduced = true;
        remember(st->choice, sizeof(st->choice), choice);   /* not news */
        part(out, size, _("Enter your password"));
    } else if (st->focus != v->layout_focus) {
        if (v->layout_focus) {
            /* Tab into the keyboard: which one it is on, and what to do. */
            part(out, size, choice);
            part(out, size, _("Type to change it"));
        } else {
            /* Back to the password: the layout that was picked, if it changed, then the field. */
            if (strcmp(st->choice, choice)) part(out, size, choice);
            part(out, size, _("Enter your password"));
        }
        remember(st->choice, sizeof(st->choice), choice);
    } else if (strcmp(st->choice, choice)) {
        remember(st->choice, sizeof(st->choice), choice);
        part(out, size, choice);
    }
    st->focus = v->layout_focus;

    if (strcmp(st->message, message)) {
        remember(st->message, sizeof(st->message), message);
        part(out, size, message);
        /* A wrong layout explains a refusal; unless it was just said. Then the field is asked for
         * again, so that the layout is not taken for where the focus is. */
        if (v->message_is_denial) {
            if (v->layout && !strstr(out, v->layout)) part(out, size, v->layout);
            part(out, size, _("Enter your password"));
        }
    }
}
