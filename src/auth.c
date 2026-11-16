#define _GNU_SOURCE
#include "auth.h"
#include "config_file.h"
#include <limits.h>
#include <security/pam_appl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define SERVICE "paclock"

static bool service_exists(const char *name) {
    char path[PATH_MAX];
    const char *dirs[] = { SYSCONFDIR "/pam.d/", "/usr/lib/pam.d/" };
    for (size_t i = 0; i < 2; i++)
        if (snprintf(path, sizeof(path), "%s%s", dirs[i], name) < (int)sizeof(path) && access(path, R_OK) == 0)
            return true;
    return false;
}

/* The service file must exist, and so must every stack it includes (system-auth on Arch and
 * Fedora, common-auth on Debian): PAM refuses every password when one is missing. */
bool auth_service_installed(void) {
    if (!service_exists(SERVICE)) return false;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), SYSCONFDIR "/pam.d/" SERVICE);
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(path, sizeof(path), "/usr/lib/pam.d/" SERVICE);
        f = fopen(path, "r");
    }
    if (!f) return false;
    bool ok = true;
    char line[512];
    while (ok && fgets(line, sizeof(line), f)) {
        char type[32], control[32], target[256];
        if (line[0] == '#' || sscanf(line, "%31s %31s %255s", type, control, target) != 3) continue;
        if ((strcmp(control, "include") == 0 || strcmp(control, "substack") == 0) && target[0] != '/')
            ok = service_exists(target);
    }
    fclose(f);
    return ok;
}

/* PAM asks; the only thing here that can answer is the password. Anything else (a one-time
 * code, say) is refused, which fails the check. */
struct talk {
    const char *password;
    char *info;      /* what PAM says besides asking */
};

static int converse(int count, const struct pam_message **msgs, struct pam_response **out, void *data) {
    const struct talk *talk = data;
    const char *password = talk->password;
    struct pam_response *replies = calloc(count, sizeof(*replies));
    if (!replies) return PAM_BUF_ERR;

    for (int i = 0; i < count; i++) {
        pamtext_add(talk->info, PAMTEXT_MAX, msgs[i]->msg_style, msgs[i]->msg);
        if (msgs[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            replies[i].resp = strdup(password);
            if (!replies[i].resp) goto fail;
        } else if (msgs[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            goto fail;
        }
    }
    *out = replies;
    return PAM_SUCCESS;

fail:
    for (int i = 0; i < count; i++) {
        if (replies[i].resp) {
            explicit_bzero(replies[i].resp, strlen(replies[i].resp));
            free(replies[i].resp);
        }
    }
    free(replies);
    return PAM_CONV_ERR;
}

static AuthResult check(const char *user, const char *password, char *info) {
    struct talk talk = { password, info };
    struct pam_conv conv = { converse, &talk };
    pam_handle_t *pam = NULL;
    if (pam_start(SERVICE, user, &conv, &pam) != PAM_SUCCESS) return AUTH_ERROR;

    /* Only the password is checked, not the account (pam_acct_mgmt): see docs/decisions.md. */
    int status = pam_authenticate(pam, 0);
    pam_end(pam, status);

    if (status == PAM_SUCCESS) return AUTH_OK;
    if (status == PAM_MAXTRIES) return AUTH_LOCKED;
    /* A refusal with an error message of PAM's is not a wrong password: pam_faillock locks the account
     * and says so, and answers as for a wrong one. */
    if (status == PAM_AUTH_ERR && info[0]) return AUTH_LOCKED;
    /* A missing module or service is not a wrong password. */
    return status == PAM_AUTH_ERR || status == PAM_USER_UNKNOWN ||
           status == PAM_CONV_ERR || status == PAM_AUTHINFO_UNAVAIL ? AUTH_DENIED : AUTH_ERROR;
}

static void *run(void *data) {
    Auth *auth = data;
    auth->info[0] = '\0';
    auth->result = check(auth->user, auth->password, auth->info);
    explicit_bzero(auth->password, sizeof(auth->password));
    uint64_t one = 1;
    if (write(auth->wake, &one, sizeof(one)) < 0) {}
    return NULL;
}

bool auth_start(Auth *auth, const char *user, const char *password) {
    if (auth->running || strlen(user) >= sizeof(auth->user) ||
        strlen(password) >= sizeof(auth->password))
        return false;
    snprintf(auth->user, sizeof(auth->user), "%s", user);
    snprintf(auth->password, sizeof(auth->password), "%s", password);
    auth->running = true;
    if (pthread_create(&auth->thread, NULL, run, auth) != 0) {
        auth->running = false;
        explicit_bzero(auth->password, sizeof(auth->password));
        return false;
    }
    return true;
}

bool auth_finish(Auth *auth, AuthResult *result) {
    uint64_t count;
    if (read(auth->wake, &count, sizeof(count)) <= 0 || !auth->running) return false;
    pthread_join(auth->thread, NULL);
    auth->running = false;
    *result = auth->result;
    return true;
}

void auth_abort(Auth *auth) {
    if (auth->running) {
        pthread_join(auth->thread, NULL);
        auth->running = false;
    }
    explicit_bzero(auth->password, sizeof(auth->password));
}
