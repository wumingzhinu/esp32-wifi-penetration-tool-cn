/**
 * @file client_counter.c
 * @brief 通过被动嗅探帧统计每个 AP 的客户端数量。
 */
#include "client_counter.h"

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "sniffer.h"
#include "ap_scanner.h"
#include "wifi_controller.h"
#include "sdkconfig.h"

#define TAG "client_counter"

#define MAX_AP 20
#define MAX_CLIENTS 16
#define SCAN_SECONDS 2

/**
 * @brief 单个 AP 的客户端统计信息
 */
typedef struct {
    uint8_t bssid[6];
    uint8_t macs[MAX_CLIENTS][6];
    uint8_t count;
} client_info_t;

static client_info_t infos[MAX_AP];
static int infos_count = 0;
static SemaphoreHandle_t lock = NULL;
static bool handler_registered = false;

static void ensure_lock(){
    if(!lock) lock = xSemaphoreCreateMutex();
}

void wifictl_clear_client_counts(){
    ensure_lock();
    xSemaphoreTake(lock, portMAX_DELAY);
    infos_count = 0;
    xSemaphoreGive(lock);
}

/**
 * @brief 嗅探事件回调：统计 destined 到各 AP 的唯一源 MAC。
 *
 * 802.11 固定头部：frame_ctrl(0-1) duration(2-3) addr1/dst(4-9) addr2/src(10-15) addr3(16-21)
 */
static void on_frame(void *arg, esp_event_base_t base, int32_t id, void *data){
    if(!data) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)data;
    if(pkt->rx_ctrl.sig_len < 16) return;

    uint8_t *frame = pkt->payload;
    uint8_t *addr1 = frame + 4;   // 目的
    uint8_t *addr2 = frame + 10;  // 源
    if(memcmp(addr1, addr2, 6) == 0) return; // AP 自身发出的帧（beacon 等）

    xSemaphoreTake(lock, portMAX_DELAY);
    int idx = -1;
    for(int i = 0; i < infos_count; i++){
        if(memcmp(infos[i].bssid, addr1, 6) == 0){ idx = i; break; }
    }
    if(idx == -1){
        if(infos_count >= MAX_AP){ xSemaphoreGive(lock); return; }
        idx = infos_count++;
        memcpy(infos[idx].bssid, addr1, 6);
        infos[idx].count = 0;
    }
    int dup = 0;
    for(int j = 0; j < infos[idx].count; j++){
        if(memcmp(infos[idx].macs[j], addr2, 6) == 0){ dup = 1; break; }
    }
    if(!dup && infos[idx].count < MAX_CLIENTS){
        memcpy(infos[idx].macs[infos[idx].count], addr2, 6);
        infos[idx].count++;
    }
    xSemaphoreGive(lock);
}

/**
 * @brief 后台任务：按信道逐个嗅探统计客户端。
 */
static void counting_task(void *arg){
    const wifictl_ap_records_t *aps = wifictl_get_ap_records();
    ESP_LOGI(TAG, "开始统计客户端，共 %d 个 AP", aps->count);

    wifictl_clear_client_counts();
    wifictl_sniffer_filter_frame_types(true, true, false); // 同时监听 data + mgmt

    uint8_t channels[13] = {0};
    for(int i = 0; i < aps->count; i++){
        uint8_t ch = aps->records[i].primary;
        if(ch >= 1 && ch <= 13) channels[ch-1] = 1;
    }

    for(int ch = 1; ch <= 13; ch++){
        if(!channels[ch-1]) continue;
        ESP_LOGI(TAG, "嗅探信道 %d ...", ch);
        wifictl_sniffer_start(ch);
        vTaskDelay(pdMS_TO_TICKS(SCAN_SECONDS * 1000));
        wifictl_sniffer_stop();
    }

    // 恢复管理热点（先切回配置的信道）
    wifictl_set_channel(CONFIG_MGMT_AP_CHANNEL);
    wifictl_mgmt_ap_start();

    /* 取注册的帧回调，避免后续攻击期间无意义运行 */
    if (handler_registered) {
        esp_event_handler_unregister(SNIFFER_EVENTS, ESP_EVENT_ANY_ID, &on_frame);
        handler_registered = false;
    }

    ESP_LOGI(TAG, "客户端统计完成");
    vTaskDelete(NULL);
}

void wifictl_start_client_counting(){
    ensure_lock();
    if(!handler_registered){
        esp_event_handler_register(SNIFFER_EVENTS, ESP_EVENT_ANY_ID, &on_frame, NULL);
        handler_registered = true;
    }
    xTaskCreate(counting_task, "count_clients", 4096, NULL, 5, NULL);
}

uint8_t wifictl_get_client_count(const uint8_t bssid[6]){
    ensure_lock();
    xSemaphoreTake(lock, portMAX_DELAY);
    for(int i = 0; i < infos_count; i++){
        if(memcmp(infos[i].bssid, bssid, 6) == 0){
            uint8_t c = infos[i].count;
            xSemaphoreGive(lock);
            return c;
        }
    }
    xSemaphoreGive(lock);
    return 0;
}
