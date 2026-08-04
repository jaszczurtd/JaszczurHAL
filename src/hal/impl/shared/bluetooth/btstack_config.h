#ifndef JASZCZURHAL_BTSTACK_CONFIG_H
#define JASZCZURHAL_BTSTACK_CONFIG_H

/* Private Stage 1 sizing. Public Bluetooth configuration is intentionally
 * deferred until the controller spike has been measured on both boards. */
#define ENABLE_LE_PERIPHERAL
#define ENABLE_SOFTWARE_AES128

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1024 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_L2CAP_CHANNELS 2
#define MAX_NR_L2CAP_SERVICES 1
#define MAX_NR_GATT_CLIENTS 0
#define MAX_NR_SM_LOOKUP_ENTRIES 1
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_LE_DEVICE_DB_ENTRIES 1
#define MAX_ATT_DB_SIZE 512

/* The shared CYW43 ring is small. Bound host/controller buffering so BLE
 * cannot starve Wi-Fi while the two protocols share gSPI. */
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

#define HAVE_EMBEDDED_TIME_MS
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#endif
