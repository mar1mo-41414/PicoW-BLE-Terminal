// `net test <host> <port>` — one-shot TCP connect probe.
// Useful for isolating "is the network reachable?" from the full OTA
// flow (which also erases 8 s of flash first).
#include "cli/cli.h"
#include "cli/command.h"
#include "network/wifi.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    volatile bool complete;
    err_t last_err;
    bool  connected;
} probe_t;

static void probe_err_cb(void *arg, err_t err) {
    probe_t *p = arg;
    p->last_err = err;
    p->complete = true;
}

static err_t probe_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    probe_t *p = arg;
    p->last_err = err;
    p->connected = (err == ERR_OK);
    p->complete = true;
    tcp_close(pcb);
    return ERR_OK;
}

// Synchronous DNS resolver — for the probe we only try the sync
// (cache / IP-literal) fast path and return an error otherwise so
// the tool stays simple.
static bool resolve(const char *host, ip_addr_t *out) {
    err_t e = dns_gethostbyname(host, out, NULL, NULL);
    return e == ERR_OK;
}

static int cmd_test(cli_ctx_t *ctx, const char *host, uint16_t port) {
    if (!wifi_is_connected()) {
        cli_write(ctx, "net: Wi-Fi not connected\r\n");
        return CLI_ERR_HARDWARE;
    }

    ip_addr_t ip;
    probe_t probe = {0};
    struct tcp_pcb *pcb = NULL;

    cyw43_arch_lwip_begin();
    if (!resolve(host, &ip)) {
        cyw43_arch_lwip_end();
        cli_printf(ctx, "net: cannot resolve %s (only IP literals / cached names in test)\r\n", host);
        return CLI_ERR_ARG;
    }
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        cyw43_arch_lwip_end();
        cli_write(ctx, "net: tcp_new failed\r\n");
        return CLI_ERR_HARDWARE;
    }
    tcp_arg(pcb, &probe);
    tcp_err(pcb, probe_err_cb);
    err_t cr = tcp_connect(pcb, &ip, port, probe_connected_cb);
    cyw43_arch_lwip_end();

    // Show the resolved dotted-quad so we know what we actually tried.
    const u8_t *o = (const u8_t *)&ip.addr;
    cli_printf(ctx, "net: connecting %u.%u.%u.%u:%u ...\r\n",
               o[0], o[1], o[2], o[3], port);

    if (cr != ERR_OK) {
        cli_printf(ctx, "net: tcp_connect returned %d (immediate)\r\n", (int)cr);
        return CLI_ERR_HARDWARE;
    }

    // sleep_ms would freeze the CYW43 async_context worker; use the
    // pico_cyw43_arch wait primitive that lets background work run.
    absolute_time_t deadline = make_timeout_time_ms(6000);
    while (!probe.complete &&
           absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        // Poll mode: we're the only thing that pumps LwIP + CYW43.
        cyw43_arch_poll();
        sleep_ms(2);
    }

    if (!probe.complete) {
        cyw43_arch_lwip_begin();
        tcp_abort(pcb);
        cyw43_arch_lwip_end();
        cli_write(ctx, "net: 6 s timeout — no SYN-ACK, no RST\r\n");
        return CLI_ERR_HARDWARE;
    }

    if (probe.connected) {
        cli_write(ctx, "net: connect ok\r\n");
        return CLI_OK;
    }
    cli_printf(ctx, "net: connect failed, lwip err=%d\r\n", (int)probe.last_err);
    return CLI_ERR_HARDWARE;
}

static void print_ip(cli_ctx_t *ctx, const char *label, const ip_addr_t *ip) {
    const u8_t *o = (const u8_t *)&ip->addr;
    cli_printf(ctx, "  %-8s : %u.%u.%u.%u\r\n", label, o[0], o[1], o[2], o[3]);
}

static void dump_one_netif(cli_ctx_t *ctx, struct netif *nif, bool is_default) {
    cli_printf(ctx, "netif : %c%c%u%s (up=%d, link=%d)\r\n",
               nif->name[0], nif->name[1], nif->num,
               is_default ? " *default*" : "",
               (int)netif_is_up(nif), (int)netif_is_link_up(nif));

    // Flag breakdown — NETIF_FLAG_ETHARP absence would explain why no
    // ARP replies show up.
    cli_printf(ctx, "  flags    :%s%s%s%s%s%s\r\n",
               (nif->flags & NETIF_FLAG_UP)         ? " UP"        : "",
               (nif->flags & NETIF_FLAG_LINK_UP)    ? " LINK"      : "",
               (nif->flags & NETIF_FLAG_BROADCAST)  ? " BROADCAST" : "",
               (nif->flags & NETIF_FLAG_ETHARP)     ? " ETHARP"    : "",
               (nif->flags & NETIF_FLAG_ETHERNET)   ? " ETHERNET"  : "",
               (nif->flags & NETIF_FLAG_IGMP)       ? " IGMP"      : "");

    print_ip(ctx, "ip",      netif_ip_addr4(nif));
    print_ip(ctx, "mask",    netif_ip_netmask4(nif));
    print_ip(ctx, "gateway", netif_ip_gw4(nif));

    // MAC
    cli_printf(ctx, "  %-8s : %02x:%02x:%02x:%02x:%02x:%02x\r\n", "mac",
               nif->hwaddr[0], nif->hwaddr[1], nif->hwaddr[2],
               nif->hwaddr[3], nif->hwaddr[4], nif->hwaddr[5]);
}

