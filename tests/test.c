/* Unit tests for the parts that need no Wayland connection or PAM. */
#define _GNU_SOURCE
#include "compose.h"
#include "pamtext.h"
#include "config_file.h"
#include "field.h"
#include "../src/announce.h"
#include "../src/hush.h"
#include "../src/layout.h"
#include "../src/settings.h"
#include "../src/speech.h"
#include <dirent.h>
#include <locale.h>
#include "length.h"
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

static int checks, failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    checks++; \
    if (strcmp((actual), (expected)) != 0) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    } \
} while (0)

static void test_field(void) {
    Field f = {0};
    CHECK(field_append(&f, "a"));
    CHECK(field_append(&f, "ã"));
    CHECK(field_chars(&f) == 2);
    CHECK(f.len == 3);
    field_backspace(&f);
    CHECK_STR(f.text, "a");
    field_clear(&f);
    CHECK(field_chars(&f) == 0 && f.text[0] == '\0');

    /* Full: refused, and left as it was. */
    for (size_t i = 0; i < FIELD_MAX - 1; i++) CHECK(field_append(&f, "x"));
    CHECK(!field_append(&f, "y"));
    CHECK(f.len == FIELD_MAX - 1);
    field_clear(&f);
}

static void test_settings(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(!s.high_contrast && s.text_scale == 1);

    CHECK(settings_set("contrast", "high", &s) && s.high_contrast);
    CHECK(!settings_set("contrast", "loud", &s));
    CHECK(settings_set("text_scale", "1.5", &s) && s.text_scale == 1.5);
    CHECK(!settings_set("text_scale", "9", &s) && s.text_scale == 1.5);
    CHECK(!settings_set("text_scale", "nan", &s) && s.text_scale == 1.5);
    CHECK(!s.has_text_size_min && !s.has_text_size_max);
    CHECK(settings_set("text_size_min", "3mm", &s) && s.has_text_size_min && s.text_size_min.value == 3 &&
          s.text_size_min.unit == PAC_LENGTH_MM);
    CHECK(settings_set("text_size_max", "40 %", &s) && s.has_text_size_max && s.text_size_max.unit == PAC_LENGTH_PERCENT);
    CHECK(!settings_set("text_size_min", "3", &s) && s.text_size_min.value == 3 && s.has_text_size_min);   /* no unit: refused, old value kept */
    CHECK(!settings_set("text_size_max", "-1px", &s) && s.text_size_max.unit == PAC_LENGTH_PERCENT);

    /* resources: three levels, and nothing else changes with them */
    Settings r;
    settings_defaults(&r);
    CHECK(r.resources == RESOURCES_NORMAL);
    CHECK(settings_set("resources", "low", &r) && r.resources == RESOURCES_LOW);
    CHECK(settings_set("resources", "minimal", &r) && r.resources == RESOURCES_MINIMAL);
    CHECK(!settings_set("resources", "extreme", &r) && !settings_set("resources", "", &r) && r.resources == RESOURCES_MINIMAL);
    CHECK(!settings_set("minimal", "yes", &r));   /* it was the same as resources = minimal */
    CHECK(r.high_contrast == false && r.speech && r.text_scale == 1);
    settings_defaults(&r);
    /* privacy: four levels; medium sets what pause_players and mute_streams default to */
    settings_defaults(&r);
    CHECK(r.privacy == PRIVACY_NORMAL && !settings_pause_players(&r) && !settings_mute_streams(&r));
    CHECK(settings_set("privacy", "medium", &r) && r.privacy == PRIVACY_MEDIUM);
    CHECK(settings_pause_players(&r) && settings_mute_streams(&r));
    CHECK(settings_set("pause_players", "no", &r) && !settings_pause_players(&r) && settings_mute_streams(&r));   /* what is set wins */
    CHECK(settings_set("privacy", "high", &r) && r.privacy == PRIVACY_HIGH && settings_mute_streams(&r));
    CHECK(settings_set("privacy", "paranoid", &r) && r.privacy == PRIVACY_PARANOID && settings_mute_streams(&r));
    /* privacy prevails over resources: what privacy asks for is done at every resources level */
    settings_defaults(&r);
    CHECK(settings_set("privacy", "medium", &r) && settings_set("resources", "minimal", &r));
    CHECK(settings_pause_players(&r) && settings_mute_streams(&r));
    CHECK(settings_set("resources", "low", &r) && settings_pause_players(&r) && settings_mute_streams(&r));
    CHECK(settings_set("privacy", "paranoid", &r) && settings_mute_streams(&r) && r.resources == RESOURCES_LOW);
    settings_defaults(&r);
    CHECK(settings_set("resources", "minimal", &r) && !settings_pause_players(&r) && !settings_mute_streams(&r));   /* no privacy asked */
    CHECK(settings_set("privacy", "medium", &r));
    CHECK(settings_set("resources", "normal", &r));
    CHECK(settings_set("privacy", "normal", &r) && !settings_mute_streams(&r));
    CHECK(settings_set("mute_streams", "yes", &r) && settings_mute_streams(&r));
    CHECK(!settings_set("privacy", "extreme", &r) && !settings_set("privacy", "", &r) && r.privacy == PRIVACY_NORMAL);
    CHECK(r.high_contrast == false && r.speech && r.text_scale == 1 && r.resources == RESOURCES_NORMAL);   /* nothing else changes */
    settings_defaults(&r);
    CHECK(!s.show_user && settings_set("show_user", "yes", &s) && s.show_user);
    CHECK(settings_set("show_user", "No", &s) && !s.show_user && settings_set("show_user", "ON", &s) && s.show_user);   /* the shared check's rule: any case */
    CHECK(settings_set("show_user", "off", &s) && !s.show_user);
    CHECK(!settings_set("show_user", "maybe", &s));
    CHECK(settings_set("language", "pt", &s));
    CHECK_STR(s.language, "pt");
    CHECK(!settings_set("nothing", "x", &s));
}

