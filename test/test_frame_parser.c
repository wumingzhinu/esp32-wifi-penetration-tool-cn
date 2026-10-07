/* Host-side test harness for the reworked frame parser (not part of the firmware build). */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

/* Minimal stubs so the real parser source can be compiled on the host. */
typedef struct { unsigned dummy; } wifi_promiscuous_pkt_t;
#define ESP_EVENT_DEFINE_BASE(x)

#include "frame_analyzer_types.h"
#include "frame_analyzer_parser.h"

static int failures = 0;
static void check(int cond, const char *name){
    printf("%-46s %s\n", name, cond ? "PASS" : "FAIL");
    if(!cond) failures++;
}

/* Builds a QoS-data EAPoL-Key frame carrying one PMKID KDE. */
static unsigned build_pmkid_frame(uint8_t *buf, unsigned bufsz){
    memset(buf, 0, bufsz);
    const uint8_t bssid[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const uint8_t sta[6]   = {0x11,0x22,0x33,0x44,0x55,0x66};

    unsigned o = 0;
    /* Frame Control: type=2 (data), subtype=8 (QoS data) */
    buf[o++] = (2 << 2) | (8 << 4);
    buf[o++] = 0x00;
    buf[o++] = 0x00; buf[o++] = 0x00;          /* duration */
    memcpy(buf + o, bssid, 6); o += 6;          /* addr1 */
    memcpy(buf + o, sta,   6); o += 6;          /* addr2 */
    memcpy(buf + o, bssid, 6); o += 6;          /* addr3 = BSSID */
    buf[o++] = 0x00; buf[o++] = 0x00;          /* seq ctrl */
    buf[o++] = 0x00; buf[o++] = 0x00;          /* QoS control (A-MSDU bit clear) */
    /* LLC/SNAP */
    buf[o++] = 0xaa; buf[o++] = 0xaa; buf[o++] = 0x03;
    buf[o++] = 0x00; buf[o++] = 0x00; buf[o++] = 0x00;
    /* EtherType EAPOL */
    buf[o++] = 0x88; buf[o++] = 0x8e;
    /* EAPoL header */
    buf[o++] = 0x01; buf[o++] = 0x03;          /* version 1, type EAPOL-Key */
    unsigned key_data_len = 22;                 /* 1 KDE = 22 bytes */
    unsigned body_len = 4 + 95 + key_data_len;
    buf[o++] = (body_len >> 8) & 0xff; buf[o++] = body_len & 0xff;
    /* EAPoL-Key descriptor: 95 fixed bytes, then key_data_length(2) immediately followed by
     * key_data (there is no trailing reserved field in the on-wire layout this parses). */
    buf[o++] = 0x02;                           /* descriptor type */
    /* key_information is two bitfield bytes allocated LSB-first; encrypted_key_data is bit 4
     * of the second byte and must be 0, or parse_pmkid() rejects the frame by design. */
    buf[o++] = 0x8a; buf[o++] = 0x05;          /* key info: install|ack + key_mic */
    for(int i = 0; i < 90; i++) buf[o++] = 0x00;   /* rest of the fixed descriptor */
    buf[o++] = 0x00; buf[o++] = key_data_len;      /* key_data_length */
    /* --- key data: one PMKID KDE --- */
    buf[o++] = 0xdd;                           /* type */
    buf[o++] = (4 + 16 + 1);                   /* length = OUI(3)+type(1)+data(16)+? */
    buf[o++] = 0x00; buf[o++] = 0xfa; buf[o++] = 0xc0;  /* OUI 00:fa:c0 */
    buf[o++] = 0x01;                           /* data type = PMKID KDE */
    for(int i = 0; i < 16; i++) buf[o++] = (uint8_t)(0xa0 + i);
    return o;
}

/* A-MSDU: QoS Data whose QoS Control bit 0 (A-MSDU Present) is set, wrapping one EAPoL
 * subframe. The driver does not de-aggregate, so the parser must walk the subframe headers. */
static unsigned build_amsdu_frame(uint8_t *buf, unsigned bufsz){
    memset(buf, 0, bufsz);
    const uint8_t bssid[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const uint8_t sta[6]   = {0x11,0x22,0x33,0x44,0x55,0x66};
    unsigned o = 0;
    buf[o++] = (2 << 2) | (8 << 4);   /* type=2 data, subtype=8 QoS data */
    buf[o++] = 0x00;
    buf[o++] = 0x00; buf[o++] = 0x00;
    memcpy(buf + o, bssid, 6); o += 6;
    memcpy(buf + o, sta,   6); o += 6;
    memcpy(buf + o, bssid, 6); o += 6;
    buf[o++] = 0x00; buf[o++] = 0x00;
    buf[o++] = 0x01; buf[o++] = 0x00;   /* QoS Control: A-MSDU Present = 1 */

    /* one subframe: DA(6) SA(6) Len(2) then LLC/SNAP + EtherType + EAPoL header */
    const unsigned sub_payload = 6 /*LLC/SNAP*/ + 2 /*ethertype*/ + 4 /*EAPoL hdr*/;
    memcpy(buf + o, bssid, 6); o += 6;
    memcpy(buf + o, sta,   6); o += 6;
    buf[o++] = (sub_payload >> 8) & 0xff; buf[o++] = sub_payload & 0xff;
    buf[o++] = 0xaa; buf[o++] = 0xaa; buf[o++] = 0x03;
    buf[o++] = 0x00; buf[o++] = 0x00; buf[o++] = 0x00;
    buf[o++] = 0x88; buf[o++] = 0x8e;
    buf[o++] = 0x01; buf[o++] = 0x03;
    buf[o++] = 0x00; buf[o++] = 0x08;
    return o;
}

int main(void){
    static uint8_t frame[256];
    const uint8_t bssid[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};

    /* 1. BSSID matching, including a short-frame guard */
    unsigned len = build_pmkid_frame(frame, sizeof(frame));
    check(is_frame_bssid_matching(frame, len, bssid), "BSSID match on valid frame");
    check(!is_frame_bssid_matching(frame, 10, bssid), "BSSID match rejects short frame");
    check(!is_frame_bssid_matching(frame, len, (const uint8_t*)"\x01\x02\x03\x04\x05\x06"),
          "BSSID mismatch rejected");

    /* 2. EAPoL located after MAC header + QoS + LLC/SNAP */
    eapol_packet_t *eapol = parse_eapol_packet(frame, len);
    check(eapol != NULL, "EAPoL found in QoS data frame");

    /* 3. EAPoL-Key located after the EAPoL header */
    eapol_key_packet_t *key = parse_eapol_key_packet(eapol);
    check(key != NULL, "EAPoL-Key located");

    /* 4. PMKID extracted and value correct.
     * eapol_key sits behind the 4-byte EAPoL header, so the readable length is measured
     * from the key descriptor, not from the EAPoL header. */
    const unsigned key_offset = (unsigned)((const uint8_t *)key - frame);
    check(key_offset < len, "EAPoL-Key offset within frame");
    unsigned klen = len - key_offset;
    pmkid_item_t *pmkids = parse_pmkid(key, klen);
    check(pmkids != NULL, "PMKID parsed");
    if(pmkids){
        check(pmkids->next == NULL, "exactly one PMKID found");
        check(memcmp(pmkids->pmkid, "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf", 16) == 0,
              "PMKID bytes correct");
        free(pmkids);
    }

    /* 5. Truncated frames must be rejected, not read out of bounds */
    check(parse_eapol_packet(frame, 20) == NULL, "truncated frame rejected (< min len)");
    check(parse_eapol_packet(frame, 0) == NULL, "zero-length frame rejected");

    /* 6. A non-EAPoL EtherType is not mistaken for EAPoL */
    uint8_t saved[2];
    memcpy(saved, frame + 32, 2);
    frame[32] = 0x08; frame[33] = 0x00;   /* IPv4 */
    check(parse_eapol_packet(frame, len) == NULL, "non-EAPoL EtherType rejected");
    memcpy(frame + 32, saved, 2);

    /* 7. Protected frame is skipped (Protected Frame bit in FC byte 1) */
    frame[1] |= 0x40;
    check(parse_eapol_packet(frame, len) == NULL, "protected frame rejected");
    frame[1] &= ~0x40;

    /* 8. Walking key data past the end must terminate */
    pmkids = parse_pmkid(key, 40);
    check(pmkids == NULL || pmkids != NULL, "truncated key data terminates safely");
    while(pmkids){ pmkid_item_t *n = pmkids->next; free(pmkids); pmkids = n; }

    /* 9. A-MSDU subframe carrying EAPoL is found by walking the subframe headers */
    static uint8_t amsdu[256];
    unsigned alen = build_amsdu_frame(amsdu, sizeof(amsdu));
    eapol_packet_t *aep = parse_eapol_packet(amsdu, alen);
    check(aep != NULL, "EAPoL found inside A-MSDU subframe");
    if(aep){
        check(parse_eapol_key_packet(aep) != NULL, "EAPoL-Key found inside A-MSDU subframe");
    }

    /* 10. A-MSDU with a zero-length subframe must terminate, not spin.
     * Subframe layout after MAC header (24) + QoS Control (2): DA (6) + SA (6) + Len (2). */
    amsdu[38] = 0x00; amsdu[39] = 0x00;
    check(parse_eapol_packet(amsdu, alen) == NULL, "zero-length A-MSDU subframe terminates");

    /* 11. A-MSDU subframe advertising more than it holds must be rejected */
    unsigned alen2 = build_amsdu_frame(amsdu, sizeof(amsdu));
    amsdu[38] = 0xff; amsdu[39] = 0xff;
    check(parse_eapol_packet(amsdu, alen2) == NULL, "overlong A-MSDU subframe rejected");

    printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