static int cmd_info(cli_ctx_t *ctx) {
    if (!netif_default && netif_list == NULL) {
        cli_write(ctx, "net: no netifs registered\r\n");
        return CLI_ERR_HARDWARE;
    }

    // Walk the full list, not just netif_default — a stray unrouted
    // interface would explain lost packets.
    for (struct netif *n = netif_list; n; n = n->next) {
        dump_one_netif(ctx, n, n == netif_default);
    }

    // ARP cache — surface how many entries we know. If it's 0 after a
    // failed net-test, the AP is very likely blocking device-to-device
    // ARP replies (Buffalo calls this プライバシーセパレータ).
    unsigned arp_valid = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *ipe;
        struct netif *nfe;
        struct eth_addr *hw;
        if (etharp_get_entry(i, &ipe, &nfe, &hw) == 1) {
            const u8_t *o = (const u8_t *)&ipe->addr;
            cli_printf(ctx, "  arp[%d]   : %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                       i, o[0], o[1], o[2], o[3],
                       hw->addr[0], hw->addr[1], hw->addr[2],
                       hw->addr[3], hw->addr[4], hw->addr[5]);
            arp_valid++;
        }
    }
    if (arp_valid == 0) {
        cli_write(ctx, "  arp      : (empty)\r\n");
    }
    return CLI_OK;
}

// Send an ARP request for `target` and wait to see the reply land in
// the ARP cache. If it never does, ARP is the reason nothing works.
static int cmd_arp(cli_ctx_t *ctx, const char *target) {
    ip_addr_t ip;
    if (!resolve(target, &ip)) {
        cli_printf(ctx, "net: cannot parse %s as IP\r\n", target);
        return CLI_ERR_ARG;
    }
    struct netif *nif = netif_default;
    if (!nif) {
        cli_write(ctx, "net: no default netif\r\n");
        return CLI_ERR_HARDWARE;
    }
    ip4_addr_t v4 = { .addr = ip.addr };

    const u8_t *o = (const u8_t *)&v4.addr;
    cli_printf(ctx, "net: ARP-requesting %u.%u.%u.%u ...\r\n",
               o[0], o[1], o[2], o[3]);

    cyw43_arch_lwip_begin();
    err_t e = etharp_request(nif, &v4);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        cli_printf(ctx, "net: etharp_request returned %d\r\n", (int)e);
        return CLI_ERR_HARDWARE;
    }

    // Give the peer 3 s to reply. Under poll mode we must call
    // cyw43_arch_poll ourselves — nothing else pumps LwIP.
    absolute_time_t arp_deadline = make_timeout_time_ms(3000);
    while (absolute_time_diff_us(get_absolute_time(), arp_deadline) > 0) {
        cyw43_arch_poll();
        sleep_ms(2);
    }

    struct eth_addr *hw;
    ip4_addr_t *found_ip;
    struct netif *found_nif;
    bool ok = false;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (etharp_get_entry(i, &found_ip, &found_nif, &hw) != 1) continue;
        if (found_ip->addr == v4.addr) {
            cli_printf(ctx, "net: got reply %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                       hw->addr[0], hw->addr[1], hw->addr[2],
                       hw->addr[3], hw->addr[4], hw->addr[5]);
            ok = true;
            break;
        }
    }
    if (!ok) {
        cli_write(ctx, "net: no ARP reply after 3 s — L2 traffic is not reaching us\r\n");
        return CLI_ERR_HARDWARE;
    }
    return CLI_OK;
}

static int handle(int argc, char **argv, cli_ctx_t *ctx) {
    if (argc < 2) return CLI_ERR_USAGE;

    if (strcmp(argv[1], "info") == 0) return cmd_info(ctx);

    if (strcmp(argv[1], "arp") == 0) {
        if (argc < 3) return CLI_ERR_USAGE;
        return cmd_arp(ctx, argv[2]);
    }

    if (strcmp(argv[1], "test") == 0) {
        if (argc < 4) return CLI_ERR_USAGE;
        int p = atoi(argv[3]);
        if (p <= 0 || p > 65535) {
            cli_write(ctx, "net: bad port\r\n");
            return CLI_ERR_ARG;
        }
        return cmd_test(ctx, argv[2], (uint16_t)p);
    }

    cli_printf(ctx, "net: unknown subcommand: %s\r\n", argv[1]);
    return CLI_ERR_USAGE;
}

CLI_COMMAND_REGISTER(net,
    "Network diagnostics",
    "net info | arp <ip> | test <host> <port>",
    handle);
