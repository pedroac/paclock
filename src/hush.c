#define _GNU_SOURCE
#include "hush.h"
#include "i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER "org.mpris.MediaPlayer2.Player"

/* ---- the parsers ---- */

static bool starts(const char *s, const char *prefix) {
    return !strncmp(s, prefix, strlen(prefix));
}

/* Adds the stream just read, if it is one to mute. */
static int keep(int n, int *index, int max, long stream, bool muted, bool speech) {
    if (stream >= 0 && !muted && !speech && n < max) index[n++] = (int)stream;
    return n;
}

int hush_loud_streams(const char *text, int *index, int max) {
    int n = 0;
    long current = -1;
    bool muted = false, speech = false;
    for (const char *line = text; line && *line; ) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (starts(p, "Sink Input #")) {
            n = keep(n, index, max, current, muted, speech);
            char *end;
            current = strtol(p + strlen("Sink Input #"), &end, 10);
            if (end == p + strlen("Sink Input #") || current < 0 || current > 0x7fffffff) current = -1;
            muted = speech = false;
        } else if (starts(p, "Mute: ")) {
            muted = starts(p + strlen("Mute: "), "yes");
        } else if (starts(p, "application.process.binary = \"sd_") ||
                   starts(p, "application.name = \"speech-dispatcher")) {
            speech = true;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return keep(n, index, max, current, muted, speech);
}

/* The name is an argument to another program: only what a bus name is made of. */
static bool is_bus_name(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

int hush_players(const char *text, char names[][HUSH_NAME_MAX], int max) {
    int n = 0;
    for (const char *line = text; line && *line && n < max; ) {
        size_t len = strcspn(line, " \t\n");
        if (len > strlen(MPRIS_PREFIX) && len < HUSH_NAME_MAX && starts(line, MPRIS_PREFIX) && is_bus_name(line, len)) {
            memcpy(names[n], line, len);
            names[n][len] = '\0';
            n++;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return n;
}

bool hush_is_playing(const char *text) {
    return strstr(text, "\"Playing\"") != NULL;
}

/* ---- running a program ---- */

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* Said once for each program (busctl, pactl) that is not there. */
static void missing(const char *program) {
    static bool said[2];
    bool *once = &said[strcmp(program, "busctl") ? 1 : 0];
    if (*once) return;
    *once = true;
    fprintf(stderr, _("paclock: cannot run %s, so that part is skipped\n"), program);
}

/* Runs argv (found in PATH, no shell) with the environment envp (NULL: ours), with out filled with
 * what it prints, cut to fit, and ended with a NUL. Killed if it takes longer than HUSH_TIMEOUT_MS.
 * True if it ended with status 0. */
static bool run(char *const argv[], char *const envp[], char *out, size_t size) {
    out[0] = '\0';
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) < 0) return false;

    /* The child must not inherit the blocked SIGINT and SIGTERM, or it could not be stopped. */
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fd[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    pid_t pid;
    int err = posix_spawnp(&pid, argv[0], &actions, &attr, argv, envp ? envp : environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(fd[1]);
    if (err) {
        close(fd[0]);
        if (err == ENOENT) missing(argv[0]);
        return false;
    }

    size_t len = 0;
    long deadline = now_ms() + HUSH_TIMEOUT_MS;
    bool stopped = false;   /* timed out or failed: the child is killed, so that waiting ends */
    for (;;) {
        long left = deadline - now_ms();
        struct pollfd p = { .fd = fd[0], .events = POLLIN };
        int ready = left > 0 ? poll(&p, 1, (int)left) : 0;
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { stopped = true; break; }   /* a timeout, or nothing more will come */
        char chunk[4096];
        ssize_t got = read(fd[0], chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { stopped = got < 0; break; }  /* the end, or a read error */
        if (len + 1 < size) {
            size_t take = (size_t)got < size - 1 - len ? (size_t)got : size - 1 - len;
            memcpy(out + len, chunk, take);
            len += take;
        }                                    /* what does not fit is read and dropped */
    }
    out[len] = '\0';
    close(fd[0]);
    if (stopped) kill(pid, SIGKILL);
    int status = 0;
    pid_t waited;
    while ((waited = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    return !stopped && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* For the calls whose output is not wanted. */
static char sink[16];

/* ---- what is done ---- */

static void set_hint(Hush *h, bool locked) {
    char *const argv[] = { "busctl", "--system", "call", "org.freedesktop.login1",
        "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session", "SetLockedHint",
        "b", locked ? "true" : "false", NULL };
    if (run(argv, NULL, sink, sizeof(sink))) h->hinted = locked;
}

static void player_call(const char *name, const char *method) {
    char *const argv[] = { "busctl", "--user", "call", (char *)name, MPRIS_PATH, MPRIS_PLAYER, (char *)method, NULL };
    run(argv, NULL, sink, sizeof(sink));
}

static void pause_players(Hush *h) {
    char *output = malloc(HUSH_OUTPUT_MAX);
    if (!output) return;
    char *const list[] = { "busctl", "--user", "--acquired", "--no-legend", "--no-pager", "list", NULL };
    if (!run(list, NULL, output, HUSH_OUTPUT_MAX)) { free(output); return; }
    char names[HUSH_MAX_PLAYERS][HUSH_NAME_MAX];
    int count = hush_players(output, names, HUSH_MAX_PLAYERS);
    for (int i = 0; i < count; i++) {
        char *const get[] = { "busctl", "--user", "get-property", names[i], MPRIS_PATH, MPRIS_PLAYER, "PlaybackStatus", NULL };
        if (!run(get, NULL, output, HUSH_OUTPUT_MAX) || !hush_is_playing(output)) continue;
        player_call(names[i], "Pause");
        memcpy(h->paused[h->paused_count++], names[i], HUSH_NAME_MAX);
    }
    free(output);
}

static void mute_stream(int index, bool mute) {
    char number[16];
    snprintf(number, sizeof(number), "%d", index);
    char *const argv[] = { "pactl", "set-sink-input-mute", number, mute ? "1" : "0", NULL };
    run(argv, NULL, sink, sizeof(sink));
}

/* Our environment with LC_ALL=C: pactl's words are translated, and LANGUAGE (set for paclock's own
 * texts) would be inherited. Entries past HUSH_ENV_MAX are left out. */
static char **c_environment(void) {
    static char *env[HUSH_ENV_MAX];
    size_t n = 0;
    env[n++] = "LC_ALL=C";
    for (char **e = environ; *e && n + 1 < HUSH_ENV_MAX; e++)
        if (strncmp(*e, "LC_ALL=", 7) && strncmp(*e, "LANGUAGE=", 9)) env[n++] = *e;
    env[n] = NULL;
    return env;
}

static void mute_streams(Hush *h) {
    char *output = malloc(HUSH_OUTPUT_MAX);
    if (!output) return;
    char *const list[] = { "pactl", "list", "sink-inputs", NULL };
    if (!run(list, c_environment(), output, HUSH_OUTPUT_MAX)) { free(output); return; }
    h->muted_count = hush_loud_streams(output, h->muted, HUSH_MAX_STREAMS);
    free(output);
    for (int i = 0; i < h->muted_count; i++) mute_stream(h->muted[i], true);
}

void hush_start(Hush *h, const Settings *s) {
    memset(h, 0, sizeof(*h));
    if (s->lock_hint) set_hint(h, true);
    if (settings_pause_players(s)) pause_players(h);
    if (settings_mute_streams(s)) mute_streams(h);
}

void hush_stop(Hush *h) {
    for (int i = 0; i < h->muted_count; i++) mute_stream(h->muted[i], false);
    for (int i = 0; i < h->paused_count; i++) player_call(h->paused[i], "Play");
    if (h->hinted) set_hint(h, false);
    memset(h, 0, sizeof(*h));
}
