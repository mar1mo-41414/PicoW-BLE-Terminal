// Duration parser shared by `gpio latch`, `sleep`, and any future
// timing-driven commands.
//
// Accepted formats:
//   "500"    → 500 ms   (bare integer = milliseconds)
//   "500ms"  → 500 ms
//   "2s"     → 2000 ms
//
// Clamp: min 1 ms, max 60000 ms (60 s). Anything outside → parse fail.
#ifndef PICOBLE_CLI_DURATION_H
#define PICOBLE_CLI_DURATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true on success and writes ms; false otherwise (bad suffix,
// no digits, over/under the 1..60000 clamp).
bool cli_parse_duration_ms(const char *s, uint32_t *out_ms);

// Sleep for `ms` while pumping cyw43_arch_poll(), so the Wi-Fi/BLE
// stacks stay responsive during long waits. Use this instead of
// bare sleep_ms() anywhere a shell command holds the loop.
void cli_keepalive_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif  // PICOBLE_CLI_DURATION_H
