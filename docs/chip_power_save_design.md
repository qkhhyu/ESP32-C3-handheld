# 芯片功耗优化 实施记录

## 1. 需求概述

- **问题**：天气应用运行时 ESP32-C3 芯片发热较大，导致 PCB 温度上升，干扰 GXHTC3 温湿度传感器测量精度
- **目标**：降低芯片整体功耗，优先在天气应用前台时生效，减小自发热对传感器的影响
- **约束**：不影响天气应用 UI 流畅度，不影响 WiFi 通信成功率

## 2. 根因分析

### 2.1 功耗问题

| 功耗来源 | 原始配置 | 问题 |
|---|---|---|
| CPU 频率 | 160MHz | 满速运行 |
| 电源管理 | `CONFIG_PM_ENABLE` 未启用 | 空闲时无法进入 Light Sleep |
| 温湿度采样 | 每 100ms | 过度采样 |
| LVGL tick | 2ms（500Hz） | 高频中断 |
| 主循环 | 10ms | 100Hz 渲染 |

### 2.2 TLS 证书验证失败（附带发现）

开启 `PM_ENABLE` 后 HTTPS 请求 QWeather API 时 TLS 证书验证失败：
- 错误码 `0x4290` — RSA 签名验证失败
- 根因：LVGL 20 行 DMA 缓冲区（25.6KB）+ 音频 I2S DMA 缓冲区消耗了 ESP32-C3 的 61KB DRAM，mbedtls RSA 2048 位签名验证时找不到足够的连续 DRAM
- 解决：编译优化从 `-Og` 改为 `-Os`，释放 ~20KB 代码空间给 DRAM 堆

### 2.3 时间同步问题（附带发现）

- 原有 `weather_sync_time()` 为每个 NTP 服务器调用 `esp_netif_sntp_init/deinit`，反复 init/deinit 破坏 SNTP 状态
- 且 `weather_sync_time_once()` 在 `sync_wait` 返回非超时错误时误判为成功
- 解决：WiFi 连上后单次 `esp_netif_sntp_init` + `sync_wait` 循环 + `deinit`，最后用 `time(NULL) > 1700000000` 实际验证

## 3. 实施方案

### 3.1 sdkconfig 改动

```
CONFIG_PM_ENABLE=y               # 自动 Light Sleep
CONFIG_PM_DFS_INIT_AUTO=y        # 自动变频 40~160MHz
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y  # 减少无意义唤醒
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80   # 默认 80MHz（WiFi 驱动自动升到 160MHz）
CONFIG_COMPILER_OPTIMIZATION_SIZE=y  # -Os 优化，释放 DRAM
```

### 3.2 代码改动

| 文件 | 改动 | 目的 |
|------|------|------|
| `main/spi_lcd_touch_example_main.c` | LVGL tick 2→10ms，主循环 10→30ms，温湿度动态采样，chip_power 初始化 | 更多空闲窗口让 CPU 休眠 |
| `main/weather_service.c` | WiFi 连上后单次阻塞 SNTP 同步，移除错误的循环 init/deinit | 确保时间正确 |
| `main/lvgl_demo_ui.c` | 进出天气应用时调用 chip_power | 天气前台降低采样频率 |
| `main/power_mgmt.c` | 熄屏检测轮询 1s→2s | 减少唤醒 |
| `main/chip_power.c/h` | 新增：动态采样间隔管理（正常 1s，天气前台 3s） | 降低传感器采样 |
| `main/CMakeLists.txt` | 添加 chip_power.c | — |

### 3.3 功耗预期

| 状态 | 优化前 | 优化后 |
|------|--------|--------|
| CPU 频率 | 160MHz 恒定 | 80MHz 默认，空闲 DFS→40MHz，WiFi 自动→160MHz |
| 空闲电流 | ~20mA | ~0.5mA（Light Sleep） |
| 温湿度采样 | 100ms 间隔 | 正常 1s，天气前台 3s |
| LVGL 刷新 | 500Hz tick，100Hz handler | 100Hz tick，33Hz handler |
| 熄屏夜间 | 屏幕常亮 | 关背光+显示休眠 |

## 4. 调试过程关键发现

1. TLS 证书验证失败不是证书包或时间问题——是 **DRAM 不足**导致 RSA 运算出错
2. 反复 `esp_netif_sntp_init/deinit` 会破坏网络栈状态，必须单次 init
3. `sync_wait` 返回非超时错误码时不能假设时间已同步，必须用 `time(NULL)` 实际验证
4. WiFi 驱动在通信时自动持有 PM 锁升频到 160MHz，默认 80MHz 不影响 TLS
