/**
 * @file sniffer.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements sniffer logic.
 */
#include "sniffer.h"

#include <stddef.h>
#include <string.h>

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

static const char *TAG = "sniffer"; 

ESP_EVENT_DEFINE_BASE(SNIFFER_EVENTS);

/**
 * @brief Layout of a captured frame as posted on the event loop.
 * 
 * Identical to \c wifi_promiscuous_pkt_t, but force-aligned so that the 802.11 frame starts
 * at a 4-byte boundary once the event loop copies it into its own (heap) buffer. Downstream
 * code casts the payload to structures containing \c uint16_t / \c uint32_t fields, and the
 * offset of \c payload inside \c wifi_pkt_rx_ctrl_t differs per target (it is a bitfield
 * struct), so unaligned access is a real hazard when porting between ESP32 and ESP32-C3.
 */
typedef struct {
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t payload[];
} __attribute__((aligned(4))) wifictl_frame_t;

_Static_assert(offsetof(wifictl_frame_t, payload) % 4 == 0, "802.11 frame must be 4-byte aligned");

/**
 * @brief Drops counters, exposed for diagnostics over the web interface.
 */
typedef struct {
    unsigned too_short;
    unsigned fcs_failed;
    unsigned aggregated;
    unsigned bssid_mismatch;
    unsigned queue_full;
} wifictl_sniffer_stats_t;

static wifictl_sniffer_stats_t stats;

static bool bssid_filter_enabled = false;
static uint8_t bssid_filter[6];
static wifictl_sniff_pkt_t pkt_types_filter = WIFICTL_SNIFF_PKT_DATA;

/**
 * @brief Reads the BSSID (address 3) out of an 802.11 frame without assuming alignment.
 * 
 * @param frame start of the 802.11 MAC header
 * @param out receives the 6 BSSID bytes
 */
static inline void read_bssid(const uint8_t *frame, uint8_t *out){
    memcpy(out, frame + 16, 6);
}

void wifictl_sniffer_set_bssid_filter(const uint8_t *bssid){
    if(bssid == NULL){
        bssid_filter_enabled = false;
        return;
    }
    memcpy(bssid_filter, bssid, 6);
    bssid_filter_enabled = true;
}

/**
 * @brief Maps the requested frame types onto the ESP-IDF promiscuous filter mask.
 */
static uint32_t build_filter_mask(wifictl_sniff_pkt_t pkt_types){
    uint32_t mask = 0;
    if(pkt_types & WIFICTL_SNIFF_PKT_DATA){
        mask |= WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_DATA_MPDU;
    }
    if(pkt_types & WIFICTL_SNIFF_PKT_MGMT){
        mask |= WIFI_PROMIS_FILTER_MASK_MGMT;
    }
    if(pkt_types & WIFICTL_SNIFF_PKT_CTRL){
        mask |= WIFI_PROMIS_FILTER_MASK_CTRL;
    }
    return mask;
}

/**
 * @brief Callback for promiscuous reciever. 
 * 
 * Runs in the Wi-Fi driver task, so it only validates and posts - all parsing and heavy work
 * is deferred to the event loop. Frames are pre-filtered here (length, FCS state,
 * aggregation, BSSID) because posting every frame on a busy channel is enough to saturate
 * the event queue and stall the driver task.
 * 
 * @param buf 
 * @param type 
 */
static void frame_handler(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *frame = (wifi_promiscuous_pkt_t *) buf;

    int32_t event_id;
    switch (type) {
        case WIFI_PKT_DATA:
            if(!(pkt_types_filter & WIFICTL_SNIFF_PKT_DATA)){
                return;
            }
            event_id = SNIFFER_EVENT_CAPTURED_DATA;
            break;
        case WIFI_PKT_MGMT:
            if(!(pkt_types_filter & WIFICTL_SNIFF_PKT_MGMT)){
                return;
            }
            event_id = SNIFFER_EVENT_CAPTURED_MGMT;
            break;
        case WIFI_PKT_CTRL:
            if(!(pkt_types_filter & WIFICTL_SNIFF_PKT_CTRL)){
                return;
            }
            event_id = SNIFFER_EVENT_CAPTURED_CTRL;
            break;
        default:
            return;
    }

    // The FCS (CRC32) is appended by the radio and counted in sig_len, but it is not part of
    // the 802.11 MPDU. Strip it so consumers never see (or write to PCAP) trailing garbage.
    unsigned mtu_len = frame->rx_ctrl.sig_len;
    if(mtu_len > WIFICTL_FCS_LEN){
        mtu_len -= WIFICTL_FCS_LEN;
    }
    // Anything shorter than a MAC header cannot be parsed safely.
    if(mtu_len < WIFICTL_MAC_HDR_LEN){
        stats.too_short++;
        return;
    }
    // FCS-failed frames cannot be trusted and would only add noise.
    if(frame->rx_ctrl.rx_state != 0){
        stats.fcs_failed++;
        return;
    }
    // A-MPDU buffers start with a delimiter, not with a Frame Control field, so the
    // 802.11 parsers cannot walk them. Skip them instead of producing bogus matches.
    if(frame->rx_ctrl.aggregation != 0){
        stats.aggregated++;
        return;
    }
    if(bssid_filter_enabled){
        uint8_t bssid[6];
        read_bssid(frame->payload, bssid);
        if(memcmp(bssid, bssid_filter, 6) != 0){
            stats.bssid_mismatch++;
            return;
        }
    }

    // Publish sig_len without the FCS so downstream length arithmetic is correct.
    wifictl_frame_t out;
    memcpy(&out.rx_ctrl, &frame->rx_ctrl, sizeof(out.rx_ctrl));
    out.rx_ctrl.sig_len = mtu_len;

    // Never block the Wi-Fi task: a full queue means the consumers are already saturated.
    if(esp_event_post(SNIFFER_EVENTS, event_id, &out,
                      sizeof(wifictl_frame_t) + mtu_len, 0) != ESP_OK){
        stats.queue_full++;
    }
}

void wifictl_sniffer_start(uint8_t channel, wifictl_sniff_pkt_t pkt_types) {
    ESP_LOGI(TAG, "Starting promiscuous mode...");
    // ESP32 cannot switch port, if there is some STA connected to AP
    ESP_LOGD(TAG, "Kicking all connected STAs from AP");
    ESP_ERROR_CHECK(esp_wifi_deauth_sta(0));

    pkt_types_filter = pkt_types;
    memset(&stats, 0, sizeof(stats));

    // Configure the promiscuous filter here so no caller can inherit a stale mask.
    wifi_promiscuous_filter_t filter = { .filter_mask = build_filter_mask(pkt_types) };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&frame_handler);
}

void wifictl_sniffer_stop() {
    ESP_LOGI(TAG, "Stopping promiscuous mode...");
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    // Leave no BSSID filter behind for the next capture session.
    bssid_filter_enabled = false;
}
