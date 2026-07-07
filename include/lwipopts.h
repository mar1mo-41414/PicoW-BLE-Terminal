// LwIP options for PicoBLE-Terminal. Sized for a single TCP client
// (HTTP OTA download) coexisting with BTStack on the CYW43. Kept close
// to pico-examples' wifi/lwipopts_examples_common.h so the CYW43 driver
// behaves the way its authors expect.
#ifndef PICOBLE_LWIPOPTS_H
#define PICOBLE_LWIPOPTS_H

// Standalone (no OS threads). pico_cyw43_arch_lwip_threadsafe_background
// drives the LwIP stack from an interrupt-service-context.
#define NO_SYS                      1

// Higher-level LwIP APIs disabled — we only use raw TCP.
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// Use pico's malloc, not LwIP's mem pool for regular allocations.
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
// Bumped from 4000 — a smaller heap was tight enough that ARP request
// pbufs sometimes couldn't be allocated when other traffic was pending,
// which manifested as ARP just never leaving the interface.
#define MEM_SIZE                    16000

#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

// Queue outbound packets while waiting for ARP resolution instead of
// dropping them. Cheap safety net.
#define ARP_QUEUEING                1
// Populate the ARP cache from incoming IP traffic (source IP + MAC).
// Without this the first exchange with a peer relies entirely on our
// own ARP round-trip completing.
#define ETHARP_TRUST_IP_MAC         1

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

// Use LwIP's built-in checksum implementation; the CYW43 driver doesn't
// have a hardware offload we can hand this to.
#define LWIP_CHKSUM_ALGORITHM       3

#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#endif

// Silence every subsystem debug channel unless we want it. Turning any
// of these on emits printfs that will interleave with the shell.
#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define MEMP_DEBUG                  LWIP_DBG_OFF
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF

#endif  // PICOBLE_LWIPOPTS_H
