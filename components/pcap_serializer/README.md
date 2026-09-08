# ESP32 Wi-Fi 渗透测试工具
## PCAP Serializer（PCAP 序列化器）组件

本组件将提供的帧格式化为 PCAP 二进制格式。

它基于 [Wireshark 的 LibPCAP 文件格式参考](https://gitlab.com/wireshark/wireshark/-/wikis/Development/LibpcapFileFormat)。
它简单地将新帧附加到结构化的缓冲区中，并可以按需获取该缓冲区。

## 使用方法
1. 首先通过调用 `pcap_serializer_init()` 初始化新的 PCAP 文件缓冲区。
1. 然后使用 `pcap_serializer_append_frame()` 向文件中添加更多帧。
1. 要获取缓冲区，请调用 `pcap_serializer_get_buffer()` 和 `pcap_serializer_get_size()`。

## 参考
Doxygen API 参考可用
