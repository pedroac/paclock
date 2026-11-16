#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include "length.h"

/* Everything the config file and the command line can set. */
typedef struct {
    /* the pac tools have these in common */
    bool high_contrast;         /* contrast = high: black and white, nothing dimmed */
    double text_scale;
    bool has_text_size_min, has_text_size_max;   /* limits on the smallest text, however large the scale or small the screen */
    PacLength text_size_min, text_size_max;
    char font[256], font_light[256];
    bool show_user;             /* show the username above the password */
    enum { RESOURCES_NORMAL, RESOURCES_LOW, RESOURCES_MINIMAL } resources;   /* low: no username or layout choice; minimal: also a small card */
    /* privacy = normal, medium, high or paranoid; each level includes the ones before */
    enum { PRIVACY_NORMAL, PRIVACY_MEDIUM, PRIVACY_HIGH, PRIVACY_PARANOID } privacy;
    char layouts[512];          /* more keyboard layouts to offer: "pt, us(intl)"; empty: the compositor's only */
    bool speech;                /* speech = no: do not speak the messages (when built with speech) */
    bool lock_hint;             /* lock_hint = yes: tell logind the session is locked (see hush.h) */
    bool pause_players;         /* pause_players = yes: pause the media players while locked */
    bool mute_streams;          /* mute_streams = yes: mute other programs' sound while locked */
    bool has_pause_players, has_mute_streams;   /* set: the setting beats the privacy level */
    char language[64];          /* language = pt: the texts' language; empty: the environment's */
} Settings;

/* The keys the file may set, for the test that keeps paclock.conf complete. */
const char *const *settings_keys(size_t *count);

void settings_defaults(Settings *s);
/* A ConfigHandler: sets one key. Also what the command-line options go through. */
bool settings_set(const char *key, const char *value, void *data);

/* What is set, else what the privacy level means: medium and above pause the players and mute
 * the streams. */
bool settings_pause_players(const Settings *s);
bool settings_mute_streams(const Settings *s);


#endif
