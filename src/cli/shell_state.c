#include "cli/shell_state.h"

#include <stdio.h>
#include <string.h>

#define STACK_DEPTH 4

typedef struct {
    int  argc;
    char args[SHELL_MAX_ARGS][SHELL_ARG_MAX_LEN + 1];
} frame_t;

static frame_t g_stack[STACK_DEPTH];
static int     g_depth  = 0;   // number of frames currently in use
static int     g_status = 0;   // $?

// Scratch buffers for the strings returned by shell_expand — need to
// survive across the caller's use but they get invalidated on the next
// push/pop/set_status. Keep two so consecutive $? and $1 in one line
// don't overwrite each other mid-expansion.
static char g_scratch[3][SHELL_ARG_MAX_LEN + 1];
static int  g_scratch_next = 0;

static char *next_scratch(void) {
    char *s = g_scratch[g_scratch_next];
    g_scratch_next = (g_scratch_next + 1) % 3;
    return s;
}

bool shell_push_args(int argc, char **argv) {
    if (g_depth >= STACK_DEPTH) return false;
    frame_t *f = &g_stack[g_depth++];
    if (argc < 0) argc = 0;
    if (argc > SHELL_MAX_ARGS) argc = SHELL_MAX_ARGS;
    f->argc = argc;
    for (int i = 0; i < argc; i++) {
        strncpy(f->args[i], argv[i], SHELL_ARG_MAX_LEN);
        f->args[i][SHELL_ARG_MAX_LEN] = '\0';
    }
    return true;
}

void shell_pop_args(void) {
    if (g_depth > 0) g_depth--;
}

void shell_set_status(int rc) { g_status = rc; }
int  shell_get_status(void)   { return g_status; }

static const frame_t *top(void) {
    return g_depth > 0 ? &g_stack[g_depth - 1] : NULL;
}

const char *shell_expand(const char *name) {
    if (!name || !*name || name[1] != '\0') return NULL;   // single-char only
    char c = name[0];

    if (c >= '0' && c <= '9') {
        int idx = c - '0';
        const frame_t *f = top();
        if (!f || idx >= f->argc) return "";
        return f->args[idx];
    }
    if (c == '#') {
        // POSIX convention: $# does NOT count $0 (script name).
        char *s = next_scratch();
        const frame_t *f = top();
        int n = (f && f->argc > 0) ? f->argc - 1 : 0;
        snprintf(s, SHELL_ARG_MAX_LEN + 1, "%d", n);
        return s;
    }
    if (c == '?') {
        char *s = next_scratch();
        snprintf(s, SHELL_ARG_MAX_LEN + 1, "%d", g_status);
        return s;
    }
    return NULL;   // fall through to vars_get
}
