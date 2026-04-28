# ESP32-C3 掌机天气桌面

[English](README.md) | [简体中文](README_ZH.md)

## 项目简介

本项目基于立创实战派 ESP32-C3 掌机例程改造而来，将主界面第一个应用替换为从 `desktop_weather` 工程移植过来的天气桌面应用。

点击主界面第一个图标后，会进入天气应用，显示以下信息：

- 网络时间
- 实时天气图标与天气描述
- 空气质量等级
- 室外温湿度
- 室内温湿度
- GIF 动态背景

原有其余应用保持不变。

## 功能特性

- 将天气桌面集成为主界面第一个应用
- 通过 Wi-Fi 同步网络时间
- 通过和风天气获取实时天气、天气预报和空气质量
- 读取板载 `GXHTC3` 显示室内温湿度
- 支持上滑退出并返回主界面
- 已启用 LVGL GIF 解码显示动态背景

## 硬件与软件环境

- 开发板：立创实战派 ESP32-C3 掌机
- 开发框架：`ESP-IDF v5.5.1`
- 图形框架：`LVGL`

本项目主要使用到的板载外设：

- `ST7789` LCD
- `FT6336` 触摸
- `GXHTC3` 温湿度传感器
- `QMI8658C` IMU
- `QMC5883L` 电子罗盘
- `ES8311` 音频编解码器

## 主要文件

- `main/lvgl_demo_ui.c`
  - 主界面 UI 与应用入口逻辑
  - 第一个图标进入天气应用
- `main/weather_app.c`
  - 天气应用界面布局与定时刷新
- `main/weather_service.c`
  - Wi-Fi、SNTP、和风天气请求及天气数据缓存
- `main/weather_secrets.example.h`
  - 本地天气配置模板
- `main/spi_lcd_touch_example_main.c`
  - 板级初始化、LVGL 初始化与主入口
- `main/font_alipuhui.c`
- `main/font_qweather.c`
- `main/font_led.c`
- `main/image_taikong.c`

## 编译前配置

### 1. 配置 Wi-Fi

编辑 `sdkconfig.defaults`，将占位值替换成你自己的 Wi-Fi 信息：

```ini
CONFIG_EXAMPLE_WIFI_SSID="YOUR_WIFI_SSID"
CONFIG_EXAMPLE_WIFI_PASSWORD="YOUR_WIFI_PASSWORD"
```

也可以通过 `idf.py menuconfig` 进行配置。

### 2. 配置和风天气

先将 `main/weather_secrets.example.h` 复制为 `main/weather_secrets.h`，然后修改：

```c
#define QWEATHER_LOCATION_ID "101280605"
#define QWEATHER_API_KEY     "YOUR_QWEATHER_API_KEY"
```

- `QWEATHER_LOCATION_ID`：目标城市或区县 ID
- `QWEATHER_API_KEY`：你自己的和风天气开发者 Key

`main/weather_secrets.h` 已加入 `.gitignore`，不会被提交到 Git 仓库。

## 首次运行前检查

以下内容参考立创官方文档中“让例程跑起来”一节，并结合本项目做了适配：

参考文档：
https://wiki.lckfb.com/zh-hans/szpi-esp32c3/beginner/comprehensive-routines.html

首次烧录前请确认：

1. 已配置 Wi-Fi 名称和密码。
2. 已配置和风天气 `location ID` 与 `API key`。
   原始例程是在 `spi_lcd_touch_example_main.c` 中修改；
   本项目改为使用 `main/weather_secrets.h`。
3. 已将目标芯片设置为 `esp32c3`。
4. 已在 `menuconfig` 中确认以下选项：
   - Flash 大小为 `8 MB`
   - 分区表使用本工程的自定义分区
   - LVGL 缓冲区或缓存大小为 `64 KB`
   - 已启用 LVGL GIF 解码器
5. 已选择正确串口，并使用串口下载方式。
6. 执行编译、烧录和串口监视。

设备启动后，会依次完成板级初始化、连接 Wi-Fi、同步网络时间、拉取天气数据，然后进入主界面。本项目中，天气桌面位于主界面第一个应用入口。

## 编译与烧录

```bash
idf.py build
idf.py -p PORT flash monitor
```

退出串口监视器：

```bash
Ctrl+]
```

## 交互说明

- 开机进入主界面
- 点击第一个图标进入天气应用
- 在应用界面上滑返回主界面

## 说明

- 网络时间同步会优先尝试 `pool.ntp.org`，失败后回退到其他 NTP 服务器
- 已启用 `LV_USE_GIF` 用于动态背景显示
- `.gitignore` 已按公开仓库发布做过整理
- `sdkconfig`、`build`、`managed_components` 默认不提交

## 致谢

本项目基于立创实战派 ESP32-C3 掌机原始例程，并融合了 `desktop_weather` 工程中的天气桌面功能。
