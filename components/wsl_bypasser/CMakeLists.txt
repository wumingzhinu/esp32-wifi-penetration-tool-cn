idf_component_register(
    SRCS "wsl_bypasser.c"
    INCLUDE_DIRS "interface"
)
# 使用 --wrap 强制重定向 ieee80211_raw_frame_sanity_check 到 __wrap_ 版本。
# 相比 -zmuldefs，--wrap 不依赖链接顺序，保证覆盖一定生效（ESP32 与 ESP32-C3 通用）。
target_link_libraries(${COMPONENT_LIB} -Wl,--wrap=ieee80211_raw_frame_sanity_check)