#ifndef PACLOCK_TUNABLES_H
#define PACLOCK_TUNABLES_H
/* Everything in paclock that a person may want to change: sizes, limits, timings and defaults. Each can
 * be set when building, without editing a file:
 *
 *     make CFLAGS="-O2 -DNAME=value"
 *
 * Build every file with the same values. A value that would make the code unsafe or useless is
 * refused by the checks at the end of this file, at build time. */

/* ---- auth.h ---- */
/* Auth password max. The reason for this default is not written down yet. */
/* The most of what PAM says during a check that is kept (pamtext.c), to show the wait of a locked account. */
#ifndef PAMTEXT_MAX
#define PAMTEXT_MAX 160
#endif
#ifndef AUTH_PASSWORD_MAX
#define AUTH_PASSWORD_MAX 512
#endif

/* ---- display.c ---- */
/* Max scale. The reason for this default is not written down yet. */
#ifndef MAX_SCALE
#define MAX_SCALE 8.0
#endif
/* Until the compositor says otherwise. */
#ifndef DEFAULT_REPEAT_RATE
#define DEFAULT_REPEAT_RATE 25
#endif
/* Default repeat delay. The reason for this default is not written down yet. */
#ifndef DEFAULT_REPEAT_DELAY
#define DEFAULT_REPEAT_DELAY 600
#endif

/* ---- display.h ---- */
/* Display max extra fds. The reason for this default is not written down yet. */
#ifndef DISPLAY_MAX_EXTRA_FDS
#define DISPLAY_MAX_EXTRA_FDS 4
#endif

/* ---- speech ---- */
/* How long, in milliseconds, paclock waits at the end for "Unlocked" to be spoken before it exits.
 * The screen is already unlocked then; it only keeps the process, and the speech, alive. */
#ifndef SPEECH_UNLOCK_WAIT_MS
#define SPEECH_UNLOCK_WAIT_MS 2000
#endif

/* ---- hush.c ---- */
/* How long, in milliseconds, a program run on locking or unlocking (busctl, pactl) may take
 * before it is killed. */
#ifndef HUSH_TIMEOUT_MS
#define HUSH_TIMEOUT_MS 1000
#endif
/* The most that is read from one of them (allocated while it is read, then freed): `pactl list sink-inputs` is about 3 KiB a stream. */
#ifndef HUSH_OUTPUT_MAX
#define HUSH_OUTPUT_MAX (256 * 1024)
#endif
/* The most streams muted, and players paused. */
#ifndef HUSH_MAX_STREAMS
#define HUSH_MAX_STREAMS 64
#endif
#ifndef HUSH_MAX_PLAYERS
#define HUSH_MAX_PLAYERS 16
#endif
/* The most environment variables passed to pactl; more are left out. */
#ifndef HUSH_ENV_MAX
#define HUSH_ENV_MAX 512
#endif
/* The longest D-Bus name of a player, with its NUL. */
#ifndef HUSH_NAME_MAX
#define HUSH_NAME_MAX 128
#endif

/* ---- layout.h ---- */
/* Max layouts. The reason for this default is not written down yet. */
#ifndef MAX_LAYOUTS
#define MAX_LAYOUTS 256
#endif

/* The largest xkeyboard-config rules list that is read, in bytes. A real evdev.lst is about
 * 100 KiB; a bigger file is a mistake, and a pipe or a device is never read. */
#ifndef LIST_MAX
#define LIST_MAX (1 << 20)
#endif

/* ---- main.c ---- */
/* What `resources = minimal` multiplies the size of everything by (text_scale still applies on top): the
 * card is the only thing paclock draws, and its buffer is the largest piece of memory it owns.
 * Smaller than this, the message under the password is under 10 pixels high on a 720p screen. */
#ifndef MINIMAL_SCALE
#define MINIMAL_SCALE 0.7
#endif

/* ---- render.c ---- */
/* Sizes are fractions of one reference length, which is REFERENCE_MM millimetres on a screen that
 * says its size (so text is as large to read on every screen), or else the screen's shorter side.
 * 300 mm is about the shorter side of a 24" monitor, where the password text is then 12 mm. */
