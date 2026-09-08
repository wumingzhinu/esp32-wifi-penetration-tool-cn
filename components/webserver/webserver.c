/**
 * @file webserver.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-05
 * @copyright Copyright (c) 2021
 *
 * @brief Implements Webserver component and all available enpoints.
 *
 * Webserver is built on esp_http_server subcomponent from ESP-IDF
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
 */
#include "webserver.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_wifi_types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "wifi_controller.h"
#include "client_counter.h"
#include "attack.h"
#include "pcap_serializer.h"
#include "hccapx_serializer.h"

#include "pages/page_index.h"

static const char* TAG = "webserver";
ESP_EVENT_DEFINE_BASE(WEBSERVER_EVENTS);

/* ── 辅助函数 ────────────────────────────────────────────────────── */

static const char* authmode_to_str(uint8_t mode) {
    static const char* labels[] = {
        "OPEN", "WEP", "WPA-P", "WPA2-P", "WPA/WPA2-P",
        "WPA2-E", "WPA/WPA2-E", "WPA3-P", "WPA2/WPA3-P", "WAPI"
    };
    if (mode < sizeof(labels) / sizeof(labels[0])) {
        return labels[mode];
    }
    return "?";
}

static void mac_to_str(const uint8_t *mac, char *buf) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief 将 SSID 写入 JSON 字符串（带引号），返回写入字节数（含结尾 \0）
 */
static int ssid_to_json_buf(const uint8_t *ssid, char *buf, int buf_size) {
    int pos = 0;
    if (buf_size < 3) return 0;
    buf[pos++] = '"';
    int ssid_len = strlen((const char *)ssid);
    if (ssid_len > 32) ssid_len = 32;
    for (int i = 0; i < ssid_len && pos < buf_size - 2; i++) {
        uint8_t c = ssid[i];
        if (c == '"') {
            if (pos < buf_size - 3) { buf[pos++] = '\\'; buf[pos++] = '"'; }
        } else if (c == '\\') {
            if (pos < buf_size - 3) { buf[pos++] = '\\'; buf[pos++] = '\\'; }
        } else if (c >= 0x20 && c < 0x7F) {
            buf[pos++] = c;
        }
    }
    buf[pos++] = '"';
    buf[pos] = '\0';
    return pos;
}

/* ── 根页面 ──────────────────────────────────────────────────────── */

static esp_err_t uri_root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)page_index, page_index_len);
}

static httpd_uri_t uri_root_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = uri_root_get_handler,
    .user_ctx = NULL
};

/* ── /reset ──────────────────────────────────────────────────────── */

static esp_err_t uri_reset_head_handler(httpd_req_t *req) {
    ESP_ERROR_CHECK(esp_event_post(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_RESET, NULL, 0, portMAX_DELAY));
    return httpd_resp_send(req, NULL, 0);
}

static httpd_uri_t uri_reset_head = {
    .uri = "/reset",
    .method = HTTP_HEAD,
    .handler = uri_reset_head_handler,
    .user_ctx = NULL
};

/* ── /ap-list  (返回 JSON) ───────────────────────────────────────── */

static esp_err_t uri_ap_list_get_handler(httpd_req_t *req) {
    wifictl_scan_nearby_aps();
    const wifictl_ap_records_t *ap_records = wifictl_get_ap_records();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "[", 1);

    char chunk[256];
    for (unsigned i = 0; i < ap_records->count; i++) {
        int off = 0;
        if (i > 0) chunk[off++] = ',';

        off += snprintf(chunk + off, sizeof(chunk) - off,
            "{\"id\":%u,\"ssid\":", i);
        off += ssid_to_json_buf(ap_records->records[i].ssid,
                                chunk + off, sizeof(chunk) - off);

        char mac_buf[18];
        mac_to_str(ap_records->records[i].bssid, mac_buf);
        off += snprintf(chunk + off, sizeof(chunk) - off,
            ",\"bssid\":\"%s\",\"rssi\":%d,\"ch\":%u,\"encr\":\"%s\",\"clients\":%u}",
            mac_buf,
            (int8_t)ap_records->records[i].rssi,
            ap_records->records[i].primary,
            authmode_to_str(ap_records->records[i].authmode),
            wifictl_get_client_count(ap_records->records[i].bssid));

        httpd_resp_send_chunk(req, chunk, off);
    }
    return httpd_resp_send_chunk(req, "]", 1);
}

