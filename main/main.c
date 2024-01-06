#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <espnow_structures.h>
#include <colors.h>


uint8_t SELF_MAC_ADDRESS[6];

static void espnow_task(void *pvParameter);

static QueueHandle_t s_example_espnow_queue;

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    //ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    
    if(status == ESP_NOW_SEND_SUCCESS) ESP_LOGI("send_cb", "SEND_SUCCESS");
    else ESP_LOGI("send_cb", "send faild");

    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE("send_cb", "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;

    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW("send_cb", "Send send queue fail");
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {

    // ESP_LOGI("recv_cb", "Data:" MACSTR "%s", MAC2STR(recv_info -> src_addr), data);

    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

    uint8_t * mac_addr = recv_info->src_addr;
    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE("recv_cb", "Receive cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;

    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE("recv_cb", "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW("recv_cb", "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* prepare ESPNOW data. */
void espnow_data_prepare(espnow_data_t *data_struct, uint8_t *payload, size_t payload_len) {
    data_struct -> magic = esp_random();
    memcpy(data_struct->payload, payload, payload_len);
    data_struct -> crc = 0;
    data_struct -> crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)data_struct, sizeof(data_struct));
    ESP_LOGI("espnow_data_prepare", "%d", data_struct -> crc);
}

/* Parse received ESPNOW data. */
espnow_data_t espnow_data_parse(espnow_data_t *data)
{
    ESP_LOGI("EXAMPLE_ESPNOW_RECV_CB", "data magic = %ld", data -> magic);
    ESP_LOGI("EXAMPLE_ESPNOW_RECV_CB", "data crc = %d", data -> crc);

    printf("EXAMPLE_ESPNOW_RECV_CB");

    for (int i = 0; i < sizeof(data -> payload); i++) {
        printf("%d", data->payload[i]);
        //ESP_LOGI("EXAMPLE_ESPNOW_RECV_CB", "%d", data->payload[i]);
    }

    uint16_t crc_from_data = data->crc;
    uint16_t crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)data, sizeof(data));

    ESP_LOGI("crc_from_data", "%d", crc_from_data);
    ESP_LOGI("crc_cal_full", "%d", crc_cal);


    espnow_data_t parsed_data_structure = {};
    
    if (crc_cal != crc_from_data) {
        ESP_LOGE("espnow_data_parse", "CRC is BAD");
        return parsed_data_structure;
    }

    ESP_LOGI("espnow_data_parse", "CRC is OK");
    parsed_data_structure.magic = data -> magic;
    parsed_data_structure.crc = data -> crc;
    memcpy(parsed_data_structure.payload, data -> payload, sizeof(data -> payload));
    return parsed_data_structure;
   
}

static esp_err_t esp_now_initialization(void) {

    //create a queue for data
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE("queue creation", "Create queue fail");
        return ESP_FAIL;
    }

    //init espnow and cb functions
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );
    
    return ESP_OK;
}

static esp_err_t register_peer (uint8_t *peer_addr) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    peer.channel = CONFIG_ESPNOW_CHANNEL;
    peer.ifidx = ESP_IF_WIFI_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK( esp_now_add_peer(&peer) );
    return ESP_OK;
}

void check_espnow_return(esp_err_t err) {
    switch (err)
    {
    case ESP_OK :
        ESP_LOGI("check_espnow_return", "ESP_OK : succeed");
        break;
    case ESP_ERR_ESPNOW_NOT_INIT :
        ESP_LOGI("check_espnow_return", "ESP_ERR_ESPNOW_NOT_INIT : ESPNOW is not initialized");
        break;
    case ESP_ERR_ESPNOW_ARG :
        ESP_LOGI("check_espnow_return", "ESP_ERR_ESPNOW_ARG : invalid argument");
        break;
    case ESP_ERR_ESPNOW_FULL :
        ESP_LOGI("check_espnow_return", "ESP_ERR_ESPNOW_FULL : peer list is full");
        break;
    case ESP_ERR_ESPNOW_NO_MEM :
        ESP_LOGI("check_espnow_return", "ESP_ERR_ESPNOW_NO_MEM : out of memory");
        break;
    case ESP_ERR_ESPNOW_EXIST :
        ESP_LOGI("check_espnow_return", "ESP_ERR_ESPNOW_EXIST : peer has existed");
        break;
    default:
        break;
    }
}

char* get_color_by_mac(uint8_t* address) {

       // Adjust the size according to the maximum possible string length
    char* result = malloc(50); // Example size, adjust as needed
    if (result == NULL) {
        return NULL; // Check if malloc was successful
    }
    result[0] = '\0'; // Initialize the string

    if (!memcmp(node_black_mac, address, ESP_NOW_ETH_ALEN)) {
        strcat(result, "black");
    } else if (!memcmp(node_blue_mac, address, ESP_NOW_ETH_ALEN)) {
        strcat(result, ANSI_COLOR_BLUE);
        strcat(result, "blue");
        strcat(result, ANSI_COLOR_RESET);
    } else if (!memcmp(node_green_mac, address, ESP_NOW_ETH_ALEN)) {
        strcat(result, ANSI_COLOR_GREEN);
        strcat(result, "green");
        strcat(result, ANSI_COLOR_RESET);
    } else if (!memcmp(node_red_mac, address, ESP_NOW_ETH_ALEN)) {
        strcat(result, ANSI_COLOR_RED);
        strcat(result, "red");
        strcat(result, ANSI_COLOR_RESET);
    }

    return result;

}

