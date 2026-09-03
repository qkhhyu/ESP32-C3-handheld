#pragma once

#include "lvgl.h"

void weather_app_open(lv_obj_t *root);
void weather_app_close(void);

// 返回 true 表示天气应用已消费左右滑动；上滑仍由启动器统一处理退出。
bool weather_app_handle_gesture(lv_dir_t dir);
