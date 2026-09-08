# ESP32 Wi-Fi 渗透测试工具
## Wi-Fi Stack Libraries (WSL) Bypasser（Wi-Fi 协议栈库绕过器）组件

本组件的主要目的是绕过 Wi-Fi 协议栈库的阻止机制，该机制会阻止发送某些类型的原始 802.11 帧。

它基于 [ESP32-Deauther](https://github.com/GANESH-ICMC/esp32-deauther) 项目，其中用于检查帧缓冲区中帧类型的函数[通过 Ghidra 工具反编译](https://github.com/GANESH-ICMC/esp32-deauther/issues/9)，并找到了其名称 `ieee80211_raw_frame_sanity_check`。

此绕过的原理是在编译期间使用链接器标志来允许多个函数定义——`-Wl,-zmuldefs`。这使得本组件能够覆盖默认函数的行为，并始终返回允许 Wi-Fi 协议栈库继续传输帧缓冲区的值。

这是通过 [CMakeLists.txt](CMakeLists.txt) 中的以下行实现的：
```cmake
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
```

该函数本身在 [wsl_bypasser.c](wsl_bypasser.c) 中定义如下：
```c
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}
```
