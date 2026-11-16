#ifndef ANNOUNCE_H
#define ANNOUNCE_H

#include "render.h"
#include <stdbool.h>
#include <stddef.h>

/* What to say aloud when the screen changes, for someone who cannot see it: where Tab has moved the
 * focus, the layout or the search result, each message. The words are the ones on the screen, and
 * never the password. */
typedef struct {
    bool introduced;           /* the password field has been announced */
    bool focus;                /* layout_focus when last announced */
    char choice[96];           /* the layout line as last announced */
    char message[256];         /* the message as last announced */
} Announcer;

/* Writes into out what to say now, in one piece so that nothing cuts anything off ("" if the screen
 * changed in no way worth saying), and remembers it. */
void announce_changes(Announcer *st, const View *v, char *out, size_t size);

#endif
