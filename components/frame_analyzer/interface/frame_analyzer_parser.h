/**
 * @file frame_analyzer_parser.h
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 * 
 * @brief Provides interface for parsing functionality
 */
#ifndef FRAME_ANALYZER_PARSER_H
#define FRAME_ANALYZER_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_analyzer_types.h"

/**
 * @brief Minimum size of an 802.11 data frame that can still carry EAPoL.
 * 
 * MAC header (24) + LLC/SNAP header (6) + EtherType (2) + EAPoL header (4)
 */
#define FRAME_ANALYZER_MIN_EAPOL_FRAME_LEN 36

/**
 * @brief Determines whether BSSID inside of the given frame matches given BSSID.
 * 
 * @param frame start of the 802.11 MAC header
 * @param frame_len length of the frame in bytes
 * @param bssid 
 * @return bool 
 */
bool is_frame_bssid_matching(const uint8_t *frame, unsigned frame_len, const uint8_t *bssid);

/**
 * @brief Parses EAPoL packet from given frame.
 * 
 * @param frame start of the 802.11 MAC header
 * @param frame_len length of the frame in bytes, FCS excluded
 * @return eapol_packet_t* if parsing successful 
 * @return \c NULL if no EAPoL packet was found
 * @return \c NULL if frame is protected
 * @return \c NULL if frame is too short to be parsed
 */
eapol_packet_t *parse_eapol_packet(const uint8_t *frame, unsigned frame_len);

/**
 * @brief Parses EAPoL-Key packet from EAPoL packet
 * 
 * @note result does not include EAPoL header
 * @param eapol_packet 
 * @return eapol_key_packet_t* if parsing successful
 * @return \c NULL if no EAPoL-Key packet found
 */
eapol_key_packet_t *parse_eapol_key_packet(eapol_packet_t *eapol_packet);

/**
 * @brief Parses PMKIDs from EAPoL-Key packet
 * 
 * @param eapol_key 
 * @param eapol_key_len number of readable bytes at \c eapol_key
 * @return pmkid_item_t* linked list of PMKIDs if parsing successful
 * @return \c NULL if no key data present
 * @return \c NULL if key data are encrypted
 * @return \c NULL parsing fails
 */
pmkid_item_t *parse_pmkid(eapol_key_packet_t *eapol_key, unsigned eapol_key_len);

#endif