static void test_config(void) {
    char path[] = "/tmp/paclock-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char *text = "# a comment\ncontrast = high  # trailing\n\nbad line\ntext_scale = 2\nunknown = 1\n";
    CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);

    Settings s;
    settings_defaults(&s);
    CHECK(pac_config_load_file("paclock", path, settings_set, &s) >= 0);
    CHECK(s.high_contrast && s.text_scale == 2);
    unlink(path);
    CHECK(pac_config_load_file("paclock", path, settings_set, &s) < 0);
}

/* Every key the file accepts must appear in the example, commented out or not. */
static bool conf_has_key(const char *path, const char *key) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '#') p++;
        size_t n = strcspn(p, " \t=\n");
        if (n == strlen(key) && strncmp(p, key, n) == 0) found = true;
    }
    fclose(f);
    return found;
}

static View announce_view(const char *layout, bool focus, const char *filter, const char *found, const char *message) {
    return (View){ .password_label = "Password", .layout = layout, .layout_focus = focus,
                   .layout_filter = filter, .layout_found = found, .message = message };
}

static void test_announce(void) {
    Announcer st = {0};
    char out[512];
    View v = announce_view("English keyboard", false, "", NULL, NULL);

    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Enter your password");  /* the start */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "");                    /* nothing new */

    v = announce_view("English keyboard", true, "", NULL, NULL);                        /* Tab */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "English keyboard. Type to change it");
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "");

    v = announce_view("English keyboard", true, "por", "Portuguese keyboard", NULL);   /* typed */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Portuguese keyboard");
    v = announce_view("English keyboard", true, "xyz", NULL, NULL);                     /* nothing */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "No keyboard matches");

    v = announce_view("English keyboard", false, "", NULL, NULL);                       /* Tab or Esc: nothing picked */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "English keyboard. Enter your password");
    v = announce_view("English keyboard", true, "", NULL, NULL);
    announce_changes(&st, &v, out, sizeof(out));
    v = announce_view("Portuguese keyboard", false, "", NULL, NULL);                    /* Enter: picked */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Portuguese keyboard. Enter your password");

    v = announce_view("Portuguese keyboard", false, "", NULL, "Authentication failed");
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Authentication failed");
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "");                    /* said once */
    v = announce_view("Portuguese keyboard", true, "", NULL, "Authentication failed");  /* Tab, message stays */
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Portuguese keyboard. Type to change it");

    /* A refused password comes with the keyboard in use; other errors do not. */
    v = announce_view("Portuguese keyboard", false, "", NULL, "");
    announce_changes(&st, &v, out, sizeof(out));
    v = announce_view("Portuguese keyboard", false, "", NULL, "Authentication failed");
    v.message_is_denial = true;
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Authentication failed. Portuguese keyboard. Enter your password");
    v = announce_view("Portuguese keyboard", false, "", NULL, "Too many failed attempts");
    announce_changes(&st, &v, out, sizeof(out)); CHECK_STR(out, "Too many failed attempts");

    /* No layout to choose: only the field and the messages. */
    Announcer one = {0};
    v = announce_view(NULL, false, "", NULL, "Caps Lock is on");
    announce_changes(&one, &v, out, sizeof(out)); CHECK_STR(out, "Enter your password. Caps Lock is on");
    /* A line break on the screen is a space aloud: the sentences already end in their stops. */
    v = announce_view(NULL, false, "", NULL, "Caps Lock is on:\nYour password might not be accepted.");
    announce_changes(&one, &v, out, sizeof(out)); CHECK_STR(out, "Caps Lock is on: Your password might not be accepted.");

    char tiny[8];                                                                       /* cut, never overrun */
    Announcer cut = {0};
    v = announce_view(NULL, false, "", NULL, NULL);
    announce_changes(&cut, &v, tiny, sizeof(tiny)); CHECK_STR(tiny, "Enter y");
}

