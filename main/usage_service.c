#include "usage_service.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usage_secrets.h"

static const char *TAG = "usage_service";

/* One response buffer is deliberately shared: requests are sequential in weather_bg. */
#define USAGE_HTTP_BUFFER_SIZE 2048
#define GLM_QUOTA_URL "https://open.bigmodel.cn/api/monitor/usage/quota/limit"

static SemaphoreHandle_t s_usage_lock;
static char s_usage_response[USAGE_HTTP_BUFFER_SIZE];
static int s_usage_rx_len;
static bool s_glm_placeholder_warned;
static bool s_codex_placeholder_warned;
static usage_snapshot_t s_usage_snapshot = {
    .glm_weekly_percent = -1,
    .glm_5h_reset_min = -1,
    .glm_weekly_reset_min = -1,
    .glm_time_percent = -1,
    .codex_secondary_percent = -1,
    .codex_primary_reset_min = -1,
    .codex_secondary_reset_min = -1,
};

static void usage_log_heap(const char *stage)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "heap %-13s free=%u min=%u largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_minimum_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
}

static esp_err_t usage_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data != NULL) {
        int remaining = USAGE_HTTP_BUFFER_SIZE - s_usage_rx_len - 1;
        int copy_len = evt->data_len < remaining ? evt->data_len : remaining;
        if (copy_len > 0) {
            memcpy((char *)evt->user_data + s_usage_rx_len, evt->data, copy_len);
            s_usage_rx_len += copy_len;
        }
    }
    return ESP_OK;
}

static int usage_percent_to_int(const cJSON *item)
{
    if (!cJSON_IsNumber(item)) {
        return -1;
    }
    int percent = (int)(item->valuedouble + 0.5);
    if (percent < 0) {
        return 0;
    }
    return percent > 100 ? 100 : percent;
}

static int usage_seconds_to_minutes(const cJSON *item)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
        return -1;
    }
    return (int)((item->valuedouble + 59.0) / 60.0);
}

