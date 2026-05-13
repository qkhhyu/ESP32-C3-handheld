#include "chip_power.h"
#include "esp_log.h"

static const char *TAG = "chip_power";

#define SAMPLE_INTERVAL_NORMAL_MS   1000
#define SAMPLE_INTERVAL_LOW_POWER_MS 3000

static int s_sample_interval_ms = SAMPLE_INTERVAL_NORMAL_MS;

void chip_power_init(void)
{
    s_sample_interval_ms = SAMPLE_INTERVAL_NORMAL_MS;
    ESP_LOGI(TAG, "Init done, sample_interval=%d ms", s_sample_interval_ms);
}

void chip_power_enter_low_power(void)
{
    s_sample_interval_ms = SAMPLE_INTERVAL_LOW_POWER_MS;
    ESP_LOGI(TAG, "Enter low power mode, sample_interval=%d ms", s_sample_interval_ms);
}

void chip_power_exit_low_power(void)
{
    s_sample_interval_ms = SAMPLE_INTERVAL_NORMAL_MS;
    ESP_LOGI(TAG, "Exit low power mode, sample_interval=%d ms", s_sample_interval_ms);
}

int chip_power_get_sample_interval_ms(void)
{
    return s_sample_interval_ms;
}
