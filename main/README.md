# ESP32 Wi-Fi 渗透测试工具
## Main（主组件）

本组件是主组件（也称为伪组件）。它包含攻击实现本身以及攻击包装框架。

以下攻击实现背后的原理位于 [/doc/ATTACK_THEORY.md](../doc/ATTACKS_THEORY.md)。

### 广播解除认证
发送解除认证帧的一种方式是绕过阻止其发送的 Wi-Fi 协议栈库。为此使用 [WSL Bypasser](../components/wsl_bypasser) 组件。有关绕过原理的更多细节，请参见 WSL Bypasser 组件的 README。

解除认证帧以广播目标 MAC 地址（ff:ff:ff:ff:ff:ff）、源 MAC 地址和目标 AP 的 BSSID 构建。

#### 优点
- 不需要存在正在进行的活跃通信。即使设备未 actively 通信，也能收到此帧。

#### 缺点
- 一些设备忽略广播解除认证帧，如 [Aircrack-ng 文档](https://www.aircrack-ng.org/doku.php?id=deauthentication#why_does_deauthentication_not_work)所述：
    > 一些客户端忽略广播解除认证帧。如果是这种情况，您需要向特定客户端发送定向解除认证帧。

### 伪造热点
另一种选择是启动伪造的重复热点。这样 Wi-Fi 协议栈库保持不变，仅使用 ESP-IDF API。考虑到可以为热点接口设置任何有效的 MAC 地址，我们可以通过 `esp_wifi_set_mac` 设置与真实热点相同的 MAC 来创建重复热点。
我们从 AP 扫描器获知所有必要的值，并将它们保存在 `wifi_ap_record_t` 结构中。从中我们可以将信息传递给 `wifi_config_t` 并通过 `esp_wifi_set_config` 配置热点。一旦此热点启动，只要它收到来自任何 STA 的 Class 2 或 3 帧，就会回复解除认证帧。此行为直接由 802.11 标准定义。STA 无法验证帧是来自真实热点还是伪造热点，出于防御目的会自行解除认证。
此过程如下图时序图所示：

![伪造热点时序图](../doc/drawio/rogueap-seq.drawio.svg)


#### 优点
- 解除认证帧定向发送给向 AP 发送了某帧的 STA。

#### 缺点
- 此方法需要存在活跃通信。
- 它可能完全使 STA 困惑，导致其无法再次认证，或者可能尝试向伪造热点而非真实热点进行认证。（可通过反复开启和关闭重复热点，给 STA 一些重连时间来解决）


### PMKID 捕获
要从 AP 捕获 PMKID，我们只需发起连接并从 AP 获取第一条握手消息。如果 PMKID 可用，AP 会将其作为第一条握手消息的一部分发送，因此我们不知道凭据也没关系。

### 拒绝服务
此方式复用上述解除认证方法，仅跳过握手捕获。它还允许组合所有解除认证方法，使其对各种设备的不同行为更加稳健。

## 参考
Doxygen API 参考可用
