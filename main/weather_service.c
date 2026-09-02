#include "weather_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "protocol_examples_common.h"
#include "weather_secrets.h"
#include "zlib.h"

static const char *TAG = "weather_service";

#define WEATHER_HTTP_BUFFER_SIZE 4096
#define WEATHER_RETRY_MS         (60 * 1000)
#define WEATHER_REFRESH_MS       (30 * 60 * 1000)

#define QWEATHER_DAILY_URL "https://devapi.qweather.com/v7/weather/3d?location=" QWEATHER_LOCATION_ID "&key=" QWEATHER_API_KEY
#define QWEATHER_NOW_URL   "https://devapi.qweather.com/v7/weather/now?location=" QWEATHER_LOCATION_ID "&key=" QWEATHER_API_KEY
#define QAIR_NOW_URL       "https://devapi.qweather.com/v7/air/now?location=" QWEATHER_LOCATION_ID "&key=" QWEATHER_API_KEY

static SemaphoreHandle_t s_weather_lock;
static char *s_http_compressed_buffer;
static char *s_http_json_buffer;
static int s_http_rx_len; // 本次请求实际收到的字节数（chunked 响应没有 content-length）
static weather_snapshot_t s_weather_snapshot = {
    .status = "正在获取天气信息",
};
static bool s_weather_started;
static esp_timer_handle_t s_wifi_connect_watchdog;

static void weather_set_status(const char *status)
{
    if (s_weather_lock == NULL) {
        return;
    }

    xSemaphoreTake(s_weather_lock, portMAX_DELAY);
    snprintf(s_weather_snapshot.status, sizeof(s_weather_snapshot.status), "%s", status);
    xSemaphoreGive(s_weather_lock);
}

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        // 不区分是否 chunked：gzip 响应经常以 chunked 方式传输，
        // 原来只在非 chunked 时拷贝会把数据整个丢掉。
        if (evt->user_data) {
            int remaining = WEATHER_HTTP_BUFFER_SIZE - s_http_rx_len - 1;
            int copy_len = evt->data_len < remaining ? evt->data_len : remaining;
            if (copy_len > 0) {
                memcpy((char *)evt->user_data + s_http_rx_len, evt->data, copy_len);
                s_http_rx_len += copy_len;
            }
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

// 解析 gzip 头部长度（跳过可选字段），失败返回 -1
static int gzip_header_len(const unsigned char *src, int src_len)
{
    if (src_len < 10 || src[0] != 0x1f || src[1] != 0x8b || src[2] != 8) {
        return -1;
    }

    int flags = src[3];
    int pos = 10;
    if (flags & 0x04) { // FEXTRA
        if (src_len < pos + 2) {
            return -1;
        }
        pos += 2 + (src[pos] | (src[pos + 1] << 8));
    }
    if (flags & 0x08) { // FNAME：以 \0 结尾
        while (pos < src_len && src[pos]) pos++;
        pos++;
    }
    if (flags & 0x10) { // FCOMMENT：以 \0 结尾
        while (pos < src_len && src[pos]) pos++;
        pos++;
    }
    if (flags & 0x02) { // FHCRC
        pos += 2;
    }
    return (pos < src_len) ? pos : -1;
}

static int inflate_raw(char *src, int src_len, char *dst, int *dst_len, int window_bits)
{
    z_stream strm = {
        .zalloc = Z_NULL,
        .zfree = Z_NULL,
        .opaque = Z_NULL,
        .avail_in = src_len,
        .avail_out = *dst_len,
        .next_in = (Bytef *)src,
        .next_out = (Bytef *)dst,
    };

    int ret = inflateInit2(&strm, window_bits);
    if (ret != Z_OK) {
        return ret;
    }

    ret = inflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_END) {
        *dst_len = strm.total_out;
    }
    inflateEnd(&strm);

    return ret;
}

static int gzip_decompress(char *src, int src_len, char *dst, int *dst_len)
{
    // 本机堆内存紧张（TLS 峰值时只剩几 KB）：先尝试 2KB 小窗口
    // raw inflate，内存峰值比标准 32KB 窗口低约 30KB。天气 JSON 很小，
    // 回溯距离通常远小于 2KB；个别流不满足时回退标准 gzip 模式。
    int header = gzip_header_len((const unsigned char *)src, src_len);
    if (header > 0) {
        int ret = inflate_raw(src + header, src_len - header, dst, dst_len, -11);
        if (ret == Z_STREAM_END) {
            return ret;
        }
        ESP_LOGW(TAG, "small window inflate failed(%d), fallback to full window", ret);
    }

    return inflate_raw(src, src_len, dst, dst_len, 31);
}

