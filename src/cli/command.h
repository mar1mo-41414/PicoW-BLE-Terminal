// Command registry for the CLI.
//
// Each command lives in its own file under src/cli/commands/ and registers
// itself via CLI_COMMAND_REGISTER at program startup (GCC constructor).
// Adding a new command is therefore a one-file drop-in — no central table
// to touch.
#ifndef PICOBLE_CLI_COMMAND_H
#define PICOBLE_CLI_COMMAND_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward — full definition in cli.h.
typedef struct cli_ctx cli_ctx_t;

// Exit codes returned by handlers. Semantics are advisory: any non-zero
// value is treated by the shell as "failure" but currently only used for
// logging.
#define CLI_OK              0
#define CLI_ERR_USAGE       1
#define CLI_ERR_ARG         2
#define CLI_ERR_HARDWARE    3
#define CLI_ERR_UNSUPPORTED 4

typedef int (*cli_handler_fn)(int argc, char **argv, cli_ctx_t *ctx);

typedef struct cli_command {
    const char *name;      // command word typed at the prompt
    const char *summary;   // one-line description for `help`
    const char *usage;     // "gpio <pin> read|high|low|toggle" style
    cli_handler_fn handler;
    struct cli_command *_next;  // linked-list link, do not touch
} cli_command_t;

// Register a command with the global table. Normally invoked by the
// CLI_COMMAND_REGISTER macro from a constructor, not called directly.
void cli_command_register(cli_command_t *cmd);

// Look up a command by name; returns NULL if not found.
const cli_command_t *cli_command_find(const char *name);

// Iterate the registry. Pass NULL to get the first command; pass the
// previous return value to advance. The list order matches registration
// order, which is roughly source-file order under `commands/`.
const cli_command_t *cli_command_iter(const cli_command_t *prev);

// Print "Usage:\n  <usage>\n" to ctx. Convenience for handlers.
void cli_usage(cli_ctx_t *ctx, const cli_command_t *cmd);

// Attach a command definition at file scope. Example:
//
//     CLI_COMMAND_REGISTER(hello,
//         "print a greeting",
//         "hello [name]",
//         cmd_hello_handler);
//
// The identifier `name_` must be a valid C identifier — it also becomes
// the string typed at the prompt.
#define CLI_COMMAND_REGISTER(name_, summary_, usage_, handler_)              \
    static cli_command_t _cli_cmd_##name_ = {                                \
        .name    = #name_,                                                   \
        .summary = (summary_),                                               \
        .usage   = (usage_),                                                 \
        .handler = (handler_),                                               \
        ._next   = NULL,                                                     \
    };                                                                       \
    __attribute__((constructor))                                             \
    static void _cli_cmd_ctor_##name_(void) {                                \
        cli_command_register(&_cli_cmd_##name_);                             \
    }

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_COMMAND_H
