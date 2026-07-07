// `ota status` — inspect the OTA subsystem.
// `ota check`  — probe for updates (not implemented yet).
// `ota update` — apply a staged update      (not implemented yet).
//
// The command exists in v0.1 so scripts and clients can already speak
// the shape of the API. Actual write path lands with the bootloader.
#include "cli/cli.h"
#include "cli/command.h"
#include "ota/ota.h"

#include <string.h>

static const char *slot_name(ota_slot_t s) {
    return s == OTA_SLOT_A ? "A" : "B";
}

static int cmd_status(cli_ctx_t *ctx) {
    ota_status_t st = ota_status();
    cli_write(ctx, "ota status\r\n");
    cli_printf(ctx, "  active slot     : %s\r\n", slot_name(st.active_slot));
    cli_printf(ctx, "  staging slot    : %s\r\n", slot_name(st.staging_slot));
    cli_printf(ctx, "  update running  : %s\r\n",
               st.update_in_progress ? "yes" : "no");
    cli_printf(ctx, "  pending verify  : %s\r\n",
               st.pending_verify ? "yes" : "no");
    if (st.update_in_progress) {
        cli_printf(ctx, "  progress        : %u / %u bytes\r\n",
                   (unsigned)st.bytes_written, (unsigned)st.staging_size);
    }
    return CLI_OK;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "status") == 0) return cmd_status(ctx);

    if (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "update") == 0) {
        cli_printf(ctx, "ota %s: not implemented in this build\r\n", argv[1]);
        return CLI_ERR_UNSUPPORTED;
    }

    cli_printf(ctx, "ota: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(ota,
    "OTA update controls (status only for now)",
    "ota status|check|update",
    handle);
