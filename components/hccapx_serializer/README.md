# ESP32 Wi-Fi 渗透测试工具
## HCCAPX Serializer（HCCAPX 序列化器）组件

本组件将提供的帧格式化为 HCCAPX（[hashcat](https://hashcat.net/hashcat/)）二进制格式。

它基于 [Hashcat 的 HCCAPX 文件格式参考](https://hashcat.net/wiki/doku.php?id=hccapx)。
它解析提供的 EAPoL-Key 数据包（使用 [Frame Analyzer 组件](../frame_analyzer)），这些数据包是 WPA 握手的一部分，并构建 HCCAPX 格式的文件，之后可以直接提供给 hashcat 来破解 PSK（预共享密钥，通常称为*网络密码*）。

## 使用方法
1. 首先通过调用 `hccapx_serializer_init` 并提供目标 AP 的 SSID 来初始化序列化器。
1. 通过调用 `hccapx_serializer_add_frame()` 添加更多握手帧。
1. 获取存储 HCCAPX 二进制数据的缓冲区指针 `hccapx_serializer_get()`。

## 参考
Doxygen API 参考可用
