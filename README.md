# ESP32-C3 Handheld Weather Desktop

[English](README.md) | [简体中文](README_ZH.md)

This project is based on the LCKFB ESP32-C3 handheld demo and replaces the first launcher app with a weather desktop app migrated from the `desktop_weather` project.

When you tap the first icon on the launcher, the device opens the weather app and shows:

- network time
- current weather icon and description
- air quality level
- outdoor temperature and humidity
- indoor temperature and humidity
- animated GIF background

The other original apps remain unchanged.

## Features

- Weather desktop integrated as the first launcher app
- Network time synchronization over Wi-Fi
- QWeather current weather, forecast, and air quality
- Indoor temperature and humidity from the onboard `GXHTC3`
- Swipe up to exit and return to the launcher
- LVGL GIF decoding enabled for animated background

## Hardware and Software

- Board: LCKFB ESP32-C3 handheld device
- Framework: `ESP-IDF v5.5.1`
- UI framework: `LVGL`

Main onboard peripherals used in this project:

- `ST7789` LCD
- `FT6336` touch controller
- `GXHTC3` temperature and humidity sensor
- `QMI8658C` IMU
- `QMC5883L` compass
- `ES8311` audio codec

## Important Files

- `main/lvgl_demo_ui.c`
  - launcher UI and app entry logic
  - the first icon opens the weather app
- `main/weather_app.c`
  - weather app layout and periodic UI refresh
- `main/weather_service.c`
  - Wi-Fi, SNTP, QWeather requests, and cached weather data
- `main/weather_secrets.example.h`
  - template for local weather configuration
- `main/spi_lcd_touch_example_main.c`
  - board init, LVGL init, and main entry
- `main/font_alipuhui.c`
- `main/font_qweather.c`
- `main/font_led.c`
- `main/image_taikong.c`

## Configuration Before Build

### 1. Configure Wi-Fi

Edit `sdkconfig.defaults` and replace the placeholders:

```ini
CONFIG_EXAMPLE_WIFI_SSID="YOUR_WIFI_SSID"
CONFIG_EXAMPLE_WIFI_PASSWORD="YOUR_WIFI_PASSWORD"
```

You can also configure these values through `idf.py menuconfig`.

### 2. Configure QWeather

Copy `main/weather_secrets.example.h` to `main/weather_secrets.h`, then edit:

```c
#define QWEATHER_LOCATION_ID "101280605"
#define QWEATHER_API_KEY     "YOUR_QWEATHER_API_KEY"
```

- `QWEATHER_LOCATION_ID`: target city or district ID
- `QWEATHER_API_KEY`: your own QWeather developer key

`main/weather_secrets.h` is ignored by Git so your real key stays local.

## First Run Checklist

The checklist below is adapted from the official LCKFB guide section about getting the original weather example running, with adjustments for this migrated project.

Reference:
https://wiki.lckfb.com/zh-hans/szpi-esp32c3/beginner/comprehensive-routines.html

Before flashing for the first time, make sure:

1. Your Wi-Fi SSID and password are configured.
2. Your QWeather `location ID` and `API key` are configured.
   In the original example, these values were edited in `spi_lcd_touch_example_main.c`.
   In this project, they are now placed in `main/weather_secrets.h`.
3. The target chip is set to `esp32c3`.
4. The following `menuconfig` options are confirmed:
   - Flash size is `8 MB`
   - The partition table uses this project's custom partition layout
   - LVGL buffer or cache size is `64 KB`
   - LVGL GIF decoder is enabled
5. The correct serial port is selected and serial download is used.
6. You build, flash, and open the serial monitor.

After boot, the device initializes the board, connects to Wi-Fi, synchronizes network time, fetches weather data, and then enters the launcher. In this project, the weather desktop is opened from the first app icon on the home screen.

## Build and Flash

```bash
idf.py build
idf.py -p PORT flash monitor
```

To exit the serial monitor:

```bash
Ctrl+]
```

## Interaction

- Boot into the launcher
- Tap the first icon to open the weather app
- Swipe up in the app to return to the launcher

## Notes

- Time sync first tries `pool.ntp.org`, then falls back to other NTP servers
- `LV_USE_GIF` is enabled for the animated background
- `.gitignore` has been prepared for public GitHub publishing
- `sdkconfig`, `build`, and `managed_components` are ignored by default

## Credits

This project is derived from the original LCKFB ESP32-C3 handheld example and integrates the weather desktop experience from the `desktop_weather` project.
