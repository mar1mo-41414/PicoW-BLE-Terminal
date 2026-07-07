// Global command registry — a singly-linked list built at startup by
// constructors emitted from CLI_COMMAND_REGISTER.
#include "cli/command.h"
#include "cli/cli.h"

#include <string.h>

static cli_command_t *g_head = NULL;

// Insert while keeping the list sorted by command name so `help` output is
// deterministic no matter what order constructors happened to run in.
void cli_command_register(cli_command_t *cmd) {
    if (!cmd || !cmd->name) return;

    cli_command_t **slot = &g_head;
    while (*slot && strcmp((*slot)->name, cmd->name) < 0) {
        slot = &(*slot)->_next;
    }
    cmd->_next = *slot;
    *slot = cmd;
}

const cli_command_t *cli_command_find(const char *name) {
    if (!name) return NULL;
    for (const cli_command_t *c = g_head; c; c = c->_next) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

const cli_command_t *cli_command_iter(const cli_command_t *prev) {
    return prev ? prev->_next : g_head;
}

void cli_usage(cli_ctx_t *ctx, const cli_command_t *cmd) {
    if (!cmd || !cmd->usage) return;
    cli_printf(ctx, "Usage:\n  %s\n", cmd->usage);
}
