#include "power_mgmt.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <time.h>

static const char *TAG = "power_mgmt";

#define NVS_NAMESPACE "power_mgmt"
#define NVS_KEY_ENABLED "enabled"
#define LEDC_MAX_DUTY 8191

// 夜间熄屏时间范围（方便测试修改）
#define NIGHT_START_H   18   // 熄屏开始：18:30
#define NIGHT_START_M   30
#define NIGHT_END_H      8   // 亮屏开始：08:50
#define NIGHT_END_M     50
#define WAKE_DURATION   60   // 触摸唤醒后亮屏秒数

typedef enum {
    SCREEN_ON,
    SCREEN_OFF,
    SCREEN_TEMP_WAKE
} screen_state_t;

static esp_lcd_panel_handle_t s_panel = NULL;
static bool s_enabled = false;
static screen_state_t s_state = SCREEN_ON;
static time_t s_wake_time = 0;
static float s_brightness = 0.5f;

static bool is_time_synced(void)
{
    return time(NULL) > 1700000000; // later than ~2023-11
}

static bool is_night_time(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int night_start = NIGHT_START_H * 60 + NIGHT_START_M;
    int night_end   = NIGHT_END_H * 60 + NIGHT_END_M;

    if (night_start > night_end) {
        // 跨午夜: e.g. 18:30 ~ 次日 08:50
        return (minutes >= night_start || minutes < night_end);
    } else {
        // 当天内窗口: e.g. 15:12 ~ 15:20
        return (minutes >= night_start && minutes < night_end);
    }
}

static void screen_turn_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_MAX_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    s_state = SCREEN_OFF;
    ESP_LOGI(TAG, "Screen off");
}

static void screen_turn_on(void)
{
    esp_lcd_panel_disp_on_off(s_panel, true);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(LEDC_MAX_DUTY * (1.0f - s_brightness)));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    s_state = SCREEN_ON;
    ESP_LOGI(TAG, "Screen on");
}

static void power_mgmt_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (!s_enabled) {
            if (s_state != SCREEN_ON) {
                screen_turn_on();
            }
            continue;
        }

        if (!is_time_synced()) {
            if (s_state != SCREEN_ON) {
                screen_turn_on();
            }
            continue;
        }

        if (!is_night_time()) {
            // Daytime: screen always on
            if (s_state != SCREEN_ON) {
                screen_turn_on();
            }
            continue;
        }

        // Night time logic
        switch (s_state) {
        case SCREEN_ON:
            screen_turn_off();
            break;
        case SCREEN_TEMP_WAKE:
            if (time(NULL) - s_wake_time >= WAKE_DURATION) {
                screen_turn_off();
            }
            break;
        case SCREEN_OFF:
        default:
            break;
        }
    }
    vTaskDelete(NULL);
}

void power_mgmt_init(esp_lcd_panel_handle_t panel)
{
    s_panel = panel;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 0;
        nvs_get_u8(nvs, NVS_KEY_ENABLED, &val);
        s_enabled = (val != 0);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Init done, power_save=%d", s_enabled);
    xTaskCreate(power_mgmt_task, "power_mgmt", 2048, NULL, 5, NULL);
}

void power_mgmt_set_enabled(bool enabled)
{
    s_enabled = enabled;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_ENABLED, enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    if (!enabled && s_state != SCREEN_ON) {
        screen_turn_on();
    }
}

bool power_mgmt_is_enabled(void)
{
    return s_enabled;
}

void power_mgmt_notify_touch(void)
{
    if (!s_enabled) return;

    if (s_state == SCREEN_OFF && is_night_time()) {
        screen_turn_on();
        s_state = SCREEN_TEMP_WAKE;
        s_wake_time = time(NULL);
        ESP_LOGI(TAG, "Woken by touch");
    }
}

void power_mgmt_set_brightness(float duty)
{
    s_brightness = duty;
}
