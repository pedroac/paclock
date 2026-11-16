#include "settings.h"
#include "config_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const keys[] = {
    "contrast", "text_scale", "text_size_min", "text_size_max", "resources", "privacy", "show_user", "font", "font_light", "layouts", "language", "speech",
    "lock_hint", "pause_players", "mute_streams",
};

const char *const *settings_keys(size_t *count) {
    *count = sizeof keys / sizeof *keys;
    return keys;
}

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->text_scale = 1;
    s->speech = true;
}

bool settings_pause_players(const Settings *s) {
    return s->has_pause_players ? s->pause_players : s->privacy >= PRIVACY_MEDIUM;
}

bool settings_mute_streams(const Settings *s) {
    return s->has_mute_streams ? s->mute_streams : s->privacy >= PRIVACY_MEDIUM;
}

static bool copy(char *to, size_t size, const char *value) {
    if (strlen(value) >= size) return false;
    snprintf(to, size, "%s", value);
    return true;
}

bool settings_set(const char *key, const char *value, void *data) {
    Settings *s = data;

    if (!strcmp(key, "contrast")) {
        if (!strcmp(value, "normal")) s->high_contrast = false;
        else if (!strcmp(value, "high")) s->high_contrast = true;
        else return false;
    } else if (!strcmp(key, "text_scale")) {
        double v;
        if (!pac_text_scale_parse(value, &v)) return false;
        s->text_scale = v;
    } else if (!strcmp(key, "text_size_min")) {
        if (!pac_length_parse(value, &s->text_size_min)) return false;   /* a bad value leaves the old one */
        s->has_text_size_min = true;
    } else if (!strcmp(key, "text_size_max")) {
        if (!pac_length_parse(value, &s->text_size_max)) return false;
        s->has_text_size_max = true;
    } else if (!strcmp(key, "show_user")) {
        return pac_config_bool(value, &s->show_user);
    } else if (!strcmp(key, "resources")) {
        if (!strcmp(value, "normal")) s->resources = RESOURCES_NORMAL;
        else if (!strcmp(value, "low")) s->resources = RESOURCES_LOW;
        else if (!strcmp(value, "minimal")) s->resources = RESOURCES_MINIMAL;
        else return false;
    } else if (!strcmp(key, "privacy")) {
        if (!strcmp(value, "normal")) s->privacy = PRIVACY_NORMAL;
        else if (!strcmp(value, "medium")) s->privacy = PRIVACY_MEDIUM;
        else if (!strcmp(value, "high")) s->privacy = PRIVACY_HIGH;
        else if (!strcmp(value, "paranoid")) s->privacy = PRIVACY_PARANOID;
        else return false;
    } else if (!strcmp(key, "speech")) {
        return pac_config_bool(value, &s->speech);   /* accepted, and ignored, in a build without speech */
    } else if (!strcmp(key, "lock_hint")) {
        return pac_config_bool(value, &s->lock_hint);
    } else if (!strcmp(key, "pause_players")) {
        if (!pac_config_bool(value, &s->pause_players)) return false;
        s->has_pause_players = true;
    } else if (!strcmp(key, "mute_streams")) {
        if (!pac_config_bool(value, &s->mute_streams)) return false;
        s->has_mute_streams = true;
    } else if (!strcmp(key, "font")) return copy(s->font, sizeof(s->font), value);
    else if (!strcmp(key, "font_light")) return copy(s->font_light, sizeof(s->font_light), value);
    else if (!strcmp(key, "layouts")) return copy(s->layouts, sizeof(s->layouts), value);
    else if (!strcmp(key, "language")) return copy(s->language, sizeof(s->language), value);
    else return false;

    return true;
}
