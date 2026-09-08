# ESP32 Wi-Fi 渗透测试工具
## Webserver（Web 服务器）组件

本组件为用户提供与工具交互的界面。

它基于 `esp_http_server` 子组件构建，官方说明请参见 [ESP-IDF 参考文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)。

HTML 页面存储在 RAM 中，以常量字符数组的形式定义在 `pages/` 目录下。目前只提供一个页面，通过 JavaScript 客户端的 AJAX 调用进行动态更新。
Web 服务器通过调用 `webserver_run()` 启动，该函数注册所有可用的端点并一直运行直到 ESP32 关闭。

### 端点
此 Web 服务器实现了几个供 JavaScript 客户端使用的端点：
- **`/`** 显示 index.html 页面
- **`/status`** 以二进制格式返回攻击状态
- **`/reset`** 告诉应用程序将攻击状态重置为默认的 READY（就绪）状态
- **`/ap-list`** 扫描附近的热点并以表格形式显示
- **`/run-attack`** 将配置发送回应用程序
- **`/capture.pcap`** 提供 PCAP 格式文件供下载
- **`/capture.hccapx`** 提供 HCCAPX 格式文件供下载

### JavaScript 客户端
端点通过 `index.html` 页面上提供的 JavaScript AJAX 调用来访问。它还将 Web 服务器的二进制响应解析为人类可读的形式。
它还应执行所有不需要在 ESP32 上运行的额外计算，以最大限度地降低 ESP32 的功耗。

## 工具
为了使 Web 客户端的开发更方便，项目中提供了一个脚本 `utils/convert_html_to_header_file.sh`，它将标准 HTML 文件转换为头文件并进行格式化，使其可以编译。

## 参考
Doxygen API 参考可用

**注意：** `pages/` 中的头文件由 `utils/` 文件夹中的脚本生成，因此不包含文档字符串。
