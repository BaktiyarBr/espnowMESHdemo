#include "esp_now.h"


#define ESPNOW_QUEUE_SIZE 10
#define CATEGORY_CODE 127
#define CONFIG_ESPNOW_CHANNEL 1
#define ESPNOW_MAXDELAY 512

uint8_t node_black_mac[ESP_NOW_ETH_ALEN] = {0xa0, 0xb7, 0x65, 0x4b, 0x5f, 0x9c};
uint8_t node_blue_mac[ESP_NOW_ETH_ALEN] = {0xb8, 0xd6, 0x1a, 0xa7, 0xd1, 0x48};
uint8_t node_green_mac[ESP_NOW_ETH_ALEN] = {0x24, 0x6f, 0x28, 0x89, 0x26, 0x78};
uint8_t node_red_mac[ESP_NOW_ETH_ALEN] = {0x8c, 0xaa, 0xb5, 0x94, 0xe8, 0x4c};

typedef enum
{
    EXAMPLE_ESPNOW_SEND_CB,
    EXAMPLE_ESPNOW_RECV_CB,
} example_espnow_event_id_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} example_espnow_event_send_cb_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} example_espnow_event_recv_cb_t;

typedef union
{
    example_espnow_event_send_cb_t send_cb;
    example_espnow_event_recv_cb_t recv_cb;
} example_espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct
{
    example_espnow_event_id_t id;
    example_espnow_event_info_t info;
} example_espnow_event_t;


/* User defined field of ESPNOW data in this example. */
typedef struct
{
    uint32_t magic;     // Magic number which is used to determine which device to send unicast ESPNOW data.
    uint16_t crc;       // CRC16 value of ESPNOW data.
    uint8_t payload[8]; // Real payload of ESPNOW data.
} __attribute__((packed)) espnow_data_t;