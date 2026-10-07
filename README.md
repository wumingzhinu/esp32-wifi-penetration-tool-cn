# ESP32 Wi-Fi 渗透测试工具（中文汉化 · ESP32-C3 版）

> 基于 [risinek/esp32-wifi-penetration-tool](https://github.com/risinek/esp32-wifi-penetration-tool) 汉化并移植到 **ESP32-C3** 的分支。
> 后端 100% 纯 C（ESP-IDF），前端逻辑已全部搬回 C 端（JSON API），浏览器端仅做极简渲染。

本项目为 ESP32 平台提供了一个通用工具，用于实现各种 Wi-Fi 攻击。它提供了一些 Wi-Fi 攻击中常用的通用功能，使实现新攻击变得更加简单。它还包含了 Wi-Fi 攻击本身的实现，例如从握手中捕获 PMKID，或通过不同方法捕获握手本身（如启动伪造的重复热点或直接发送解除认证帧等）...

显然，破解哈希不是本项目的一部分，因为 ESP32 无法以高效的方式破解哈希。其余的工作都可以在这个小型、廉价、低功耗的 SoC 上完成。

## 本分支特性

| 特性 | 说明 |
|------|------|
| 🇨🇳 **全量汉化** | Web UI、README、攻击原理文档、组件文档全部中文化 |
| 🔧 **ESP32-C3 移植** | 原项目仅支持 ESP32 (Xtensa)，本分支适配 ESP32-C3 (RISC-V)，ESP-IDF v4.4 编译 |
| 🖥️ **深色主题 UI** | 信号色条、加密彩色标签、客户端 badge、旋转动画、卡片式结果布局、移动端响应式 |
| ⚙️ **逻辑全部在 C 端** | 加密方式映射、MAC 格式化、攻击方式列表、PMKID 结果解析、hashcat 行生成——全部由 C 后端 JSON API 完成，JS 仅做渲染 |
| 👥 **客户端探测** | 被动嗅探统计每个 AP 的在线客户端数（Wifite 风格 CLIENT 列） |
| 🚫 **WSL 绕过** | 通过 `--wrap` 链接器技巧在 C3 上覆盖 `ieee80211_raw_frame_sanity_check`，实现原始 802.11 帧（解除认证帧）发送 |
| 🤖 **GitHub Actions 云编译** | 无需本地环境，推送即自动编译 ESP32-C3 固件 |

## 功能特性
- **PMKID 捕获**
- **WPA/WPA2 握手捕获**与解析
- 使用多种方法的**解除认证攻击**
- **拒绝服务攻击**
- 将捕获的流量格式化为 **PCAP 格式**
- 将捕获的握手解析为 **HCCAPX 文件**，可直接用于 Hashcat 破解
- 被动握手嗅探
- 易于扩展的框架，便于实现新的攻击
- 管理热点，方便随时使用智能手机等进行配置

## 使用方法
1. 从 [Actions](../../actions) 下载最新一次成功构建的 `esp32c3-firmware` 产物，或按下方[编译](#编译)自行编译
1. 按下方[烧录](#烧录)烧录到 ESP32-C3
1. 给 ESP32-C3 供电
1. 管理热点在启动后自动开启
1. 连接到此热点\
默认配置：
*SSID:* `ManagementAP`，*密码:* `mgmtadmin`
1. 在浏览器中打开 `192.168.4.1`，您将看到深色主题的 Web 控制面板

## 编译

本项目使用 **ESP-IDF v4.4**（原项目的 v4.1 不支持 ESP32-C3）：

```shell
git clone https://github.com/wumingzhinu/esp32-wifi-penetration-tool-cn
cd esp32-wifi-penetration-tool-cn
git checkout cn-localization

idf.py set-target esp32c3
idf.py build
```

产物位于 `build/`：
- `bootloader/bootloader.bin`
- `partition_table/partition-table.bin`
- `esp32-wifi-penetration-tool.bin`

### 云编译（推荐）

推送到 GitHub 后在 **Actions** 页面手动触发 `Build ESP32-C3 Firmware`，完成后下载 `esp32c3-firmware` 产物即可，无需本地搭建环境。

## 烧录

> ⚠️ **注意：ESP32-C3 的烧录地址与原版 ESP32 不同！**
> 原版 ESP32 bootloader 在 `0x1000`；**ESP32-C3 bootloader 在 `0x0`**。烧错地址会导致完全无法启动（找不到热点）。

### 使用 espflash

```shell
espflash write-bin --port <串口> --baud 115200 \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x10000 esp32-wifi-penetration-tool.bin
```

### 使用 esptool.py

```shell
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 115200 --after hard_reset write_flash \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/esp32-wifi-penetration-tool.bin
```

### 使用 idf.py

```shell
idf.py -p <串口> flash
```

## Web API（全部 C 端实现）

| 端点 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 主页面（gzip） |
| `/ap-list` | GET | JSON：附近热点（含中文加密标签、MAC 字符串、客户端数） |
| `/attack-config?type=N` | GET | JSON：攻击方式列表 + 默认超时（type: 1=握手 2=PMKID 3=DoS） |
| `/run-attack` | POST | 启动攻击（4 字节二进制：AP id / 类型 / 方式 / 超时） |
| `/status` | GET | 攻击状态（二进制头 + 结果内容） |
| `/pmkid-result` | GET | JSON：格式化 PMKID 结果 + hashcat 就绪行 |
| `/count-clients` | POST | 触发被动嗅探客户端统计 |
| `/capture.pcap` | GET | 下载 PCAP |
| `/capture.hccapx` | GET | 下载 HCCAPX |
| `/reset` | HEAD | 复位攻击状态机 |

## 文档
### Wi-Fi 攻击
本项目中的攻击实现在 [main 组件 README](main/) 中有详细描述。这些攻击背后的原理位于 [doc/ATTACKS_THEORY.md](doc/ATTACKS_THEORY.md)
### API 参考
本项目使用 Doxygen 标注来记录组件 API 和实现。项目中包含 Doxyfile，如果您想生成 API 参考，只需在根目录运行 `doxygen` 即可。它将在 `doc/api/html` 中生成 HTML API 参考。

### 组件
本项目由多个组件组成，这些组件可以在其他项目中复用。每个组件都有自己的 README，包含详细描述。以下是各组件的简要说明：

- [**Main（主组件）**](main) 攻击状态机与各攻击实现（握手捕获 / PMKID / DoS）。
- [**Wifi Controller（Wi-Fi 控制器）**](components/wifi_controller) 封装所有 Wi-Fi 操作：热点、扫描、嗅探、**客户端统计（client_counter）**等。
- [**Webserver（Web 服务器）**](components/webserver) Web 界面与 JSON API。所有格式化逻辑（加密标签 / MAC / PMKID / hashcat 行）均在此 C 端完成。
- [**Wi-Fi Stack Libraries Bypasser（Wi-Fi 协议栈库绕过器）**](components/wsl_bypasser) 通过 `--wrap` 链接器技巧绕过协议栈对原始 802.11 帧发送的限制（C3 适配版）。
- [**Frame Analyzer（帧分析器）**](components/frame_analyzer) 处理捕获的帧，提供解析功能。
- [**PCAP Serializer（PCAP 序列化器）**](components/pcap_serializer) 将捕获的帧序列化为 PCAP 二进制格式。
- [**HCCAPX Serializer（HCCAPX 序列化器）**](components/hccapx_serializer) 将捕获的握手序列化为 HCCAPX 二进制格式。

## 硬件
本分支在 **ESP32-C3**（RISC-V 单核，160MHz，Wi-Fi 4）上编译验证。原项目在 **ESP32-DEVKITC-32E** / **ESP32-WROOM-32** 上开发测试。

ESP32-C3 开发板（如 ESP32-C3-DevKitM-1）单价约 ¥15~25，功耗更低，非常适合便携渗透测试场景。

## 与原版差异摘要

| 项目 | 原版 | 本分支 |
|------|------|--------|
| 目标芯片 | ESP32 (Xtensa) | **ESP32-C3** (RISC-V) |
| ESP-IDF | v4.1 | **v4.4** |
| WSL 绕过 | `-z muldefs` 重复定义 | **`-Wl,--wrap=ieee80211_raw_frame_sanity_check`** |
| 前端逻辑 | JS 二进制解析（43 字节/热点） | **C 端 JSON API，JS 仅渲染** |
| UI | 原始白底表格 | **深色主题、彩色标签、动画** |
| 客户端数列 | 无 | **被动嗅探统计** |
| 语言 | 英文 | **中文** |
| 串口日志 | 大量 ESP_LOG + 裸 printf | **全部关闭**（`_NONE` 配置） |

## 2026-10 更新：嗅探与 PMKID 修复

本轮修复了帧捕获链路上的若干实际缺陷，并按要求关闭了串口调试输出。

### 功能性 Bug 修复

| 问题 | 现象 | 修复 |
|------|------|------|
| PMKID 数据类型常量错误 | `KEY_DATA_DATA_TYPE_PMKID_KDE` 写成 `4`，实际应为 `1`（802.11-2016 Table 12-6）。判断永不成立，PMKID 提取是死代码 | 常量改为 `1` |
| OUI 常量畸形 | `KEY_DATA_OUI_IEEE80211` 为 `0x00fac00`（多一位），且对 24 位 OUI 使用 `ntohl()` 导致恒不匹配 | 改为 `0x00fac0`，比较改为逐字节读取 |
| 帧长度含 FCS | `rx_ctrl.sig_len` 包含尾部 4 字节 FCS，原代码当作帧长使用，导致 PCAP 每个包多 4 字节 CRC、Wireshark 解析错位 | 在嗅探回调中剥离 FCS，并同步修正 `sig_len` |
| A-MSDU 未处理 | 驱动不做反聚合，包在 A-MSDU 子帧中的 EAPoL 会被漏掉 | 解析器新增子帧遍历 |

### 健壮性修复

- **越界读取**：`parse_eapol_packet` / `parse_pmkid` 全程缺少长度校验，畸形帧或截断帧会读到缓冲区外；`parse_pmkid_from_key_data` 的推进量在 length 很小时会让指针回退。现已加入完整边界检查。
- **内存对齐**：`wifi_pkt_rx_ctrl_t` 是位域结构体，各 target 布局不同，`payload` 偏移可能未对齐。新增 4 字节对齐的帧包装 + `_Static_assert`。
- **A-MPDU 误判**：`rx_ctrl.aggregation != 0` 时 `payload[0]` 实际是 delimiter 而非帧头，原代码会当普通 MPDU 解析。现已跳过。
- **事件注销范围**：`ESP_EVENT_ANY_BASE` / `ANY_ID` 会误注销其它处理器，已改为精确的 base + event id（4 处）。

### 串口日志关闭

`sdkconfig.defaults` 新增：

```
CONFIG_LOG_DEFAULT_LEVEL_NONE=y
CONFIG_LOG_MAXIMUM_LEVEL_NONE=y
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
CONFIG_BOOTLOG_ENABLE=n
```

采用 `_NONE` 而非 `CONFIG_LOG=n`，保留 log 组件但不编入任何格式串，串口不再有任何输出。同时移除了 `frame_analyzer_parser.c` 中 3 处不受 Kconfig 管辖的裸 `printf`（其中一处会把 PMKID 明文打印到串口）。

> ⚠️ **如需恢复调试日志**，把上述 4 项改回即可，例如将 `CONFIG_LOG_DEFAULT_LEVEL_NONE=y` 换成 `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`（并删除对应的 `_NONE` 行）。

### 过滤器重构

过滤掩码从运行时 `else if` 串联改为启动时一次性配置：

```c
wifictl_sniffer_start(channel, WIFICTL_SNIFF_PKT_DATA | WIFICTL_SNIFF_PKT_MGMT);
wifictl_sniffer_set_bssid_filter(bssid);   // 传入 NULL 则不过滤
```

原版 `wifictl_sniffer_filter_frame_types()` 用 `else if` 串联，三个参数只生效一个，已删除。

## 类似项目
* [risinek/esp32-wifi-penetration-tool](https://github.com/risinek/esp32-wifi-penetration-tool)（上游原版）
* [GANESH-ICMC/esp32-deauther](https://github.com/GANESH-ICMC/esp32-deauther)
* [SpacehuhnTech/esp8266_deauther](https://github.com/SpacehuhnTech/esp8266_deauther)
* [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder)
* [Jeija/esp32free80211](https://github.com/Jeija/esp32free80211)

## 免责声明
本项目展示了 Wi-Fi 网络及其底层 802.11 标准的漏洞，以及如何利用 ESP32 平台攻击这些漏洞点。请负责任地使用，仅对您有权限攻击的网络进行测试。

## 许可证
虽然本项目采用 MIT 许可证授权（详情见 [LICENSE](LICENSE) 文件），但请不要害羞或吝啬，分享您的工作成果。
