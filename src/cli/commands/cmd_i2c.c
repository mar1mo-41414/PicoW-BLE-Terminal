// `i2c <bus> scan`
// `i2c <bus> read  <addr7> <len>`
// `i2c <bus> write <addr7> <hex-bytes>`
//
// addr7 = 7-bit address (0x08..0x77). hex-bytes = contiguous hex, no
// separator: e.g. `deadbeef` = 4 bytes.
#include "cli/cli.h"
#include "cli/command.h"
#include "drivers/i2c_ctrl.h"

#include <stdlib.h>
#include <string.h>

static bool parse_uint_base(const char *s, unsigned long *out, int base, unsigned long max) {
    if (!s || !*s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, base);
    if (*end != '\0' || v > max) return false;
    *out = v;
    return true;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Parses "deadbeef" style contiguous hex; returns bytes decoded or -1 on
// odd length / bad char. Writes into `out`.
static int parse_hex_bytes(const char *s, uint8_t *out, size_t max) {
    size_t n = strlen(s);
    if (n == 0 || (n & 1)) return -1;
    size_t bytes = n / 2;
    if (bytes > max) return -1;
    for (size_t i = 0; i < bytes; i++) {
        int hi = hex_nibble(s[i * 2]);
        int lo = hex_nibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)bytes;
}

static int cmd_scan(cli_ctx_t *ctx, uint bus) {
    uint8_t found[128];
    size_t n = i2c_ctrl_scan(bus, found, sizeof(found));
    if (n == 0) {
        cli_write(ctx, "i2c: no devices found\r\n");
    } else {
        cli_printf(ctx, "i2c: %u device(s):", (unsigned)n);
        for (size_t i = 0; i < n; i++) cli_printf(ctx, " 0x%02x", found[i]);
        cli_write(ctx, "\r\n");
    }
    return CLI_OK;
}

static int cmd_read(cli_ctx_t *ctx, uint bus, const char *addr_s, const char *len_s) {
    unsigned long addr, len;
    // Accept 0x-prefixed hex or plain decimal for the address.
    int base = (addr_s[0] == '0' && (addr_s[1] == 'x' || addr_s[1] == 'X')) ? 16 : 10;
    if (!parse_uint_base(addr_s, &addr, base, 0x77) || addr < 0x08) {
        cli_printf(ctx, "i2c: bad address: %s (0x08..0x77)\r\n", addr_s);
        return CLI_ERR_ARG;
    }
    if (!parse_uint_base(len_s, &len, 10, 64) || len == 0) {
        cli_printf(ctx, "i2c: bad length: %s (1..64)\r\n", len_s);
        return CLI_ERR_ARG;
    }
    uint8_t buf[64];
    int rc = i2c_ctrl_read(bus, (uint8_t)addr, buf, (size_t)len);
    if (rc < 0) {
        cli_printf(ctx, "i2c: read failed rc=%d\r\n", rc);
        return CLI_ERR_HARDWARE;
    }
    for (int i = 0; i < rc; i++) cli_printf(ctx, "%02x", buf[i]);
    cli_write(ctx, "\r\n");
    return CLI_OK;
}

static int cmd_write(cli_ctx_t *ctx, uint bus, const char *addr_s, const char *hex_s) {
    unsigned long addr;
    int base = (addr_s[0] == '0' && (addr_s[1] == 'x' || addr_s[1] == 'X')) ? 16 : 10;
    if (!parse_uint_base(addr_s, &addr, base, 0x77) || addr < 0x08) {
        cli_printf(ctx, "i2c: bad address: %s (0x08..0x77)\r\n", addr_s);
        return CLI_ERR_ARG;
    }
    uint8_t buf[64];
    int n = parse_hex_bytes(hex_s, buf, sizeof(buf));
    if (n < 0) {
        cli_printf(ctx, "i2c: bad hex: %s (need even-length hex chars)\r\n", hex_s);
        return CLI_ERR_ARG;
    }
    int rc = i2c_ctrl_write(bus, (uint8_t)addr, buf, (size_t)n);
    if (rc < 0) {
        cli_printf(ctx, "i2c: write failed rc=%d\r\n", rc);
        return CLI_ERR_HARDWARE;
    }
    return CLI_OK;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 3) return CLI_ERR_USAGE;

    unsigned long bus_ul;
    if (!parse_uint_base(argv[1], &bus_ul, 10, I2C_BUS_MAX - 1)) {
        cli_printf(ctx, "i2c: bad bus: %s (0..%d)\r\n", argv[1], I2C_BUS_MAX - 1);
        return CLI_ERR_ARG;
    }
    uint bus = (uint)bus_ul;

    const char *op = argv[2];
    if (strcmp(op, "scan") == 0)  return cmd_scan(ctx, bus);
    if (strcmp(op, "read") == 0) {
        if (argc < 5) return CLI_ERR_USAGE;
        return cmd_read(ctx, bus, argv[3], argv[4]);
    }
    if (strcmp(op, "write") == 0) {
        if (argc < 5) return CLI_ERR_USAGE;
        return cmd_write(ctx, bus, argv[3], argv[4]);
    }

    cli_printf(ctx, "i2c: unknown op: %s\r\n", op);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(i2c,
    "I2C master: scan / read / write",
    "i2c <bus> scan | read <addr7> <len> | write <addr7> <hex>",
    handle);
