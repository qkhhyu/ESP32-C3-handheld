#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void chip_power_init(void);
void chip_power_enter_low_power(void);
void chip_power_exit_low_power(void);
int  chip_power_get_sample_interval_ms(void);

#ifdef __cplusplus
}
#endif
