#ifndef AUTH_H
#define AUTH_H

#include <pthread.h>
#include <stdbool.h>
#include "pamtext.h"
#include "tunables.h"


/* Whether the password is the user's, asked of PAM (service "paclock"). PAM can take seconds
 * (a failed login is delayed on purpose), so a check runs in its own thread and wakes the
 * main loop through an eventfd. One at a time. */
typedef enum {
    AUTH_OK,
    AUTH_DENIED,     /* the password is not the user's */
    AUTH_LOCKED,     /* too many failed attempts: PAM refuses for a while, even the right password (PAM_MAXTRIES,
                      * or a refusal with an error message of PAM's, as pam_faillock sends one) */
    AUTH_ERROR,      /* PAM could not be used at all: no service, no module */
} AuthResult;

typedef struct {
    pthread_t thread;
    char user[256];
    char password[AUTH_PASSWORD_MAX];   /* wiped when the check is over */
    char info[PAMTEXT_MAX];             /* what PAM said besides asking (pamtext.h); empty if nothing */
    AuthResult result;
    bool running;
    int wake;     /* the eventfd */
} Auth;

/* Whether the PAM service file exists: without it PAM falls back to "other", which refuses
 * everyone, and a lock nobody can open must not be started. */
bool auth_service_installed(void);
/* Returns false if the thread could not be started. */
bool auth_start(Auth *auth, const char *user, const char *password);
/* Call when the eventfd is readable: collects the result of a finished check. Returns false
 * if there was nothing to collect. */
bool auth_finish(Auth *auth, AuthResult *result);
/* Waits for a check in flight and wipes the password. */
void auth_abort(Auth *auth);

#endif