static esp_err_t usage_http_get(const char *url, const char *header_name, const char *header_value)
{
    memset(s_usage_response, 0, sizeof(s_usage_response));
    s_usage_rx_len = 0;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = usage_http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_data = s_usage_response,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    if (header_name != NULL && header_value != NULL && header_value[0] != '\0') {
        esp_http_client_set_header(client, header_name, header_value);
    }
    /* Neither endpoint needs compressed HTTP. This avoids an extra decode buffer. */
    esp_http_client_set_header(client, "Accept-Encoding", "identity");

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    int rx_len = s_usage_rx_len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return err;
    }
    if (status_code != 200 || rx_len <= 0) {
        ESP_LOGW(TAG, "quota request failed: status=%d len=%d", status_code, rx_len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void usage_set_updated_time(usage_snapshot_t *snapshot)
{
    /* Usage pages always use China Standard Time, independent of libc TZ state. */
    time_t now = time(NULL) + 8 * 60 * 60;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    snapshot->updated_hour = timeinfo.tm_hour;
    snapshot->updated_min = timeinfo.tm_min;
}

static void usage_commit_glm(int five_percent, int week_percent, int mcp_percent,
                             int five_reset_min, int week_reset_min)
{
    xSemaphoreTake(s_usage_lock, portMAX_DELAY);
    s_usage_snapshot.glm_ready = true;
    s_usage_snapshot.glm_tokens_percent = five_percent;
    s_usage_snapshot.glm_weekly_percent = week_percent;
    s_usage_snapshot.glm_5h_reset_min = five_reset_min;
    s_usage_snapshot.glm_weekly_reset_min = week_reset_min;
    s_usage_snapshot.glm_time_percent = mcp_percent;
    usage_set_updated_time(&s_usage_snapshot);
    xSemaphoreGive(s_usage_lock);
}

static void usage_commit_codex(int primary_percent, int secondary_percent,
                               int primary_reset_min, int secondary_reset_min)
{
    xSemaphoreTake(s_usage_lock, portMAX_DELAY);
    s_usage_snapshot.codex_ready = true;
    s_usage_snapshot.codex_primary_percent = primary_percent;
    s_usage_snapshot.codex_secondary_percent = secondary_percent;
    s_usage_snapshot.codex_primary_reset_min = primary_reset_min;
    s_usage_snapshot.codex_secondary_reset_min = secondary_reset_min;
    usage_set_updated_time(&s_usage_snapshot);
    xSemaphoreGive(s_usage_lock);
}

static esp_err_t usage_query_glm_quota(void)
{
    esp_err_t ret = usage_http_get(GLM_QUOTA_URL, "Authorization", GLM_API_KEY);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON *root = cJSON_Parse(s_usage_response);
    cJSON *data = root ? cJSON_GetObjectItem(root, "data") : NULL;
    cJSON *limits = data ? cJSON_GetObjectItem(data, "limits") : NULL;
    if (!cJSON_IsArray(limits)) {
        ESP_LOGW(TAG, "unexpected GLM quota response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* TOKENS_LIMIT 会同时返回5小时窗口和周窗口（实测 pro 套餐各一条），
       用 nextResetTime（epoch 毫秒）区分：最早重置的是 5H，较晚的是 WK。
       TIME_LIMIT 是月度 MCP 用量。 */
    int64_t earliest_ms = INT64_MAX;
    int64_t latest_ms = -1;
    int five_percent = -1;
    int week_percent = -1;
    int mcp_percent = -1;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, limits) {
        const cJSON *type = cJSON_GetObjectItem(item, "type");
        int value = usage_percent_to_int(cJSON_GetObjectItem(item, "percentage"));
        if (!cJSON_IsString(type) || value < 0) {
            continue;
        }

        if (strcmp(type->valuestring, "TOKENS_LIMIT") == 0) {
            const cJSON *reset = cJSON_GetObjectItem(item, "nextResetTime");
            int64_t reset_ms = cJSON_IsNumber(reset) ? (int64_t)reset->valuedouble : INT64_MAX;
            if (reset_ms < earliest_ms) {
                /* 新出现的更早窗口：原来的 5H 其实是周窗口 */
                if (earliest_ms != INT64_MAX) {
                    week_percent = five_percent;
                    latest_ms = earliest_ms;
                }
                earliest_ms = reset_ms;
                five_percent = value;
            } else if (reset_ms > latest_ms) {
                latest_ms = reset_ms;
                week_percent = value;
            }
        } else if (strcmp(type->valuestring, "TIME_LIMIT") == 0) {
            mcp_percent = value;
        }
    }
    cJSON_Delete(root);

    int five_reset_min = -1;
    int week_reset_min = -1;
    if (earliest_ms != INT64_MAX && time(NULL) > 1700000000) {
        int64_t horizon_ms = earliest_ms - (int64_t)time(NULL) * 1000;
        if (horizon_ms > 0) {
            five_reset_min = (int)(horizon_ms / 60000);
        }
    }
    if (latest_ms > earliest_ms && time(NULL) > 1700000000) {
        int64_t horizon_ms = latest_ms - (int64_t)time(NULL) * 1000;
        if (horizon_ms > 0) {
            week_reset_min = (int)(horizon_ms / 60000);
        }
    }

    if (five_percent < 0) {
        return ESP_FAIL;
    }
    usage_commit_glm(five_percent, week_percent, mcp_percent, five_reset_min, week_reset_min);
    ESP_LOGI(TAG, "GLM quota: 5h=%d%% wk=%d%% reset=%d/%dmin mcp=%d%%",
             five_percent, week_percent, five_reset_min, week_reset_min, mcp_percent);
    return ESP_OK;
}

static esp_err_t usage_query_codex_bridge(void)
{
    esp_err_t ret = usage_http_get(CODEX_BRIDGE_URL, "X-Usage-Token", CODEX_BRIDGE_TOKEN);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON *root = cJSON_Parse(s_usage_response);
    cJSON *primary = root ? cJSON_GetObjectItem(root, "primary") : NULL;
    cJSON *secondary = root ? cJSON_GetObjectItem(root, "secondary") : NULL;
    int primary_percent = primary ? usage_percent_to_int(cJSON_GetObjectItem(primary, "used_percent")) : -1;
    int primary_reset = primary ? usage_seconds_to_minutes(cJSON_GetObjectItem(primary, "reset_after_seconds")) : -1;
    int secondary_percent = secondary ? usage_percent_to_int(cJSON_GetObjectItem(secondary, "used_percent")) : -1;
    int secondary_reset = secondary ? usage_seconds_to_minutes(cJSON_GetObjectItem(secondary, "reset_after_seconds")) : -1;
    cJSON_Delete(root);

    if (primary_percent < 0) {
        ESP_LOGW(TAG, "unexpected Codex bridge response");
        return ESP_FAIL;
    }
    usage_commit_codex(primary_percent, secondary_percent, primary_reset, secondary_reset);
    ESP_LOGI(TAG, "Codex quota: primary=%d%% secondary=%d%%", primary_percent, secondary_percent);
    return ESP_OK;
}

void usage_service_init(void)
{
    if (s_usage_lock == NULL) {
        s_usage_lock = xSemaphoreCreateMutex();
    }
    if (s_usage_lock == NULL) {
        ESP_LOGE(TAG, "Failed to create usage lock");
    }
}

void usage_service_fetch(void)
{
    if (s_usage_lock == NULL) {
        return;
    }

    if (strcmp(GLM_API_KEY, "YOUR_GLM_API_KEY") == 0) {
        if (!s_glm_placeholder_warned) {
            ESP_LOGW(TAG, "GLM_API_KEY not configured, skip GLM quota query");
            s_glm_placeholder_warned = true;
        }
    } else {
        if (usage_query_glm_quota() != ESP_OK) {
            ESP_LOGW(TAG, "GLM quota query failed");
        }
        usage_log_heap("after GLM");
    }

    if (CODEX_BRIDGE_URL[0] == '\0') {
        if (!s_codex_placeholder_warned) {
            ESP_LOGI(TAG, "CODEX_BRIDGE_URL not configured, skip Codex quota query");
            s_codex_placeholder_warned = true;
        }
    } else {
        if (usage_query_codex_bridge() != ESP_OK) {
            ESP_LOGW(TAG, "Codex bridge query failed");
        }
        usage_log_heap("after Codex");
    }
}

void usage_service_get_snapshot(usage_snapshot_t *snapshot)
{
    static usage_snapshot_t s_ui_cache = {
        .glm_weekly_percent = -1,
        .glm_5h_reset_min = -1,
        .glm_weekly_reset_min = -1,
        .glm_time_percent = -1,
        .codex_secondary_percent = -1,
        .codex_primary_reset_min = -1,
        .codex_secondary_reset_min = -1,
    };
    if (snapshot == NULL) {
        return;
    }
    /* UI is always bounded: retain the previous snapshot if weather_bg owns the lock. */
    if (s_usage_lock != NULL && xSemaphoreTake(s_usage_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_ui_cache = s_usage_snapshot;
        xSemaphoreGive(s_usage_lock);
    }
    *snapshot = s_ui_cache;
}
