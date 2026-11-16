#ifndef PAMTEXT_H
#define PAMTEXT_H

#include <stdbool.h>
#include <stddef.h>

/* What PAM says while it checks a password, besides asking for it. pam_faillock, when an account is
 * locked, says "The account is locked due to 3 failed logins. (9 minutes left to unlock)" as an
 * error message, and still answers "authentication failed", as for a wrong password: the text is
 * the only sign that the wait is not a wrong password. It is in the system's language, not ours. */

/* Adds a message PAM sent to info (size bytes, PAMTEXT_MAX is enough). Only error messages
 * (PAM_ERROR_MSG) are kept; control characters become spaces, and what does not fit is cut. */
void pamtext_add(char *info, size_t size, int style, const char *text);

/* A hint for the screen, not a decision: nothing is done with it, only shown, so another module's "(...)" is
 * shown as it is. The part of info between the last pair of parentheses, which is where pam_faillock puts the
 * time left ("9 minutes left to unlock"), written into out. False if there is none, or it is
 * empty. */
bool pamtext_hint(const char *info, char *out, size_t size);

#endif
