/**
 * @file frame_analyzer_parser.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements parsing functionality
 */
#include "frame_analyzer_parser.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "arpa/inet.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_event.h"

#include "frame_analyzer_types.h"

static const char *TAG = "frame_analyzer:parser";


/**
 * @brief Offset of Address 3 (BSSID) inside the 802.11 MAC header.
 */
#define MAC_HEADER_ADDR3_OFFSET 16

/**
 * @brief Reads a big-endian uint16_t without assuming the source is aligned.
 */
static inline uint16_t read_be16(const uint8_t *p){
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return ntohs(v);
}

/**
 * @brief Reads a big-endian 24-bit value without assuming the source is aligned.
 */
static inline uint32_t read_be24(const uint8_t *p){
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

/**
 * @brief Walks the subframes of an A-MSDU body looking for an EAPoL payload.
 * 
 * The driver hands over A-MSDU bodies un-de-aggregated. Each subframe is prefixed with
 * DA (6) + SA (6) + Length (2), then carries an optional LLC/SNAP header.
 * Ref: 802.11-2016 [10.3.2.8 A-MSDU]
 * 
 * @param body start of the A-MSDU body (after the MAC header and QoS Control field)
 * @param body_len number of readable bytes at body
 */
static eapol_packet_t *parse_eapol_from_amsdu(const uint8_t *body, unsigned body_len){
    unsigned offset = 0;
    // Each subframe needs its 14-byte header plus an LLC/SNAP header and an EtherType.
    while(body_len - offset >= (6 + 6 + 2 + sizeof(llc_snap_header_t) + 2 + sizeof(eapol_packet_header_t))){
        const uint8_t *subframe = body + offset;
        unsigned subframe_len = read_be16(subframe + 12);

        if(subframe_len < sizeof(llc_snap_header_t) + 2 + sizeof(eapol_packet_header_t)){
            return NULL;
        }
        if(subframe_len > body_len - offset - 14){
            // Advertised length runs past the end of the body: malformed, stop here.
            return NULL;
        }

        const uint8_t *ethertype = subframe + 14 + sizeof(llc_snap_header_t);
        if(read_be16(ethertype) == ETHER_TYPE_EAPOL){
            return (eapol_packet_t *)(ethertype + 2);
        }

        offset += 14 + subframe_len;
        // Guard against a zero-length subframe looping forever.
        if(subframe_len == 0){
            return NULL;
        }
    }
    return NULL;
}

bool is_frame_bssid_matching(const uint8_t *frame, unsigned frame_len, const uint8_t *bssid) {
    // Need at least the fixed fields of the MAC header to read Address 3.
    if(frame_len < MAC_HEADER_ADDR3_OFFSET + 6){
        return false;
    }
    return memcmp(frame + MAC_HEADER_ADDR3_OFFSET, bssid, 6) == 0;
}

eapol_packet_t *parse_eapol_packet(const uint8_t *frame, unsigned frame_len) {
    // Frame Control (2) + Duration (2) + Addr1..3 (18) + Seq Ctrl (2) + LLC/SNAP (6)
    // + EtherType (2) + EAPoL header (4)
    if(frame_len < FRAME_ANALYZER_MIN_EAPOL_FRAME_LEN){
        return NULL;
    }

    // Frame Control is the first byte pair. Protocol Version (2b) / Type (2b) / Subtype (4b).
    const uint8_t type = (frame[0] >> 2) & 0x03;
    const uint8_t subtype = (frame[0] >> 4) & 0x0f;
    const bool protected_frame = (frame[1] & 0x40) != 0;

    if(type != 2){
        return NULL;
    }
    if(protected_frame){
        return NULL;
    }

    unsigned offset = sizeof(data_frame_mac_header_t);

    // QoS Data frames (subtypes 8..15) carry a 2-byte QoS Control field before the payload.
    if(subtype & 0x08){
        offset += 2;
        // QoS Control bit 0 is A-MSDU Present. 802.11 numbers the bits of this field LSB-first,
        // so it is the low bit of the *first* octet, i.e. frame[offset - 2].
        // The driver does not de-aggregate A-MSDU, so subframes must be walked to find EAPoL.
        // Ref: 802.11-2016 [9.4.2.37 QoS Control]
        if(frame[offset - 2] & 0x01){
            return parse_eapol_from_amsdu(frame + offset, frame_len - offset);
        }
    }

    if(frame_len < offset + sizeof(llc_snap_header_t) + 2){
        return NULL;
    }

    // Skip the 6-byte LLC/SNAP header, then check the EtherType.
    const uint8_t *ethertype = frame + offset + sizeof(llc_snap_header_t);
    if(read_be16(ethertype) != ETHER_TYPE_EAPOL){
        return NULL;
    }

    return (eapol_packet_t *)(ethertype + 2);
}

eapol_key_packet_t *parse_eapol_key_packet(eapol_packet_t *eapol_packet){
    if(eapol_packet == NULL){
        return NULL;
    }
    if(eapol_packet->header.packet_type != EAPOL_KEY){
        return NULL;
    }
    return (eapol_key_packet_t *) eapol_packet->packet_body;
}

/**
 * @brief Parses all PMKIDs to linked list structure 
 * 
 * It crawlers through key data buffer and looks for PMKIDs.
 * If PMKID element is found, its saved into the list of PMKIDs.
 * 
 * @param key_data 
 * @param length of key data
 * @param key_data_max readable bytes at key_data
 * @return pmkid_item_t* 
 */
static pmkid_item_t *parse_pmkid_from_key_data(uint8_t *key_data, uint16_t length, unsigned key_data_max){
    uint8_t *key_data_index = key_data;
    uint8_t *key_data_max_index = key_data + key_data_max;

    pmkid_item_t *pmkid_item_head = NULL;
    uint8_t element_length;

    // Each KDE is at least type+length+oui+data_type (6 bytes). Bail out rather than
    // walking off the end of the frame on a malformed or truncated element.
    while((unsigned)(key_data_max_index - key_data_index) >= 6){
        element_length = key_data_index[1];

        // The KDE header is byte-oriented here: the packed bitfields in key_data_field_t
        // cannot have their address taken, and the OUI comparison is clearer on raw bytes.
        if(key_data_index[0] != KEY_DATA_TYPE){
            break;
        }
        if(read_be24(key_data_index + 2) != KEY_DATA_OUI_IEEE80211){
            break;
        }
        if(key_data_index[5] != KEY_DATA_DATA_TYPE_PMKID_KDE){
            break;
        }

        // A PMKID KDE always carries a full 16-byte PMKID.
        if((unsigned)(key_data_max_index - key_data_index) < 22){
            break;
        }

        pmkid_item_t *pmkid_item = (pmkid_item_t *) malloc(sizeof(pmkid_item_t));
        if(pmkid_item == NULL){
            break;
        }
        memcpy(pmkid_item->pmkid, key_data_index + 6, 16);
        pmkid_item->next = pmkid_item_head;
        pmkid_item_head = pmkid_item;

        // Advance past this element. The KDE Length field covers OUI (3) + data type (1) +
        // data, and the next element starts after type (1) + length (1) + that payload.
        unsigned consumed = (unsigned)element_length + 2;
        unsigned remaining = (unsigned)(key_data_max_index - key_data_index);
        // Refuse to step outside the buffer: a bogus Length would otherwise wrap the
        // remaining-bytes check around and let the loop read past the end.
        if(consumed >= remaining){
            break;
        }
        key_data_index += consumed;
    }

    return pmkid_item_head;
}

pmkid_item_t *parse_pmkid(eapol_key_packet_t *eapol_key, unsigned eapol_key_len){
    if(eapol_key == NULL){
        return NULL;
    }
    if(eapol_key->key_data_length == 0){
        return NULL;
    }
    if(eapol_key->key_information.encrypted_key_data == 1){
        return NULL;
    }

    // key_data sits past the fixed EAPoL-Key header, which is 4 (EAPoL) + 95 (key descriptor).
    const uint8_t *key_data = eapol_key->key_data;
    unsigned key_data_max = eapol_key_len - offsetof(eapol_key_packet_t, key_data);
    unsigned key_data_len = ntohs(eapol_key->key_data_length);

    // Trust the smaller of the advertised and the actually available length.
    if(key_data_len > key_data_max){
        key_data_len = key_data_max;
    }
    if(key_data_len == 0){
        return NULL;
    }

    return parse_pmkid_from_key_data((uint8_t *)key_data, key_data_len, key_data_len);
}