static httpd_uri_t uri_ap_list_get = {
    .uri = "/ap-list",
    .method = HTTP_GET,
    .handler = uri_ap_list_get_handler,
    .user_ctx = NULL
};

/* ── /count-clients ──────────────────────────────────────────────── */

static esp_err_t uri_count_clients_post_handler(httpd_req_t *req) {
    wifictl_start_client_counting();
    return httpd_resp_send(req, NULL, 0);
}

static httpd_uri_t uri_count_clients_post = {
    .uri = "/count-clients",
    .method = HTTP_POST,
    .handler = uri_count_clients_post_handler,
    .user_ctx = NULL
};

/* ── /attack-config?type=N  (返回攻击方式 + 默认值) ──────────────── */

static esp_err_t uri_attack_config_get_handler(httpd_req_t *req) {
    char query[32] = {0};
    char type_str[4] = {0};
    int attack_type = 1;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "type", type_str, sizeof(type_str)) == ESP_OK) {
            attack_type = atoi(type_str);
        }
    }

    char resp[384];
    int off = 0;

    switch (attack_type) {
        case 1: /* HANDSHAKE */
            off = snprintf(resp, sizeof(resp),
                "{\"methods\":["
                "{\"v\":0,\"l\":\"广播解除认证（主动）\"},"
                "{\"v\":1,\"l\":\"伪造热点（被动）\"},"
                "{\"v\":2,\"l\":\"仅捕获（被动）\"}"
                "],\"default_method\":0,\"default_timeout\":60}");
            break;
        case 2: /* PMKID */
            off = snprintf(resp, sizeof(resp),
                "{\"methods\":[],\"default_method\":0,\"default_timeout\":5}");
            break;
        case 3: /* DOS */
            off = snprintf(resp, sizeof(resp),
                "{\"methods\":["
                "{\"v\":0,\"l\":\"广播解除认证（主动）\"},"
                "{\"v\":1,\"l\":\"伪造热点（被动）\"},"
                "{\"v\":2,\"l\":\"组合全部\"}"
                "],\"default_method\":0,\"default_timeout\":120}");
            break;
        default:
            off = snprintf(resp, sizeof(resp),
                "{\"methods\":[],\"default_method\":0,\"default_timeout\":30}");
            break;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, off);
}

static httpd_uri_t uri_attack_config_get = {
    .uri = "/attack-config",
    .method = HTTP_GET,
    .handler = uri_attack_config_get_handler,
    .user_ctx = NULL
};

/* ── /run-attack ─────────────────────────────────────────────────── */

static esp_err_t uri_run_attack_post_handler(httpd_req_t *req) {
    attack_request_t attack_request;
    httpd_req_recv(req, (char *)&attack_request, sizeof(attack_request_t));
    esp_err_t res = httpd_resp_send(req, NULL, 0);
    ESP_ERROR_CHECK(esp_event_post(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_REQUEST, &attack_request, sizeof(attack_request_t), portMAX_DELAY));
    return res;
}

static httpd_uri_t uri_run_attack_post = {
    .uri = "/run-attack",
    .method = HTTP_POST,
    .handler = uri_run_attack_post_handler,
    .user_ctx = NULL
};

/* ── /status ─────────────────────────────────────────────────────── */

