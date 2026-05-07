#pragma once

#include "esp_lcd_types.h"
#include <stdbool.h>

void power_mgmt_init(esp_lcd_panel_handle_t panel);
void power_mgmt_set_enabled(bool enabled);
bool power_mgmt_is_enabled(void);
void power_mgmt_notify_touch(void);
void power_mgmt_set_brightness(float duty);
