// HTTP/1.1 GET client over LwIP raw TCP. See http_get.h for scope.
//
// Threading model: this file assumes the LwIP threadsafe_background arch,
// so tcp_* callbacks fire from an async_context worker (interrupt-ish).
// The blocking wrapper (http_get) drives a state machine held in ctx_t
// and busy-waits on a `complete` flag; every write to that flag comes
// from LwIP callback context.
#include "network/http_get.h"
#include "system/log.h"

#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- state -----------------------------------------------------------------

#define HOST_MAX 64
#define PATH_MAX 128
#define HDR_MAX  512

typedef enum {
    S_INIT,
    S_DNS,
    S_CONNECT,
    S_HEADERS,
    S_BODY,
    S_DONE,
    S_ERROR,
} state_t;

typedef struct {
    state_t state;

    struct tcp_pcb *pcb;
    ip_addr_t       remote_ip;
    uint16_t        port;
    char            host[HOST_MAX];
    char            path[PATH_MAX];

    char   hdr_buf[HDR_MAX + 1];   // +1 for NUL
    size_t hdr_len;

    bool   status_ok;
    size_t content_length;         // 0 = unknown / not present
    size_t body_received;

    http_chunk_cb_t chunk_cb;
    void           *user;

    http_result_t result;
    volatile bool complete;
} ctx_t;

// ---- URL parser ------------------------------------------------------------
//
// Grammar: "http://" host [":" port] [ "/" path ] — no query grammar work,
// the entire remainder after host[:port] is treated as the resource path.

static bool parse_url(const char *url, ctx_t *ctx) {
    if (!url || strncmp(url, "http://", 7) != 0) return false;
    const char *p = url + 7;

    const char *host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
    size_t host_len = (size_t)(host_end - p);
    if (host_len == 0 || host_len >= HOST_MAX) return false;
    memcpy(ctx->host, p, host_len);
    ctx->host[host_len] = '\0';

    p = host_end;
    if (*p == ':') {
        p++;
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (v == 0 || v > 65535) return false;
        ctx->port = (uint16_t)v;
        p = end;
    } else {
        ctx->port = 80;
    }

    if (*p == '\0') {
        ctx->path[0] = '/';
        ctx->path[1] = '\0';
    } else if (*p == '/') {
        size_t path_len = strlen(p);
        if (path_len >= PATH_MAX) return false;
        memcpy(ctx->path, p, path_len + 1);
    } else {
        return false;
    }
    return true;
}

// ---- header parser ---------------------------------------------------------

// Case-insensitive prefix match — HTTP headers are case-insensitive.
static bool ci_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        char a = *s++, b = *prefix++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

static void parse_response_headers(ctx_t *ctx) {
    ctx->hdr_buf[ctx->hdr_len] = '\0';

    // Status line: "HTTP/1.x <code> <reason>\r\n"
    ctx->status_ok = false;
    if (ctx->hdr_len >= 12 && memcmp(ctx->hdr_buf, "HTTP/1.", 7) == 0) {
        const char *sp = strchr(ctx->hdr_buf, ' ');
        if (sp) {
            int code = atoi(sp + 1);
            ctx->status_ok = (code >= 200 && code < 300);
            if (!ctx->status_ok) LOGW("http: server status %d", code);
        }
    }

    // Content-Length. Scan the header buffer line-by-line for it.
    const char *line = strstr(ctx->hdr_buf, "\r\n");
    ctx->content_length = 0;
    while (line) {
        line += 2;
        if (*line == '\0' || (line[0] == '\r' && line[1] == '\n')) break;
        if (ci_starts_with(line, "content-length:")) {
            const char *v = line + 15;
            while (*v == ' ' || *v == '\t') v++;
            ctx->content_length = (size_t)strtoul(v, NULL, 10);
        }
        line = strstr(line, "\r\n");
    }
}

// Feed bytes into the header buffer until we see the CRLF-CRLF sentinel.
// Returns the number of input bytes consumed (never > `len`). The state
// flips to S_BODY once headers finish; the caller should pass any
// remaining bytes on as body.
static size_t feed_header_bytes(ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t consumed = 0;
    while (consumed < len && ctx->state == S_HEADERS) {
        if (ctx->hdr_len < HDR_MAX) {
            ctx->hdr_buf[ctx->hdr_len++] = (char)data[consumed];
        } else {
            // Header buffer overflow — the server is either misbehaving
            // or serving something we can't handle. Abort.
            ctx->result = HTTP_ERR_PARSE;
            ctx->state  = S_ERROR;
            return consumed;
        }
        consumed++;
        if (ctx->hdr_len >= 4 &&
            memcmp(&ctx->hdr_buf[ctx->hdr_len - 4], "\r\n\r\n", 4) == 0) {
            parse_response_headers(ctx);
            ctx->state = S_BODY;
            break;
        }
    }
    return consumed;
}

// ---- LwIP callbacks --------------------------------------------------------

static void mark_complete(ctx_t *ctx, http_result_t r) {
    if (ctx->complete) return;
    ctx->result = r;
    ctx->state  = (r == HTTP_OK) ? S_DONE : S_ERROR;
    ctx->complete = true;
}

static void tcp_err_cb(void *arg, err_t err) {
    ctx_t *ctx = arg;
    LOGE("http: tcp err %d", (int)err);
    // pcb has already been freed by LwIP by the time err fires.
    ctx->pcb = NULL;
    mark_complete(ctx, HTTP_ERR_ABORTED);
}

