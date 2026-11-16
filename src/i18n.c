#include "i18n.h"
#ifndef NO_I18N
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOMAIN "paclock"

void i18n_init(void) {
    /* To try the catalogs of a build before installing them: make LOCALEDIR=$PWD/build/locale */
    bindtextdomain(DOMAIN, LOCALEDIR);
    bind_textdomain_codeset(DOMAIN, "UTF-8");
    textdomain(DOMAIN);
}


const char *i18n_layout_name(const char *name) {
    static bool bound;
    static const char *const domains[] = { "xkeyboard-config-2", "xkeyboard-config" };
    if (!bound) {
        for (int i = 0; i < 2; i++) bind_textdomain_codeset(domains[i], "UTF-8");
        bound = true;
    }
    for (int i = 0; i < 2; i++) {
        const char *text = dgettext(domains[i], name);
        if (text != name) return text;
    }
    return name;
}

/* A language list is names separated by ':' made of letters, digits and "_-.@". */
static bool valid(const char *s) {
    if (!*s || strlen(s) > 63) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              strchr("_-.@:", *s)))
            return false;
    return true;
}

bool i18n_set_language(const char *language) {
    if (!language[0]) return true;
    if (!valid(language)) return false;

    /* gettext reads LANGUAGE, but only when the message locale is not "C" (glibc counts C.UTF-8
     * as "C"), and a greeter often runs with no locale at all. So the message locale is the
     * first of these that is generated: the one asked for, with and without ".UTF-8", then
     * en_US.UTF-8 only to switch LANGUAGE on, which then picks the catalog by itself. */
    setenv("LANGUAGE", language, 1);
    char first[64], utf8[80];
    snprintf(first, sizeof(first), "%.*s", (int)strcspn(language, ":"), language);
    snprintf(utf8, sizeof(utf8), "%s.UTF-8", first);
    const char *tries[] = { first, utf8, "en_US.UTF-8" };
    for (size_t i = 0; i < sizeof(tries) / sizeof(*tries); i++)
        if (setlocale(LC_MESSAGES, tries[i])) return true;
    return false;
}

#endif /* NO_I18N */
