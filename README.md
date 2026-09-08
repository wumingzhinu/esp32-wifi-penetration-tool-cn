# ESP32 Wi-Fi 渗透测试工具

本项目为 ESP32 平台提供了一个通用工具，用于实现各种 Wi-Fi 攻击。它提供了一些 Wi-Fi 攻击中常用的通用功能，使实现新攻击变得更加简单。它还包含了 Wi-Fi 攻击本身的实现，例如从握手中捕获 PMKID，或通过不同方法捕获握手本身（如启动伪造的重复热点或直接发送解除认证帧等）...

显然，破解哈希不是本项目的一部分，因为 ESP32 无法以高效的方式破解哈希。其余的工作都可以在这个小型、廉价、低功耗的 SoC 上完成。

<p align="center">
    <img src="doc/images/logo.png" alt="Logo">
</p>

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
- 以及更多...

### 演示视频
[![演示视频（YouTube）](https://img.youtube.com/vi/9I3BxRu86GE/0.jpg)](https://www.youtube.com/watch?v=9I3BxRu86GE)


## 使用方法
1. [编译](#编译)并[烧录](#烧录)项目到 ESP32（开发板或模块）
1. 给 ESP32 供电
1. 管理热点在启动后自动开启
1. 连接到此热点\
默认配置：
*SSID:* `ManagementAP`，*密码:* `mgmtadmin`
1. 在浏览器中打开 `192.168.4.1`，您将看到一个用于配置和控制工具的 Web 客户端，如下所示：

    ![Web 客户端界面](doc/images/ui-config.png)

## 编译
本项目目前使用 ESP-IDF 4.1（commit `5ef1b390026270503634ac3ec9f1ec2e364e23b2`）开发。在更新版本上可能无法正常工作。

项目可以按照常规的 ESP-IDF 方式编译：

```shell
idf.py build
```

本项目不支持使用 `make` 的传统编译方式。

## 烧录
如果您已配置好 ESP-IDF，最简单的方式是使用 `idf.py flash`。

如果您不想配置完整的 ESP-IDF，可以使用 [`build/`](build/) 中预编译的二进制文件，并通过 [`esptool.py`](https://github.com/espressif/esptool)（需要 Python）进行烧录。

示例命令（请遵循 [esptool 仓库](https://github.com/espressif/esptool)中的说明）：
```
esptool.py -p /dev/ttyS5 -b 115200 --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x8000 build/partition_table/partition-table.bin 0x1000 build/bootloader/bootloader.bin 0x10000 build/esp32-wifi-penetration-tool.bin
```

在 Windows 上，您可以使用官方的 [Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)。

## 文档
### Wi-Fi 攻击
本项目中的攻击实现在 [main 组件 README](main/) 中有详细描述。这些攻击背后的原理位于 [doc/ATTACKS_THEORY.md](doc/ATTACKS_THEORY.md)
### API 参考
本项目使用 Doxygen 标注来记录组件 API 和实现。项目中包含 Doxyfile，如果您想生成 API 参考，只需在根目录运行 `doxygen` 即可。它将在 `doc/api/html` 中生成 HTML API 参考。

### 组件
本项目由多个组件组成，这些组件可以在其他项目中复用。每个组件都有自己的 README，包含详细描述。以下是各组件的简要说明：

- [**Main（主组件）**](main) 是本项目的入口点。所有必要的初始化步骤都在这里完成。管理热点在此启动，控制权随后交给 Web 服务器。
- [**Wifi Controller（Wi-Fi 控制器）**](components/wifi_controller) 组件封装了所有与 Wi-Fi 相关的操作。用于启动热点、以 STA 模式连接、扫描附近热点等。
- [**Webserver（Web 服务器）**](components/webserver) 组件提供用于配置攻击的 Web 界面。它要求热点已启动，且未启用 SSL 加密等额外安全功能。
- [**Wi-Fi Stack Libraries Bypasser（Wi-Fi 协议栈库绕过器）**](components/wsl_bypasser) 组件绕过 Wi-Fi 协议栈库对发送某些类型的任意 802.11 帧的限制。
- [**Frame Analyzer（帧分析器）**](components/frame_analyzer) 组件处理捕获的帧，并向其他组件提供解析功能。
- [**PCAP Serializer（PCAP 序列化器）**](components/pcap_serializer) 组件将捕获的帧序列化为 PCAP 二进制格式，并提供给其他组件（主要用于 Web 服务器/界面）
- [**HCCAPX Serializer（HCCAPX 序列化器）**](components/hccapx_serializer) 组件将捕获的帧序列化为 HCCAPX 二进制格式，并提供给其他组件（主要用于 Web 服务器/界面）

### 延伸阅读
* [关于本项目的学术论文（PDF）](https://excel.fit.vutbr.cz/submissions/2021/048/48.pdf)

## 硬件
本项目主要在 **ESP32-DEVKITC-32E** 上构建和测试，但对于任何 **ESP32-WROOM-32** 模块应该没有差异。

<p align="center">
    <img src="doc/images/soucastky_8b.png" alt="硬件组件" width="400">
</p>

在下图中，您可以看到一个由电池（锂聚合物电池）供电的 ESP32 DevKitC，使用了以下硬件：
- **ESP32-DEVKITC-32E**（价格 213 CZK/8.2 EUR/9.6 美元）
- 220mAh 锂聚合物 3.7V 电池（重约 5g，价格 77 CZK/3 EUR/3.5 美元）
- MCP1702-3302ET 降压 3.3V 稳压器（价格 11 CZK/0.42 EUR/0.50 美元）
- 捷克 5 克朗硬币作为比例参照（重 4.8g，直径 23mm，价格 0.19 EUR/0.23 美元）
<p align="center">
    <img src="doc/images/mini.jpg" alt="硬件组件" width="300">
    <img src="doc/images/mini2.jpg" alt="硬件组件" width="300">
</p>

总计（不含硬币），此配置重约 17g。通过使用更小的锂聚合物电池和直接使用 ESP32-WROOM-32 模块而非整块开发板，可以进一步减小体积。

此配置花费约 300 CZK（约 11.50 EUR/13.50 美元）。直接使用模块（约 80 CZK/约 3 EUR/3.5 美元），总成本可降至 160 CZK（约 6.5 EUR/7.5 美元），这使得该工具非常便宜，几乎人人都能负担。

### 功耗
根据实验测量，ESP32 在执行攻击时功耗约 100mA。

## 类似项目
* [GANESH-ICMC/esp32-deauther](https://github.com/GANESH-ICMC/esp32-deauther)
* [SpacehuhnTech/esp8266_deauther](https://github.com/SpacehuhnTech/esp8266_deauther)
* [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder)
* [EParisot/esp32-network-toolbox](https://www.tindie.com/products/klhnikov/esp32-network-toolbox/)
* [Jeija/esp32free80211](https://github.com/Jeija/esp32free80211)

## 贡献
欢迎贡献代码。请不要犹豫对现有代码库进行重构。在为新函数和文件添加注释时，请遵循 Doxygen 标注规范。本项目主要用于教育和演示目的，因此欢迎详细的文档。

## 免责声明
本项目展示了 Wi-Fi 网络及其底层 802.11 标准的漏洞，以及如何利用 ESP32 平台攻击这些漏洞点。请负责任地使用，仅对您有权限攻击的网络进行测试。

## 许可证
虽然本项目采用 MIT 许可证授权（详情见 [LICENSE](LICENSE) 文件），但请不要害羞或吝啬，分享您的工作成果。
