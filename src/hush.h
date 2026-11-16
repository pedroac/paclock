#ifndef HUSH_H
#define HUSH_H

#include "settings.h"
#include "tunables.h"
#include <stdbool.h>

/* What paclock does to the rest of the session while the screen is locked, and undoes when it is
 * unlocked. Each is an option, off by default, done by running a program (no library): busctl for
 * the first two, pactl for the third.
 *  - lock_hint: tells logind the session is locked (Session.LockedHint), for whoever listens;
 *  - pause_players: pauses the MPRIS players that are playing, and plays those again;
 *  - mute_streams: mutes the playback streams of other programs, never speech-dispatcher's, and
 *    unmutes the ones it muted. A stream that was already muted is left as it was. */
typedef struct {
    bool hinted;
    int muted[HUSH_MAX_STREAMS];                /* pulse sink-input indexes */
    int muted_count;
    char paused[HUSH_MAX_PLAYERS][HUSH_NAME_MAX];   /* D-Bus names of the players */
    int paused_count;
} Hush;

/* Call once the screen is locked. A program that is missing is said on stderr, once, and skipped. */
void hush_start(Hush *h, const Settings *s);
/* Call after the screen is unlocked, so that nothing here ever keeps it locked. */
void hush_stop(Hush *h);

/* The parsers, apart from the programs, for the tests. */

/* From the text of `pactl list sink-inputs` (in the C locale): the index of each stream that is not
 * muted and not speech-dispatcher's. Returns how many, at most max. */
int hush_loud_streams(const char *text, int *index, int max);
/* From the text of `busctl list`: the names of the MPRIS players, at most max, each fitting size. */
int hush_players(const char *text, char names[][HUSH_NAME_MAX], int max);
/* Whether the text of `busctl get-property ... PlaybackStatus` says "Playing". */
bool hush_is_playing(const char *text);

#endif
