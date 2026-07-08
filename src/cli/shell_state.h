// Shell state for positional arguments ($0..$9, $#) and the last
// command's return code ($?).
//
// Positional args are stack-scoped so nested `run` calls (once they
// are allowed) can push their own frame and pop it on return.
#ifndef PICOBLE_CLI_SHELL_STATE_H
#define PICOBLE_CLI_SHELL_STATE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_ARG_MAX_LEN 63     // max length of any single positional arg
#define SHELL_MAX_ARGS    10     // supports $0..$9

// Push a new positional-arg frame. Copies up to SHELL_MAX_ARGS strings
// out of argv (typically caller uses argv+1 to skip the invoking
// command name). Returns false if we're at the max nesting depth.
bool shell_push_args(int argc, char **argv);

// Pop the top frame. No-op if the stack is empty.
void shell_pop_args(void);

// Set / read the last command's return code, exposed as $?
void shell_set_status(int rc);
int  shell_get_status(void);

// Look up a special expansion name. Returns NULL if the name isn't a
// shell-special (i.e. it isn't "0".."9", "#", or "?") — fall back to
// regular vars_get in that case.
//
// The returned pointer is valid until the next shell_push_args /
// shell_pop_args / shell_set_status call.
const char *shell_expand(const char *name);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_SHELL_STATE_H