static void test_speech(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(s.speech);
    CHECK(settings_set("speech", "no", &s) && !s.speech);
    CHECK(settings_set("speech", "yes", &s) && s.speech);
    CHECK(!settings_set("speech", "maybe", &s) && s.speech);
}

static void test_hush(void) {
    Settings s;
    settings_defaults(&s);
    CHECK(!s.lock_hint && !s.pause_players && !s.mute_streams);   /* off until asked for */
    CHECK(settings_set("lock_hint", "yes", &s) && s.lock_hint);
    CHECK(settings_set("pause_players", "on", &s) && s.pause_players);
    CHECK(settings_set("mute_streams", "1", &s) && s.mute_streams);
    CHECK(!settings_set("mute_streams", "maybe", &s) && s.mute_streams);

    const char *pactl =
        "Sink Input #7\n\tMute: no\n\tProperties:\n\t\tapplication.name = \"Firefox\"\n"
        "Sink Input #8\n\tMute: yes\n\tProperties:\n\t\tapplication.name = \"mpv\"\n"
        "Sink Input #9\n\tMute: no\n\tProperties:\n\t\tapplication.name = \"speech-dispatcher-espeak-ng\"\n"
        "Sink Input #10\n\tMute: no\n\tProperties:\n\t\tapplication.process.binary = \"sd_piper\"\n"
        "Sink Input #11\n\tMute: no\n";
    int idx[8];
    CHECK(hush_loud_streams(pactl, idx, 8) == 2);        /* not the muted, not the speech */
    CHECK(idx[0] == 7 && idx[1] == 11);
    CHECK(hush_loud_streams(pactl, idx, 1) == 1 && idx[0] == 7);   /* never past max */
    CHECK(hush_loud_streams("", idx, 8) == 0);
    CHECK(hush_loud_streams("Sink Input #abc\n\tMute: no\nSink Input #-3\n", idx, 8) == 0);   /* hostile */
    CHECK(hush_loud_streams("Sink Input #99999999999999999999\n", idx, 8) == 0);
    CHECK(hush_loud_streams("Sink Input #5\n\tMute: no", idx, 8) == 1 && idx[0] == 5);   /* no final newline */

    const char *list =
        "org.freedesktop.DBus      1416 systemd  me :1.0 -  -\n"
        "org.mpris.MediaPlayer2.firefox.instance_1_315  7381 firefox me :1.315 - - \n"
        ":1.12                     100 x me :1.12 - -\n"
        "org.mpris.MediaPlayer2.mpv 55 mpv me :1.20 - -\n"
        "org.mpris.MediaPlayer2.bad;name 1 x\n"
        "org.mpris.MediaPlayer2.\n";
    char names[4][HUSH_NAME_MAX];
    CHECK(hush_players(list, names, 4) == 2);
    CHECK_STR(names[0], "org.mpris.MediaPlayer2.firefox.instance_1_315");
    CHECK_STR(names[1], "org.mpris.MediaPlayer2.mpv");
    CHECK(hush_players(list, names, 1) == 1);
    char longname[HUSH_NAME_MAX + 40];
    snprintf(longname, sizeof(longname), "org.mpris.MediaPlayer2.%0*d 1 x\n", HUSH_NAME_MAX, 1);
    CHECK(hush_players(longname, names, 4) == 0);        /* too long to keep */

    CHECK(hush_is_playing("s \"Playing\"\n"));
    CHECK(!hush_is_playing("s \"Paused\"\n") && !hush_is_playing(""));
}