static esp_err_t uri_status_get_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Fetching attack status...");
    const attack_status_t *attack_status = attack_get_status();

    ESP_ERROR_CHECK(httpd_resp_set_type(req, HTTPD_TYPE_OCTET));
    ESP_ERROR_CHECK(httpd_resp_send_chunk(req, (char *) attack_status, 4));
    if (((attack_status->state == FINISHED) || (attack_status->state == TIMEOUT))
        && (attack_status->content_size > 0)) {
        ESP_ERROR_CHECK(httpd_resp_send_chunk(req, attack_status->content, attack_status->content_size));
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static httpd_uri_t uri_status_get = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = uri_status_get_handler,
    .user_ctx = NULL
};

/* ── /capture.pcap ───────────────────────────────────────────────── */

static esp_err_t uri_capture_pcap_get_handler(httpd_req_t *req){
    ESP_LOGD(TAG, "Providing PCAP file...");
    ESP_ERROR_CHECK(httpd_resp_set_type(req, HTTPD_TYPE_OCTET));
    return httpd_resp_send(req, (char *) pcap_serializer_get_buffer(), pcap_serializer_get_size());
}

static httpd_uri_t uri_capture_pcap_get = {
    .uri = "/capture.pcap",
    .method = HTTP_GET,
    .handler = uri_capture_pcap_get_handler,
    .user_ctx = NULL
};

/* ── /capture.hccapx ─────────────────────────────────────────────── */

static esp_err_t uri_capture_hccapx_get_handler(httpd_req_t *req){
    ESP_LOGD(TAG, "Providing HCCAPX file...");
    ESP_ERROR_CHECK(httpd_resp_set_type(req, HTTPD_TYPE_OCTET));
    return httpd_resp_send(req, (char *) hccapx_serializer_get(), sizeof(hccapx_t));
}

static httpd_uri_t uri_capture_hccapx_get = {
    .uri = "/capture.hccapx",
    .method = HTTP_GET,
    .handler = uri_capture_hccapx_get_handler,
    .user_ctx = NULL
};

/* ── /pmkid-result  (返回格式化 PMKID JSON) ──────────────────────── */

