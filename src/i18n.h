#ifndef I18N_H
#define I18N_H

#include <stdbool.h>

#ifdef NO_I18N
/* I18N=0: English only, no gettext. */
#define _(text) (text)
#define N_(text) text
#define ngettext(one, many, n) ((n) == 1 ? (one) : (many))
static inline void i18n_init(void) {}
static inline const char *i18n_layout_name(const char *name) { return name; }
static inline bool i18n_set_language(const char *language) { (void)language; return true; }
#else
#include <libintl.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

/* gettext, domain "paclock". _() translates when it runs; N_() only marks a string for
 * xgettext, for the ones that are stored first and translated where they are shown. */
#define _(text) gettext(text)
#define N_(text) text

/* Binds the domain. Call once, before the first _(). */
void i18n_init(void);
/* The name of a keyboard layout as xkeyboard-config describes it, translated if it can. */
const char *i18n_layout_name(const char *name);
/* Makes texts come in LANGUAGE (e.g. "pt", "pt_BR", "pt_BR:pt") whatever the environment says;
 * empty leaves the environment's choice. Returns false if the name is not usable. */
bool i18n_set_language(const char *language);

#endif /* NO_I18N */

#endif