/* The compose table is made on the first key that may start a sequence: no sequence of any installed
 * Compose file may start with another one. */
static void test_compose_start(void) {
    CHECK(compose_may_start(XKB_KEY_dead_acute) && compose_may_start(XKB_KEY_dead_greek));
    CHECK(compose_may_start(XKB_KEY_Multi_key) && compose_may_start(XKB_KEY_backslash));
    CHECK(compose_may_start(0x01000000 + 0x17fb) && compose_may_start(XKB_KEY_ch));
    CHECK(!compose_may_start('a') && !compose_may_start(XKB_KEY_space) && !compose_may_start(XKB_KEY_BackSpace));
    CHECK(!compose_may_start(XKB_KEY_Return) && !compose_may_start(XKB_KEY_Shift_L) && !compose_may_start(XKB_KEY_agrave));

    DIR *d = opendir("/usr/share/X11/locale");
    if (!d) return;   /* no libX11 locale data here: nothing to check against */
    int files = 0, sequences = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        char path[512], line[512];
        snprintf(path, sizeof path, "/usr/share/X11/locale/%s/Compose", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        files++;
        while (fgets(line, sizeof line, f)) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '<') continue;   /* a comment, an include, a blank line */
            char name[64];
            size_t n = 0;
            for (p++; *p && *p != '>' && n < sizeof name - 1; p++) name[n++] = *p;
            name[n] = '\0';
            xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
            if (sym == XKB_KEY_NoSymbol) continue;
            sequences++;
            if (!compose_may_start(sym)) { CHECK(0 && "a Compose sequence starts with a key compose_may_start refuses"); fprintf(stderr, "%s: <%s>\n", path, name); }
        }
        fclose(f);
    }
    closedir(d);
    CHECK(files == 0 || sequences > 0);
}

