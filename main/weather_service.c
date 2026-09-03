#include "weather_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usage_service.h"
#include "weather_secrets.h"
#include "zlib.h"

static const char *TAG = "weather_service";

#define WEATHER_HTTP_BUFFER_SIZE 4096
#define WEATHER_RETRY_MS         (60 * 1000)
#define WEATHER_WIFI_TIMEOUT_MS  (25 * 1000)
#define WEATHER_WIFI_MAX_RETRY   6
/* Weather and AI usage intentionally share this Wi-Fi/TLS cycle. */
#define WEATHER_REFRESH_MS       (10 * 60 * 1000)

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

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
static esp_netif_t *s_wifi_netif;
static EventGroupHandle_t s_wifi_event_group;
static volatile bool s_wifi_cycle_active;
static int s_wifi_retry_count;

static void weather_log_heap(const char *stage)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "heap %-13s free=%u min=%u largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_minimum_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
}

static void weather_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_wifi_cycle_active) {
            return;
        }

        if (++s_wifi_retry_count <= WEATHER_WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d",
                     s_wifi_retry_count, WEATHER_WIFI_MAX_RETRY);
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "esp_wifi_connect retry failed: %s", esp_err_to_name(ret));
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
            }
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && s_wifi_cycle_active) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t weather_wifi_init_once(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Keep the driver, netif and handlers for the device lifetime. Recreating
       this entire graph every refresh leaked a small amount in IDF internals. */
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                    weather_wifi_event_handler, NULL),
                        TAG, "register WiFi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    weather_wifi_event_handler, NULL),
                        TAG, "register IP handler failed");

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set WiFi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set WiFi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set WiFi config failed");
    return ESP_OK;
}

static void weather_wifi_stop(void);

static esp_err_t weather_wifi_connect(void)
{
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    s_wifi_retry_count = 0;
    s_wifi_cycle_active = true;

    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        s_wifi_cycle_active = false;
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    weather_log_heap("wifi started");

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        weather_wifi_stop();
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WEATHER_WIFI_TIMEOUT_MS));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        weather_log_heap("IP acquired");
        return ESP_OK;
    }

    weather_wifi_stop();
    if ((bits & WIFI_FAILED_BIT) != 0) {
        ESP_LOGE(TAG, "WiFi connection failed after %d retries", s_wifi_retry_count);
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "WiFi connection timed out after %d ms", WEATHER_WIFI_TIMEOUT_MS);
    return ESP_ERR_TIMEOUT;
}

static void weather_wifi_stop(void)
{
    s_wifi_cycle_active = false;

    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(ret));
    }
    ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
    }

    /* Let pending disconnect events run before recording the cycle baseline. */
    vTaskDelay(pdMS_TO_TICKS(100));
    weather_log_heap("wifi stopped");
}

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

static esp_err_t weather_refresh_once(bool refresh_daily)
{
    bool connected = false;
    bool time_sync_failed = false;
    weather_log_heap("cycle begin");

    esp_err_t ret = weather_wifi_connect();
    if (ret != ESP_OK) {
        weather_set_status("正在连接WiFi");
        return ret;
    }
    connected = true;

    // SNTP 随 WiFi 会话重建：开机即启动的 SNTP 在 WiFi 未连接时请求失败，
    // LWIP 按指数退避重试（最长3分钟一次），导致拿到 IP 后时间迟迟不同步。
    // 在联网窗口内重建服务，保证立刻开始全新请求（周期性重校时也顺便消除漂移）。
    esp_netif_sntp_deinit(); // 上一周期可能还在运行，未初始化时返回错误可忽略
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    esp_netif_sntp_init(&sntp_cfg);

    // 等待 SNTP 同步
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
        weather_log_heap("after daily");
    }

    weather_set_status("正在获取天气信息");
    ret = weather_fetch_now();
    if (ret != ESP_OK) {
        weather_set_status("正在获取天气信息");
        goto cleanup;
    }
    weather_log_heap("after current");

    ret = weather_fetch_air();
    if (ret != ESP_OK) {
        weather_set_status("正在获取天气信息");
        goto cleanup;
    }
    weather_log_heap("after air");

    // 顺带查询 GLM 额度（共用本次 WiFi 连接，失败不影响天气刷新）
    usage_service_fetch();
    weather_log_heap("after usage");

    if (time_sync_failed) {
        weather_set_status("获取天气信息成功");
    } else {
        weather_set_status("天气信息获取成功");
    }

cleanup:
    if (connected) {
        weather_wifi_stop();
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

    ret = weather_wifi_init_once();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi lifecycle: %s", esp_err_to_name(ret));
        return;
    }

    // SNTP 不在这里初始化：随每个联网周期重建（见 weather_refresh_once），
    // 避免 WiFi 未连接时的失败请求触发 LWIP 指数退避

    usage_service_init();

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
        // 时间可能在两次联网周期之间同步成功（SNTP 后台重试），即时点亮界面时钟
        if (!s_weather_snapshot.time_synced && time(NULL) > 1700000000) {
            s_weather_snapshot.time_synced = true;
        }
        s_ui_cache = s_weather_snapshot;
        xSemaphoreGive(s_weather_lock);
    }

    *snapshot = s_ui_cache;
}
