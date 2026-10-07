/**
 * @file frame_analyzer.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements frame analysis
 */
#include "frame_analyzer.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi_types.h"

#include "wifi_controller.h"
#include "sniffer.h"
#include "frame_analyzer_parser.h"

ESP_EVENT_DEFINE_BASE(FRAME_ANALYZER_EVENTS);

static const char *TAG = "frame_analyzer";
static uint8_t target_bssid[6];
static search_type_t search_type = -1;
static bool capture_active = false;


/**
 * @brief Analyzes data frames from sniffer.
 *  
 * @param args 
 * @param event_base 
 * @param event_id 
 * @param event_data 
 */
static void data_frame_handler(void *args, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    wifi_promiscuous_pkt_t *frame = (wifi_promiscuous_pkt_t *) event_data;
    const uint8_t *payload = frame->payload;
    const unsigned payload_len = frame->rx_ctrl.sig_len;

    // The sniffer already filtered by BSSID, but re-check here so the analyzer stays correct
    // regardless of the configured sniffer filter.
    if(!is_frame_bssid_matching(payload, payload_len, target_bssid)){
        return;
    }

    eapol_packet_t *eapol_packet = parse_eapol_packet(payload, payload_len);
    if(eapol_packet == NULL){
        return;
    }

    if(search_type == SEARCH_HANDSHAKE){
        // TODO handle timeouts properly by e.g. for cycle
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(FRAME_ANALYZER_EVENTS, DATA_FRAME_EVENT_EAPOLKEY_FRAME, frame, sizeof(wifi_promiscuous_pkt_t) + payload_len, portMAX_DELAY));
        return;
    }

    if(search_type == SEARCH_PMKID){
        // Hand over only the bytes that are actually readable from the EAPoL-Key descriptor,
        // so the PMKID parser can bound-check its walk through the key data.
        const unsigned eapol_offset = (unsigned)((const uint8_t *)eapol_packet - payload)
                                    + sizeof(eapol_packet_header_t);
        if(payload_len <= eapol_offset){
            return;
        }
        pmkid_item_t *pmkid_items = parse_pmkid((eapol_key_packet_t *)(payload + eapol_offset),
                                                payload_len - eapol_offset);
        if(pmkid_items == NULL){
            return;
        }
        ESP_ERROR_CHECK(esp_event_post(FRAME_ANALYZER_EVENTS, DATA_FRAME_EVENT_PMKID, &pmkid_items, sizeof(pmkid_item_t *), portMAX_DELAY));
        return;
    }
}

void frame_analyzer_capture_start(search_type_t search_type_arg, const uint8_t *bssid){
    ESP_LOGI(TAG, "Frame analysis started...");
    search_type = search_type_arg;
    memcpy(target_bssid, bssid, 6);
    // Drop unwanted frames inside the Wi-Fi driver task rather than after posting them.
    wifictl_sniffer_set_bssid_filter(bssid);
    if(!capture_active){
        ESP_ERROR_CHECK(esp_event_handler_register(SNIFFER_EVENTS, SNIFFER_EVENT_CAPTURED_DATA, &data_frame_handler, NULL));
        capture_active = true;
    }
}

void frame_analyzer_capture_stop(){
    // Unregister the exact handler, not ESP_EVENT_ANY_BASE/ESP_EVENT_ANY_ID - the latter also
    // tears down unrelated handlers (e.g. the client counter's) registered on this loop.
    if(capture_active){
        esp_event_handler_unregister(SNIFFER_EVENTS, SNIFFER_EVENT_CAPTURED_DATA, &data_frame_handler);
        capture_active = false;
    }
}