static void test_pamtext(void) {
    char info[PAMTEXT_MAX] = "", hint[PAMTEXT_MAX];

    /* Only error messages are kept, and a prompt or an info line is not what a locked account says. */
    pamtext_add(info, sizeof info, PAM_PROMPT_ECHO_OFF, "Password: ");
    pamtext_add(info, sizeof info, PAM_TEXT_INFO, "Welcome");
    CHECK_STR(info, "");
    pamtext_add(info, sizeof info, PAM_ERROR_MSG, "The account is locked due to 3 failed logins.");
    pamtext_add(info, sizeof info, PAM_ERROR_MSG, "(9 minutes left to unlock)");
    CHECK_STR(info, "The account is locked due to 3 failed logins. (9 minutes left to unlock)");
    CHECK(pamtext_hint(info, hint, sizeof hint));
    CHECK_STR(hint, "9 minutes left to unlock");

    CHECK(!pamtext_hint("The account is locked.", hint, sizeof hint) && hint[0] == '\0');
    CHECK(!pamtext_hint("", hint, sizeof hint) && !pamtext_hint("() x", hint, sizeof hint));
    CHECK(!pamtext_hint(") (", hint, sizeof hint) && !pamtext_hint("x (unclosed", hint, sizeof hint));
    CHECK(pamtext_hint("a (one) b (two)", hint, sizeof hint));
    CHECK_STR(hint, "two");                                   /* the last pair */
    CHECK(pamtext_hint("(un (nested)", hint, sizeof hint));
    CHECK_STR(hint, "nested");

    /* Control characters (a line break, an escape) are not kept, and what does not fit is cut. */
    char small[16] = "";
    pamtext_add(small, sizeof small, PAM_ERROR_MSG, "line\nbreak\033[31m and a very long rest of the text");
    CHECK(strlen(small) == sizeof small - 1 && !strchr(small, '\n') && !strchr(small, 27));
    CHECK(strncmp(small, "line break [31m", 15) == 0);
    pamtext_add(small, sizeof small, PAM_ERROR_MSG, "more");  /* full: nothing past the end */
    CHECK(strlen(small) == sizeof small - 1);
    pamtext_add(small, 0, PAM_ERROR_MSG, "x");                 /* no room at all: no crash */
    pamtext_add(info, sizeof info, PAM_ERROR_MSG, NULL);
    char big[PAMTEXT_MAX] = "";
    for (int i = 0; i < 50; i++) pamtext_add(big, sizeof big, PAM_ERROR_MSG, "0123456789");
    CHECK(strlen(big) == sizeof big - 1);
    CHECK(pamtext_hint("(0123456789012345678901234567890123456789)", hint, 16) && strlen(hint) == 15);   /* cut to fit */
}

static void test_layout_spec(void) {
    Layout l;
    CHECK(layout_parse_spec("us(intl)", &l));
    CHECK_STR(l.layout, "us"); CHECK_STR(l.variant, "intl"); CHECK_STR(l.name, "us(intl)");
    CHECK(layout_parse_spec("us (intl)", &l));           /* a space before the variant */
    CHECK_STR(l.layout, "us"); CHECK_STR(l.variant, "intl"); CHECK_STR(l.name, "us (intl)");
    CHECK(layout_parse_spec("pt   ", &l));                /* no trailing space in the name */
    CHECK_STR(l.layout, "pt"); CHECK_STR(l.variant, ""); CHECK_STR(l.name, "pt");
    CHECK(layout_parse_spec("  de(nodeadkeys)  ", &l));
    CHECK_STR(l.variant, "nodeadkeys"); CHECK_STR(l.name, "de(nodeadkeys)");
    CHECK(!layout_parse_spec("", &l) && !layout_parse_spec("us(", &l) && !layout_parse_spec("us()", &l));
    CHECK(!layout_parse_spec("(intl)", &l));
}

/* A comma-decimal locale (LC_NUMERIC "1,5"): the settings must still be read as written in the
 * file. The Makefile builds one into build/locale-test (LOCPATH); without localedef it is skipped. */
static void test_comma_locale(void) {
    static const char *const names[] = { "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "pt_BR.UTF-8" };
    bool set = false;
    for (size_t i = 0; i < sizeof names / sizeof names[0] && !set; i++)
        set = setlocale(LC_ALL, names[i]) && localeconv()->decimal_point[0] == ',';
    if (!set) { printf("skipped: no comma-decimal locale (LOCPATH)\n"); setlocale(LC_ALL, "C"); return; }
    Settings s;
    settings_defaults(&s);
    CHECK(settings_set("text_scale", "1.5", &s) && s.text_scale == 1.5);
    PacLength len;
    CHECK(pac_length_parse("3.5mm", &len));
    CHECK(!pac_length_parse("3,5mm", &len));
    setlocale(LC_ALL, "C");
}

int main(void) {
    size_t nkeys;
    const char *const *keys = settings_keys(&nkeys);
    for (size_t i = 0; i < nkeys; i++)
        CHECK(conf_has_key("paclock.conf", keys[i]));   /* every option is documented */

    test_field();
    test_settings();
    test_comma_locale();
    test_compose_start();
    test_pamtext();
    test_speech();
    test_hush();
    test_layout_spec();
    test_announce();
    test_config();
    if (failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}
