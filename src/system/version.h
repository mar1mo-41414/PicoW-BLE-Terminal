// Firmware version — bumped by hand on release. Keep in sync with the
// banner printed in cli_greet() and with the top of README.md.
#ifndef PICOBLE_SYSTEM_VERSION_H
#define PICOBLE_SYSTEM_VERSION_H

#define PICOBLE_FW_VERSION "v0.1.2"

// __DATE__ / __TIME__ are captured at translation time — build systems
// that want reproducible builds can override SOURCE_DATE_EPOCH.
#define PICOBLE_BUILD_DATE __DATE__ " " __TIME__

#endif  // PICOBLE_SYSTEM_VERSION_H
