// Minimal HTTP/1.1 GET client for OTA image download.
//
// Blocking API — the caller sits on the current thread until the whole
// body has been received (or an error occurs). Body bytes are handed to
// a chunk callback as they arrive from LwIP, so the caller never has to
// materialize the entire response in memory.
//
// TLS is not implemented. This limits usability to HTTP endpoints on a
// trusted local network; the OTA layer's SHA-256 verification covers
// image integrity but not source authentication.
#ifndef PICOBLE_NETWORK_HTTP_GET_H
#define PICOBLE_NETWORK_HTTP_GET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_OK              =  0,
    HTTP_ERR_PARSE       = -1,   // couldn't decode the URL
    HTTP_ERR_DNS         = -2,   // hostname didn't resolve
    HTTP_ERR_CONNECT     = -3,   // TCP connect failed
    HTTP_ERR_HTTP_STATUS = -4,   // server returned non-2xx
    HTTP_ERR_ABORTED     = -5,   // remote closed mid-body or reset
    HTTP_ERR_TIMEOUT     = -6,   // overall deadline hit
    HTTP_ERR_NOMEM       = -7,   // LwIP pbuf/tcp_write pressure
    HTTP_ERR_INTERNAL    = -8,   // bad state / bad url args
} http_result_t;

// Called once per body chunk. Return false to abort the transfer.
typedef bool (*http_chunk_cb_t)(const uint8_t *data, size_t len, void *user);

// GET a URL and stream the body through `chunk_cb`.
//   url        : "http://host[:port]/path"
//   chunk_cb   : called from LwIP context — keep it short and non-blocking
//   user       : opaque user pointer for the callback
//   timeout_ms : overall deadline; 0 disables the timeout
//   out_total  : optional, receives the total body byte count
http_result_t http_get(const char *url,
                       http_chunk_cb_t chunk_cb,
                       void *user,
                       uint32_t timeout_ms,
                       size_t *out_total);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_NETWORK_HTTP_GET_H
