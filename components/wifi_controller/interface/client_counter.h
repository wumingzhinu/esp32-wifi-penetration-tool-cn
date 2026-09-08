/**
 * @file client_counter.h
 * @brief 通过被动嗅探帧统计每个 AP 的客户端数量。
 *
 * 原理：监听 destined 到各 AP(BSSID) 的帧，统计其中不重复的源 MAC 地址数。
 * 源 MAC != 目的 MAC 的帧来自客户端，源 MAC == 目的 MAC 的帧来自 AP 自身(beacon 等)。
 *
 * @note 嗅探必须先断开管理热点（ESP32 无法一边开 AP 一边嗅探），
 *       统计完成后管理热点会自动恢复，需要重新连接。
 * @note 这是被动监听得到的估算值，只统计到统计窗口内发过帧的活跃客户端。
 */
#ifndef CLIENT_COUNTER_H
#define CLIENT_COUNTER_H

#include <stdint.h>

/**
 * @brief 启动客户端统计后台任务。
 *
 * 会按上次扫描到的信道逐个嗅探，统计每个 AP 的唯一客户端数。
 * 会先断开管理热点，完成后自动恢复管理热点。
 */
void wifictl_start_client_counting();

/**
 * @brief 获取指定 BSSID 的客户端数量（上次统计结果，未统计过返回 0）。
 */
uint8_t wifictl_get_client_count(const uint8_t bssid[6]);

/**
 * @brief 清空统计结果。
 */
void wifictl_clear_client_counts();

#endif
