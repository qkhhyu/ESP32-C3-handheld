#pragma once

#include <stdbool.h>

typedef struct {
    bool time_synced;
    bool weather_ready;
    bool daily_ready;
    bool air_ready;
    int now_temp;
    int now_humi;
    int now_icon;
    int daily_temp_max;
    int daily_temp_min;
    int air_level;
    char sunrise[10];
    char sunset[10];
    char status[64];
} weather_snapshot_t;

void weather_service_init(void);
void weather_service_get_snapshot(weather_snapshot_t *snapshot);