static esp_err_t weather_http_buffers_init(void)
{
    if (s_http_compressed_buffer == NULL) {
        s_http_compressed_buffer = calloc(1, WEATHER_HTTP_BUFFER_SIZE);
        if (s_http_compressed_buffer == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_http_json_buffer == NULL) {
        s_http_json_buffer = calloc(1, WEATHER_HTTP_BUFFER_SIZE);
        if (s_http_json_buffer == NULL) {
            free(s_http_compressed_buffer);
            s_http_compressed_buffer = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static esp_err_t http_get_gzip_json(const char *url, char *json_buffer, size_t json_buffer_size)
{
    ESP_RETURN_ON_ERROR(weather_http_buffers_init(), TAG, "allocate weather http buffer failed");

    memset(s_http_compressed_buffer, 0, WEATHER_HTTP_BUFFER_SIZE);
    s_http_rx_len = 0;
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = s_http_compressed_buffer,
        .timeout_ms = 15000, // 网络异常时避免 perform 无限期阻塞后台任务
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    int rx_len = s_http_rx_len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return err;
    }

    if (status_code != 200 || rx_len <= 0 || rx_len >= WEATHER_HTTP_BUFFER_SIZE) {
        return ESP_FAIL;
    }

    int out_len = (int)json_buffer_size - 1;
    memset(json_buffer, 0, json_buffer_size);
    int zret = gzip_decompress(s_http_compressed_buffer, rx_len, json_buffer, &out_len);
    if (zret != Z_STREAM_END) {
        return ESP_FAIL;
    }

    json_buffer[out_len] = '\0';
    return ESP_OK;
}

static esp_err_t weather_sync_time_once(const char *server)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    esp_netif_sntp_init(&config);

    int retry = 0;
    const int retry_count = 3;
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time from %s... (%d/%d)", server, retry, retry_count);
    }

    esp_netif_sntp_deinit();
    if (retry >= retry_count) {
        return ESP_ERR_TIMEOUT;
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    xSemaphoreTake(s_weather_lock, portMAX_DELAY);
    s_weather_snapshot.time_synced = true;
    xSemaphoreGive(s_weather_lock);

    return ESP_OK;
}

static esp_err_t weather_sync_time(void)
{
    static const char *kNtpServers[] = {
        "pool.ntp.org",
        "ntp.aliyun.com",
        "ntp.tencent.com",
    };

    for (size_t i = 0; i < sizeof(kNtpServers) / sizeof(kNtpServers[0]); ++i) {
        esp_err_t ret = weather_sync_time_once(kNtpServers[i]);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Time sync via %s failed: %s", kNtpServers[i], esp_err_to_name(ret));
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t weather_fetch_daily(void)
{
    ESP_RETURN_ON_ERROR(weather_http_buffers_init(), TAG, "allocate weather json buffer failed");
    ESP_RETURN_ON_ERROR(http_get_gzip_json(QWEATHER_DAILY_URL, s_http_json_buffer, WEATHER_HTTP_BUFFER_SIZE), TAG, "fetch daily weather failed");

    cJSON *root = cJSON_Parse(s_http_json_buffer);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *daily1 = cJSON_IsArray(daily) ? cJSON_GetArrayItem(daily, 0) : NULL;
    if (daily1 == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *temp_max = cJSON_GetObjectItem(daily1, "tempMax");
    cJSON *temp_min = cJSON_GetObjectItem(daily1, "tempMin");
    cJSON *sunrise = cJSON_GetObjectItem(daily1, "sunrise");
    cJSON *sunset = cJSON_GetObjectItem(daily1, "sunset");
    if (!cJSON_IsString(temp_max) || !cJSON_IsString(temp_min) || !cJSON_IsString(sunrise) || !cJSON_IsString(sunset)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    xSemaphoreTake(s_weather_lock, portMAX_DELAY);
    s_weather_snapshot.daily_temp_max = atoi(temp_max->valuestring);
    s_weather_snapshot.daily_temp_min = atoi(temp_min->valuestring);
    snprintf(s_weather_snapshot.sunrise, sizeof(s_weather_snapshot.sunrise), "%s", sunrise->valuestring);
    snprintf(s_weather_snapshot.sunset, sizeof(s_weather_snapshot.sunset), "%s", sunset->valuestring);
    s_weather_snapshot.daily_ready = true;
    xSemaphoreGive(s_weather_lock);

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t weather_fetch_now(void)
{
    ESP_RETURN_ON_ERROR(weather_http_buffers_init(), TAG, "allocate weather json buffer failed");
    ESP_RETURN_ON_ERROR(http_get_gzip_json(QWEATHER_NOW_URL, s_http_json_buffer, WEATHER_HTTP_BUFFER_SIZE), TAG, "fetch current weather failed");

    cJSON *root = cJSON_Parse(s_http_json_buffer);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (now == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *temp = cJSON_GetObjectItem(now, "temp");
    cJSON *icon = cJSON_GetObjectItem(now, "icon");
    cJSON *humidity = cJSON_GetObjectItem(now, "humidity");
    if (!cJSON_IsString(temp) || !cJSON_IsString(icon) || !cJSON_IsString(humidity)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    xSemaphoreTake(s_weather_lock, portMAX_DELAY);
    s_weather_snapshot.now_temp = atoi(temp->valuestring);
    s_weather_snapshot.now_humi = atoi(humidity->valuestring);
    s_weather_snapshot.now_icon = atoi(icon->valuestring);
    s_weather_snapshot.weather_ready = true;
    xSemaphoreGive(s_weather_lock);

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t weather_fetch_air(void)
{
    ESP_RETURN_ON_ERROR(weather_http_buffers_init(), TAG, "allocate weather json buffer failed");
    ESP_RETURN_ON_ERROR(http_get_gzip_json(QAIR_NOW_URL, s_http_json_buffer, WEATHER_HTTP_BUFFER_SIZE), TAG, "fetch air quality failed");

    cJSON *root = cJSON_Parse(s_http_json_buffer);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    cJSON *level = now ? cJSON_GetObjectItem(now, "level") : NULL;
    if (!cJSON_IsString(level)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    xSemaphoreTake(s_weather_lock, portMAX_DELAY);
    s_weather_snapshot.air_level = atoi(level->valuestring);
    s_weather_snapshot.air_ready = true;
    xSemaphoreGive(s_weather_lock);

    cJSON_Delete(root);
    return ESP_OK;
}

static void wifi_connect_watchdog_cb(void *arg)
{
    // example_connect 内部用 portMAX_DELAY 等 IP 信号量，
    // 路由器异常（不回 DHCP 等）时会永远阻塞；这里强制断开，
    // 让它走失败重试路径，避免后台任务从此卡死。
    ESP_LOGW(TAG, "example_connect stuck, forcing wifi disconnect");
    esp_wifi_disconnect();
}

static esp_err_t weather_refresh_once(bool refresh_daily)
{
    bool connected = false;
    bool time_sync_failed = false;
    ESP_LOGI(TAG, "weather refresh begin: heap free=%u min=%u",
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    esp_timer_start_once(s_wifi_connect_watchdog, 25 * 1000 * 1000);
    esp_err_t ret = example_connect();
    esp_timer_stop(s_wifi_connect_watchdog); // 定时器可能已触发，忽略返回值
    if (ret != ESP_OK) {
        example_disconnect();
        weather_set_status("正在连接WiFi");
        return ret;
    }
    connected = true;

    // 等待 SNTP 同步（SNTP 在 weather_service_init 已初始化一次，WiFi连上后自动同步）
    for (int i = 0; i < 10 && time(NULL) < 1700000000; i++) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (time(NULL) > 1700000000) {
        setenv("TZ", "CST-8", 1);
        tzset();
        xSemaphoreTake(s_weather_lock, portMAX_DELAY);
        s_weather_snapshot.time_synced = true;
        xSemaphoreGive(s_weather_lock);
        ESP_LOGI(TAG, "Time synced: %lld", (long long)time(NULL));
    } else {
        ESP_LOGW(TAG, "Time sync failed");
    }

    if (refresh_daily) {
        weather_set_status("正在获取天气信息");
        ret = weather_fetch_daily();
        if (ret != ESP_OK) {
            weather_set_status("正在获取天气信息");
            goto cleanup;
        }
    }

    weather_set_status("正在获取天气信息");
    ret = weather_fetch_now();
    if (ret != ESP_OK) {
        weather_set_status("正在获取天气信息");
        goto cleanup;
    }

    ret = weather_fetch_air();
    if (ret != ESP_OK) {
        weather_set_status("正在获取天气信息");
        goto cleanup;
    }

    if (time_sync_failed) {
        weather_set_status("获取天气信息成功");
    } else {
        weather_set_status("天气信息获取成功");
    }

cleanup:
    if (connected) {
        example_disconnect();
    }
    return ret;
}

static void weather_background_task(void *args)
{
    bool refresh_daily = true;
    (void)args;

    while (1) {
        esp_err_t ret = weather_refresh_once(refresh_daily);
        if (ret == ESP_OK) {
            refresh_daily = !refresh_daily;
            vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_MS));
        }
    }
}

void weather_service_init(void)
{
    if (s_weather_started) {
        return;
    }

    s_weather_lock = xSemaphoreCreateMutex();
    if (s_weather_lock == NULL) {
        ESP_LOGE(TAG, "Failed to create weather lock");
        return;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);

    const esp_timer_create_args_t watchdog_args = {
        .callback = &wifi_connect_watchdog_cb,
        .name = "wifi_wdt",
    };
    ESP_ERROR_CHECK(esp_timer_create(&watchdog_args, &s_wifi_connect_watchdog));

    s_weather_started = true;
    // 优先级与 LVGL 任务相同（1）：联网/解压是后台工作，
    // 高于界面优先级时会把 80MHz 的界面卡住好几秒，看起来像死机
    xTaskCreate(weather_background_task, "weather_bg", 10240, NULL, 1, NULL);
}

void weather_service_get_snapshot(weather_snapshot_t *snapshot)
{
    // UI 定时器里调用：只允许有限等待，拿不到锁时用上一次的数据，
    // 保证后台任务再怎么异常也不会把界面卡死
    static weather_snapshot_t s_ui_cache = {
        .status = "正在获取天气信息",
    };

    if (snapshot == NULL) {
        return;
    }

    if (s_weather_lock != NULL && xSemaphoreTake(s_weather_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_ui_cache = s_weather_snapshot;
        xSemaphoreGive(s_weather_lock);
    }

    *snapshot = s_ui_cache;
}