void log_recv(uint8_t* sender_address, uint8_t* receiver_address) {
    char* sender_color = get_color_by_mac(sender_address);
    char* receiver_color = get_color_by_mac(receiver_address);
    printf("log_recv %s node received from %s node \n", receiver_color, sender_color);
    // ESP_LOGW("log_recv ", "%s node received from %s node", receiver_color, sender_color);
}

void log_send(uint8_t* sender_address, uint8_t* receiver_address) {
    char* sender_color = get_color_by_mac(sender_address);
    char* receiver_color = get_color_by_mac(receiver_address);

    // ESP_LOGW("log_send ", "%s node sended to %s node", sender_color, receiver_color);
    printf("log_send %s node sended to %s node \n", sender_color, receiver_color);
}

static void espnow_recieve_task(void *pvParameter) {
    example_espnow_event_t evt;
    espnow_data_t receive_data_struct;

    while(true) {
        if (s_example_espnow_queue == 0 || !xQueueReceive(s_example_espnow_queue, &evt,  portMAX_DELAY)) 
            continue;
            
        switch (evt.id) {
            case EXAMPLE_ESPNOW_SEND_CB:{
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                //ESP_LOGI("espnow_task ESPNOW_SEND_CB ", "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);
                log_send(&SELF_MAC_ADDRESS, send_cb->mac_addr);
            } break;

            case EXAMPLE_ESPNOW_RECV_CB:{
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
                espnow_data_t *recv_data = (espnow_data_t *) recv_cb -> data;

                //ESP_LOGI("espnow_task ESPNOW_RECV_CB ", "Data recieved from "MACSTR"", MAC2STR(recv_cb->mac_addr));
                //ESP_LOGI("EXAMPLE_ESPNOW_RECV_CB", "data len = %d", recv_cb -> data_len);
                log_recv(recv_cb->mac_addr, &SELF_MAC_ADDRESS);
                receive_data_struct = espnow_data_parse(recv_data);
                
            } break;
        }
    }
}

int get_cpu_ticks_by_mac(uint8_t* address) {

    if (!memcmp(node_black_mac, address, ESP_NOW_ETH_ALEN)) {
        return 1000;
    } else if (!memcmp(node_blue_mac, address, ESP_NOW_ETH_ALEN) ){
        return 1500;
    } else if (!memcmp(node_green_mac, address, ESP_NOW_ETH_ALEN) ){
        return 2500;
    } else if (!memcmp(node_red_mac, address, ESP_NOW_ETH_ALEN) ){
        return 3500;
    }

    return 1000;
}

static void espnow_send_task(void *pvParameter){
    
    espnow_data_t send_data_struct;
    uint8_t my_payload[] = {
        8, 3, 5, 4, 3, 1, 7, 15
    };

    espnow_data_prepare(&send_data_struct, my_payload, sizeof(my_payload));

    int tick_delay = get_cpu_ticks_by_mac(SELF_MAC_ADDRESS);

    for(;;){
        esp_err_t result = esp_now_send(NULL, &send_data_struct, sizeof(send_data_struct));
        //check_espnow_return(result);
        vTaskDelay(tick_delay / portTICK_PERIOD_MS);
    }
}

void register_peers() {
    if (memcmp(node_black_mac, SELF_MAC_ADDRESS, ESP_NOW_ETH_ALEN)) {
        ESP_ERROR_CHECK(register_peer(node_black_mac));
    }
    if (memcmp(node_blue_mac, SELF_MAC_ADDRESS, ESP_NOW_ETH_ALEN)) {
        ESP_ERROR_CHECK(register_peer(node_blue_mac));
    }
    if (memcmp(node_green_mac, SELF_MAC_ADDRESS, ESP_NOW_ETH_ALEN)) {
        ESP_ERROR_CHECK(register_peer(node_green_mac));
    }
    if (memcmp(node_red_mac, SELF_MAC_ADDRESS, ESP_NOW_ETH_ALEN)) {
        ESP_ERROR_CHECK(register_peer(node_red_mac));
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    ESP_ERROR_CHECK(esp_now_initialization());

    esp_read_mac(SELF_MAC_ADDRESS, ESP_MAC_WIFI_STA);
    ESP_LOGI("appmain", " MY__MAC = "MACSTR" ", MAC2STR(SELF_MAC_ADDRESS));

    register_peers();

    xTaskCreate(espnow_recieve_task, "espnow_recieve_task", 4096 , NULL, 4, NULL);
    xTaskCreate(espnow_send_task, "espnow_send_task", 2048 , NULL, 3, NULL);

    
}
