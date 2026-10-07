/**
 * @file sniffer.h
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 * 
 * @brief Provides an interface for sniffer functionality.
 */
#ifndef SNIFFER_H
#define SNIFFER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"
#include "esp_wifi_types.h"

ESP_EVENT_DECLARE_BASE(SNIFFER_EVENTS);

enum {
    SNIFFER_EVENT_CAPTURED_DATA,
    SNIFFER_EVENT_CAPTURED_MGMT,
    SNIFFER_EVENT_CAPTURED_CTRL
};

/**
 * @brief Length of the trailing Frame Check Sequence (CRC32) appended by the radio.
 * 
 * It is part of \c rx_ctrl.sig_len and of the captured payload, but it is NOT part of
 * the 802.11 MPDU. Must be removed before handing frames to a PCAP writer.
 * 
 * @see Ref: 802.11-2016 [10.3 Frame Check Sequence (FCS)]
 */
#define WIFICTL_FCS_LEN 4

/**
 * @brief Minimum length of a valid 802.11 MAC header (Frame Control .. Sequence Control).
 */
#define WIFICTL_MAC_HDR_LEN 24

/**
 * @brief Bitmask of 802.11 frame types to deliver to the application.
 */
typedef enum {
    WIFICTL_SNIFF_PKT_DATA = 1 << 0,
    WIFICTL_SNIFF_PKT_MGMT = 1 << 1,
    WIFICTL_SNIFF_PKT_CTRL = 1 << 2
} wifictl_sniff_pkt_t;

/**
 * @brief Restricts promiscuous capture to frames associated with a single BSSID.
 * 
 * Called before wifictl_sniffer_start(). Filtering happens inside the Wi-Fi driver task,
 * before any event is posted, which keeps the event queue from filling up on busy channels.
 * 
 * @param bssid BSSID to accept, or \c NULL to accept frames from any BSS
 */
void wifictl_sniffer_set_bssid_filter(const uint8_t *bssid);

/**
 * @brief Starts promiscuous mode on given channel.
 * 
 * The promiscuous filter is configured here, so callers cannot leave a stale filter mask
 * behind. Frames are pre-filtered by length, FCS state, aggregation and (optionally) BSSID
 * inside the RX callback before being posted to the event loop.
 * 
 * @param channel channel on which sniffer should operate
 * @param pkt_types bitmask of wifictl_sniff_pkt_t frame types to deliver
 */
void wifictl_sniffer_start(uint8_t channel, wifictl_sniff_pkt_t pkt_types);

/**
 * @brief Stop promisuous mode
 * 
 */
void wifictl_sniffer_stop();

#endif
