# Handheld Weather Desktop for ESP32-C3

This project is based on the Lichuang ESP32-C3 handheld demo and replaces the first home screen app with a weather desktop app migrated from the `desktop_weather` project.

When you tap the first icon on the launcher, the device opens the weather app and shows:

- network time
- current weather icon and text
- air quality level
- outdoor temperature and humidity
- indoor temperature and humidity from the onboard sensor
- animated GIF background

The other original apps are kept in place.

## Features

- Weather desktop integrated as the first launcher app
- Wi-Fi based network time sync
- QWeather current weather, daily forecast, and air quality
- Indoor temperature and humidity from GXHTC3
- Up-swipe gesture to exit the app and return to the launcher
- GIF background enabled in LVGL

## Hardware and Software

- Board: Lichuang ESP32-C3 handheld device
- ESP-IDF: `v5.5.1`
- UI framework: `LVGL`

Main peripherals used by this project:

- `ST7789` LCD
- `FT6336` touch controller
- `GXHTC3` temperature and humidity sensor
- `QMI8658C` IMU
- `QMC5883L` compass
- `ES8311` audio codec

## Project Structure

Important files:

- `main/lvgl_demo_ui.c`
  - launcher UI and app entry logic
  - the first icon opens the weather app
- `main/weather_app.c`
  - weather app layout and periodic UI refresh
- `main/weather_service.c`
  - Wi-Fi, SNTP, QWeather requests, and cached weather data
- `main/spi_lcd_touch_example_main.c`
  - board init, LVGL setup, and main entry
- `main/font_alipuhui.c`
- `main/font_qweather.c`
- `main/font_led.c`
- `main/image_taikong.c`

## Before You Build

### 1. Configure Wi-Fi

Edit [sdkconfig.defaults](sdkconfig.defaults) and replace the placeholders:

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

- `QWEATHER_LOCATION_ID`: target city or district code
- `QWEATHER_API_KEY`: your own QWeather developer key

`main/weather_secrets.h` is ignored by Git, so your real key stays local.
The project will still compile without a valid API key, but online weather data will not load.

## Build and Flash

```bash
idf.py build
idf.py -p PORT flash monitor
```

To exit monitor:

```bash
Ctrl+]
```

## Interaction

- Boot into the launcher
- Tap the first icon to enter the weather app
- Swipe up in the app to return to the launcher

## Notes

- Time sync first tries `pool.ntp.org`, then falls back to other NTP servers
- `LV_USE_GIF` is enabled for the animated background
- `.gitignore` is prepared for public GitHub publishing
- `sdkconfig`, `build`, and `managed_components` are intentionally ignored

## Repository Cleanup for Publishing

The GitHub-ready cleanup in this repo includes:

- removed hardcoded Wi-Fi credentials from tracked defaults
- moved the QWeather API key into a local ignored header file
- added an ESP-IDF friendly `.gitignore`
- kept the current working weather desktop integration

## Credits

This project is derived from the original Lichuang ESP32-C3 handheld example and integrates the weather desktop experience from the `desktop_weather` project.