#ifndef REFERENCE_MM
#define REFERENCE_MM 300.0
#endif
/* Sizes are fractions of the reference length. */
#ifndef CARD_WIDTH
#define CARD_WIDTH 0.50
#endif
/* Field width. The reason for this default is not written down yet. */
#ifndef FIELD_WIDTH
#define FIELD_WIDTH 0.42
#endif
/* Field size. The reason for this default is not written down yet. */
/* The smallest a prompt that is too wide for the field is set, as a fraction of the field's text size. */
#ifndef LABEL_MIN_SHRINK
#define LABEL_MIN_SHRINK 0.6
#endif
#ifndef FIELD_SIZE
#define FIELD_SIZE 0.040
#endif
/* Field row. The reason for this default is not written down yet. */
#ifndef FIELD_ROW
#define FIELD_ROW 0.075
#endif
/* User size. The reason for this default is not written down yet. */
#ifndef USER_SIZE
#define USER_SIZE 0.022
#endif
/* Above the password line, when the username is shown */
#ifndef USER_ROOM
#define USER_ROOM 0.062
#endif
/* Between the username's baseline and the password line */
#ifndef USER_GAP
#define USER_GAP 0.020
#endif
/* Message size. The reason for this default is not written down yet. */
#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 0.019
#endif
/* From the password line to the message baseline */
#ifndef MESSAGE_OFFSET
#define MESSAGE_OFFSET 0.040
#endif
/* How many lines a message may have (a lockout says what happened, how long, and what to do). */
#ifndef MESSAGE_LINES
#define MESSAGE_LINES 3
#endif
/* The least important line: small, and far from the rest */
#ifndef LAYOUT_SIZE
#define LAYOUT_SIZE 0.016
#endif
/* From the message baseline to the layout baseline */
#ifndef LAYOUT_OFFSET
#define LAYOUT_OFFSET 0.130
#endif
/* From the typed text to the layout found */
#ifndef LAYOUT_RESULT_OFFSET
#define LAYOUT_RESULT_OFFSET 0.040
#endif
/* Bottom margin. The reason for this default is not written down yet. */
#ifndef BOTTOM_MARGIN
#define BOTTOM_MARGIN 0.020
#endif
/* Line width. The reason for this default is not written down yet. */
#ifndef LINE_WIDTH
#define LINE_WIDTH 0.0016
#endif
/* Dot radius. The reason for this default is not written down yet. */
#ifndef DOT_RADIUS
#define DOT_RADIUS 0.0042
#endif
/* Dot pitch. The reason for this default is not written down yet. */
#ifndef DOT_PITCH
#define DOT_PITCH 0.020
#endif

/* A zero or negative limit would make the code unsafe or useless. */
_Static_assert(PAMTEXT_MAX >= 32, "PAMTEXT_MAX must hold a short sentence");
_Static_assert(AUTH_PASSWORD_MAX > 0, "AUTH_PASSWORD_MAX must be positive");
_Static_assert(MAX_SCALE > 0, "MAX_SCALE must be positive");
_Static_assert(DEFAULT_REPEAT_RATE > 0, "DEFAULT_REPEAT_RATE must be positive");
_Static_assert(DEFAULT_REPEAT_DELAY > 0, "DEFAULT_REPEAT_DELAY must be positive");
_Static_assert(DISPLAY_MAX_EXTRA_FDS > 0, "DISPLAY_MAX_EXTRA_FDS must be positive");
_Static_assert(MAX_LAYOUTS > 0, "MAX_LAYOUTS must be positive");
_Static_assert(LIST_MAX > 0, "LIST_MAX must be positive");
_Static_assert(REFERENCE_MM > 0, "REFERENCE_MM must be positive");
_Static_assert(CARD_WIDTH > 0, "CARD_WIDTH must be positive");
_Static_assert(FIELD_WIDTH > 0, "FIELD_WIDTH must be positive");
_Static_assert(LABEL_MIN_SHRINK > 0 && LABEL_MIN_SHRINK <= 1, "LABEL_MIN_SHRINK must be above 0 and at most 1");
_Static_assert(FIELD_SIZE > 0, "FIELD_SIZE must be positive");
_Static_assert(FIELD_ROW > 0, "FIELD_ROW must be positive");
_Static_assert(USER_SIZE > 0, "USER_SIZE must be positive");
_Static_assert(USER_ROOM > 0, "USER_ROOM must be positive");
_Static_assert(USER_GAP > 0, "USER_GAP must be positive");
_Static_assert(MESSAGE_SIZE > 0, "MESSAGE_SIZE must be positive");
_Static_assert(MESSAGE_LINES >= 2 && MESSAGE_LINES <= 4, "MESSAGE_LINES must be 2 to 4");
_Static_assert(MESSAGE_OFFSET > 0, "MESSAGE_OFFSET must be positive");
_Static_assert(LAYOUT_SIZE > 0, "LAYOUT_SIZE must be positive");
_Static_assert(LAYOUT_OFFSET > 0, "LAYOUT_OFFSET must be positive");
_Static_assert(LAYOUT_RESULT_OFFSET > 0, "LAYOUT_RESULT_OFFSET must be positive");
_Static_assert(BOTTOM_MARGIN > 0, "BOTTOM_MARGIN must be positive");
_Static_assert(LINE_WIDTH > 0, "LINE_WIDTH must be positive");
_Static_assert(DOT_RADIUS > 0, "DOT_RADIUS must be positive");
_Static_assert(DOT_PITCH > 0, "DOT_PITCH must be positive");
_Static_assert(SPEECH_UNLOCK_WAIT_MS > 0, "SPEECH_UNLOCK_WAIT_MS must be positive");

_Static_assert(HUSH_TIMEOUT_MS > 0, "HUSH_TIMEOUT_MS must be positive");
_Static_assert(HUSH_OUTPUT_MAX > 1, "HUSH_OUTPUT_MAX must be more than 1");
_Static_assert(HUSH_MAX_STREAMS > 0, "HUSH_MAX_STREAMS must be positive");
_Static_assert(HUSH_MAX_PLAYERS > 0, "HUSH_MAX_PLAYERS must be positive");
_Static_assert(HUSH_ENV_MAX > 2, "HUSH_ENV_MAX must be more than 2");
_Static_assert(HUSH_NAME_MAX > 24, "HUSH_NAME_MAX must hold a player's name");

#endif