static esp_err_t uri_pmkid_result_get_handler(httpd_req_t *req) {
    const attack_status_t *st = attack_get_status();
    httpd_resp_set_type(req, "application/json");

    if (st->state != FINISHED && st->state != TIMEOUT) {
        return httpd_resp_send(req, "{\"error\":\"no result\"}", -1);
    }
    if (st->type != ATTACK_TYPE_PMKID || st->content == NULL || st->content_size == 0) {
        return httpd_resp_send(req, "{\"error\":\"no pmkid result\"}", -1);
    }

    const uint8_t *p = (const uint8_t *)st->content;
    unsigned remain = st->content_size;

    /* 内容布局: STA_MAC(6) + AP_MAC(6) + SSID_LEN(1) + SSID(N) + PMKID*(16) */
    if (remain < 13) {
        return httpd_resp_send(req, "{\"error\":\"invalid data\"}", -1);
    }

    char mac_sta[18], mac_ap[18];
    mac_to_str(p, mac_sta);
    mac_to_str(p + 6, mac_ap);

    uint8_t ssid_len = p[12];
    const uint8_t *ssid = p + 13;
    if (ssid_len > 32) ssid_len = 32;

    unsigned pmkid_offset = 13 + ssid_len;
    unsigned pmkid_count = 0;
    if (remain > pmkid_offset) {
        pmkid_count = (remain - pmkid_offset) / 16;
    }

    char resp[512];
    int off = 0;
    off += snprintf(resp + off, sizeof(resp) - off,
        "{\"mac_sta\":\"%s\",\"mac_ap\":\"%s\",\"ssid\":\"", mac_sta, mac_ap);

    /* 写入 SSID hex */
    for (int i = 0; i < ssid_len && off < (int)sizeof(resp) - 2; i++) {
        off += snprintf(resp + off, sizeof(resp) - off, "%02X", ssid[i]);
    }
    off += snprintf(resp + off, sizeof(resp) - off, "\",\"ssid_text\":\"");
    /* 写入 SSID 文本（过滤不可打印字符） */
    for (int i = 0; i < ssid_len && off < (int)sizeof(resp) - 2; i++) {
        if (ssid[i] >= 0x20 && ssid[i] < 0x7F && ssid[i] != '"' && ssid[i] != '\\') {
            resp[off++] = ssid[i];
        }
    }
    off += snprintf(resp + off, sizeof(resp) - off,
        "\",\"pmkid_count\":%u,\"pmkids\":[", pmkid_count);

    /* 写入 PMKID hex 列表 */
    for (unsigned i = 0; i < pmkid_count && off < (int)sizeof(resp) - 4; i++) {
        if (i > 0) resp[off++] = ',';
        resp[off++] = '"';
        for (int j = 0; j < 16; j++) {
            off += snprintf(resp + off, sizeof(resp) - off,
                "%02X", pmkid_offset + i * 16 + j < remain
                    ? p[pmkid_offset + i * 16 + j] : 0);
        }
        resp[off++] = '"';
    }

    /* 构建 hashcat 就绪行（格式: PMKID*AP*STA*SSID_HEX） */
    off += snprintf(resp + off, sizeof(resp) - off,
        "],\"hashcat\":\"");
    if (pmkid_count > 0) {
        off += snprintf(resp + off, sizeof(resp) - off,
            "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X*",
            pmkid_offset < remain ? p[pmkid_offset] : 0,
            pmkid_offset + 1 < remain ? p[pmkid_offset + 1] : 0,
            pmkid_offset + 2 < remain ? p[pmkid_offset + 2] : 0,
            pmkid_offset + 3 < remain ? p[pmkid_offset + 3] : 0,
            pmkid_offset + 4 < remain ? p[pmkid_offset + 4] : 0,
            pmkid_offset + 5 < remain ? p[pmkid_offset + 5] : 0,
            pmkid_offset + 6 < remain ? p[pmkid_offset + 6] : 0,
            pmkid_offset + 7 < remain ? p[pmkid_offset + 7] : 0,
            pmkid_offset + 8 < remain ? p[pmkid_offset + 8] : 0,
            pmkid_offset + 9 < remain ? p[pmkid_offset + 9] : 0,
            pmkid_offset + 10 < remain ? p[pmkid_offset + 10] : 0,
            pmkid_offset + 11 < remain ? p[pmkid_offset + 11] : 0,
            pmkid_offset + 12 < remain ? p[pmkid_offset + 12] : 0,
            pmkid_offset + 13 < remain ? p[pmkid_offset + 13] : 0,
            pmkid_offset + 14 < remain ? p[pmkid_offset + 14] : 0,
            pmkid_offset + 15 < remain ? p[pmkid_offset + 15] : 0);
        /* AP MAC (无冒号) */
        for (int i = 0; i < 6; i++) {
            off += snprintf(resp + off, sizeof(resp) - off, "%02X", p[6 + i]);
        }
        off += snprintf(resp + off, sizeof(resp) - off, "*");
        /* STA MAC (无冒号) */
        for (int i = 0; i < 6; i++) {
            off += snprintf(resp + off, sizeof(resp) - off, "%02X", p[i]);
        }
        off += snprintf(resp + off, sizeof(resp) - off, "*");
        /* SSID hex */
        for (int i = 0; i < ssid_len; i++) {
            off += snprintf(resp + off, sizeof(resp) - off, "%02X", ssid[i]);
        }
    }
    off += snprintf(resp + off, sizeof(resp) - off, "\"}");

    return httpd_resp_send(req, resp, off);
}

static httpd_uri_t uri_pmkid_result_get = {
    .uri = "/pmkid-result",
    .method = HTTP_GET,
    .handler = uri_pmkid_result_get_handler,
    .user_ctx = NULL
};

/* ── 启动 ────────────────────────────────────────────────────────── */

void webserver_run(){
    ESP_LOGD(TAG, "Running webserver");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(httpd_start(&server, &config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_root_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_reset_head));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_ap_list_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_count_clients_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_attack_config_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_run_attack_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_status_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_capture_pcap_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_capture_hccapx_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_pmkid_result_get));
}