static err_t send_request(ctx_t *ctx) {
    char req[HOST_MAX + PATH_MAX + 128];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: PicoBLE-Terminal/1\r\n"
        "Connection: close\r\n"
        "Accept: */*\r\n"
        "\r\n",
        ctx->path, ctx->host);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        mark_complete(ctx, HTTP_ERR_INTERNAL);
        return ERR_MEM;
    }
    err_t e = tcp_write(ctx->pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        mark_complete(ctx, HTTP_ERR_NOMEM);
        return e;
    }
    tcp_output(ctx->pcb);
    return ERR_OK;
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    ctx_t *ctx = arg;
    (void)pcb;
    if (err != ERR_OK) {
        mark_complete(ctx, HTTP_ERR_CONNECT);
        return err;
    }
    ctx->state = S_HEADERS;
    return send_request(ctx);
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    ctx_t *ctx = arg;

    if (!p) {
        // Peer closed. If we've received the whole body already, that's
        // success; otherwise the transfer was truncated.
        if (ctx->state == S_BODY &&
            (ctx->content_length == 0 || ctx->body_received == ctx->content_length) &&
            ctx->status_ok) {
            mark_complete(ctx, HTTP_OK);
        } else if (!ctx->complete) {
            mark_complete(ctx,
                ctx->status_ok ? HTTP_ERR_ABORTED : HTTP_ERR_HTTP_STATUS);
        }
        tcp_close(pcb);
        ctx->pcb = NULL;
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        mark_complete(ctx, HTTP_ERR_ABORTED);
        return err;
    }

    // Walk the pbuf chain — each segment might straddle the headers /
    // body boundary, so we run the header state machine per segment.
    struct pbuf *q = p;
    while (q && !ctx->complete) {
        const uint8_t *bytes = (const uint8_t *)q->payload;
        size_t remaining = q->len;

        if (ctx->state == S_HEADERS) {
            size_t used = feed_header_bytes(ctx, bytes, remaining);
            bytes     += used;
            remaining -= used;
            if (ctx->state == S_ERROR) break;
        }

        if (ctx->state == S_BODY && remaining > 0) {
            size_t take = remaining;
            if (ctx->content_length > 0) {
                size_t left = ctx->content_length - ctx->body_received;
                if (take > left) take = left;
            }
            if (take > 0 && ctx->chunk_cb) {
                bool keep_going = ctx->chunk_cb(bytes, take, ctx->user);
                if (!keep_going) {
                    mark_complete(ctx, HTTP_ERR_ABORTED);
                    break;
                }
            }
            ctx->body_received += take;

            if (ctx->content_length > 0 &&
                ctx->body_received == ctx->content_length) {
                if (ctx->status_ok) {
                    mark_complete(ctx, HTTP_OK);
                } else {
                    mark_complete(ctx, HTTP_ERR_HTTP_STATUS);
                }
            }
        }

        q = q->next;
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void start_connect(ctx_t *ctx) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        mark_complete(ctx, HTTP_ERR_NOMEM);
        return;
    }
    ctx->pcb   = pcb;
    ctx->state = S_CONNECT;
    tcp_arg(pcb, ctx);
    tcp_err(pcb, tcp_err_cb);
    tcp_recv(pcb, tcp_recv_cb);
    err_t e = tcp_connect(pcb, &ctx->remote_ip, ctx->port, tcp_connected_cb);
    if (e != ERR_OK) {
        mark_complete(ctx, HTTP_ERR_CONNECT);
    }
}

static void dns_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name;
    ctx_t *ctx = arg;
    if (!addr) {
        mark_complete(ctx, HTTP_ERR_DNS);
        return;
    }
    ctx->remote_ip = *addr;
    start_connect(ctx);
}

// ---- entry point -----------------------------------------------------------

http_result_t http_get(const char *url,
                       http_chunk_cb_t chunk_cb,
                       void *user,
                       uint32_t timeout_ms,
                       size_t *out_total) {
    static ctx_t ctx;  // static: only one download in flight at a time
    memset(&ctx, 0, sizeof(ctx));
    ctx.chunk_cb = chunk_cb;
    ctx.user     = user;
    ctx.state    = S_INIT;
    ctx.result   = HTTP_ERR_INTERNAL;

    if (!parse_url(url, &ctx)) return HTTP_ERR_PARSE;

    // Kick things off under the LwIP lock. dns_gethostbyname either
    // synchronously returns a cached answer (ERR_OK) or fires dns_cb
    // later; either way we then enter the connect path.
    cyw43_arch_lwip_begin();
    err_t e = dns_gethostbyname(ctx.host, &ctx.remote_ip, dns_cb, &ctx);
    if (e == ERR_OK) {
        start_connect(&ctx);
    } else if (e != ERR_INPROGRESS) {
        cyw43_arch_lwip_end();
        return HTTP_ERR_DNS;
    }
    cyw43_arch_lwip_end();

    // Busy-wait for the state machine to reach a terminal state. The
    // threadsafe_background arch drives all the LwIP work from an
    // async_context worker so we don't have to poll anything ourselves.
    absolute_time_t deadline = (timeout_ms > 0)
        ? make_timeout_time_ms(timeout_ms)
        : at_the_end_of_time;
    while (!ctx.complete) {
        if (timeout_ms > 0 && absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
            cyw43_arch_lwip_begin();
            if (ctx.pcb) { tcp_abort(ctx.pcb); ctx.pcb = NULL; }
            cyw43_arch_lwip_end();
            mark_complete(&ctx, HTTP_ERR_TIMEOUT);
            break;
        }
        sleep_ms(2);
    }

    if (out_total) *out_total = ctx.body_received;
    return ctx.result;
}
