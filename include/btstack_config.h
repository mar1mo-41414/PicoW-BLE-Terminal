// BTstack build-time configuration for PicoBLE-Terminal.
//
// BTstack requires the application to supply this header on the include
// path. Values below are the minimum set for a LE-peripheral with a
// custom GATT service (NUS) on the Pico W CYW43. The controller-flow
// numbers are the same pico-examples uses to avoid overrunning the
// cyw43 shared SPI bus.
#ifndef PICOBLE_BTSTACK_CONFIG_H
#define PICOBLE_BTSTACK_CONFIG_H

// ---- BTstack features enabled -----------------------------------------------
// ENABLE_BLE is already set by pico_btstack_ble on the command line; guard so
// we don't trip a -Wmacro-redefined warning.
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_ATT_DELAYED_RESPONSE
#define ENABLE_PRINTF_HEXDUMP

// ---- Buffers and limits -----------------------------------------------------
#define HCI_ACL_PAYLOAD_SIZE          (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT  4
// Required by pico's cyw43 HCI transport — leaves 4 bytes of headroom for
// the CYW43 packet header prepended to outgoing HCI ACL/CMD packets.
#define HCI_OUTGOING_PRE_BUFFER_SIZE  4
#define MAX_NR_GATT_CLIENTS           0
#define MAX_NR_HCI_CONNECTIONS        1
#define MAX_NR_LE_DEVICE_DB_ENTRIES   1
#define MAX_NR_SM_LOOKUP_ENTRIES      3
#define MAX_NR_WHITELIST_ENTRIES      16
#define MAX_ATT_DB_SIZE               512

// ---- CYW43 shared-bus flow control ------------------------------------------
// Keep controller queue small so BLE HCI packets don't back up behind
// the same SPI channel Wi-Fi (when enabled) would compete for.
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN       (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM       3
#define HCI_HOST_SCO_PACKET_LEN       120
#define HCI_HOST_SCO_PACKET_NUM       3

// ---- Persistent storage sizing ----------------------------------------------
// TLV-backed device DB — dimensions kept even though we don't bond today,
// so enabling bonding later doesn't require a config change.
#define NVM_NUM_DEVICE_DB_ENTRIES     16
#define NVM_NUM_LINK_KEYS             16

// ---- Crypto -----------------------------------------------------------------
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ---- HAL --------------------------------------------------------------------
#define HAVE_EMBEDDED_TIME_MS
#define HCI_RESET_RESEND_TIMEOUT_MS   1000

#endif  // PICOBLE_BTSTACK_CONFIG_H
