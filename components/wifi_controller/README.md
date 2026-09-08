# ESP32 Wi-Fi 渗透测试工具
## Wi-Fi Controller（Wi-Fi 控制器）组件

本组件封装了与 Wi-Fi 相关的操作，并提供简化的 API 供其他组件使用。

### Common（通用功能，wifi_controller）
提供 API 例如：使用给定配置启动和停止热点、控制 STA 连接、更改接口 MAC 地址等。

### AP Scanner（AP 扫描器，ap_scanner）
AP Scanner 提供扫描附近热点的 API，并将结果保存到数组中以供后续处理。

### Sniffer（嗅探器，sniffer）
Sniffer 用于将 ESP32 切换到混杂模式（或关闭），并捕获原始 802.11 帧。它提供过滤选项，并将捕获的帧作为 SNIFFER_EVENTS 事件发送到事件池。

## 参考
Doxygen API 参考可用
