#pragma once

#include <stdbool.h>

typedef struct {
    bool glm_ready;                 // 至少成功获取过一次 GLM 数据
    int glm_tokens_percent;         // GLM 5 小时窗口用量（0~100，取最早重置的 TOKENS_LIMIT）
    int glm_weekly_percent;         // GLM 周窗口用量（0~100），-1 表示未返回
    int glm_5h_reset_min;           // GLM 5 小时窗口距重置分钟数，-1 表示未知
    int glm_weekly_reset_min;       // GLM 周窗口距重置分钟数，-1 表示未知
    int glm_time_percent;           // GLM TIME_LIMIT：月度 MCP 用量（0~100），-1 表示未返回
    bool codex_ready;               // 至少成功获取过一次 Codex 桥接数据
    int codex_primary_percent;      // Codex 主窗口（通常为 5 小时，0~100）
    int codex_secondary_percent;    // Codex 次窗口（通常为周窗口，0~100），-1 表示未返回
    int codex_primary_reset_min;    // 主窗口距重置分钟数，-1 表示未知
    int codex_secondary_reset_min;  // 次窗口距重置分钟数，-1 表示未知
    int updated_hour;                // 最近一次成功获取的时间（用于界面显示）
    int updated_min;
} usage_snapshot_t;

// 创建内部锁，在 weather_service_init 里调用
void usage_service_init(void);

// 在已联网（WiFi 已连接）的情况下查询一次额度。GLM 和本机 Codex
// 桥接共用天气服务的 WiFi 周期，不额外连接/断开 WiFi。
void usage_service_fetch(void);

// UI 获取快照（限时等待，不会阻塞界面）
void usage_service_get_snapshot(usage_snapshot_t *snapshot);
