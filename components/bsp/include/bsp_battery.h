// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// 芯片带标准 Li-Poly profile，可直接给出基础 SOC%；产品精度仍需按电芯标定。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 剩余电量百分比 0..100;读失败返回 -1。
int bsp_battery_soc(void);

// SOC 寄存器原始 16bit 值；读失败返回 -1。用于区分算法未就绪与总线错误。
int bsp_battery_soc_raw(void);

// 可用于界面显示的 0..100 电量。优先使用芯片 SOC；SOC 无效时按实时
// 电压曲线给出近似值；电压也无效时返回 -1。
int bsp_battery_level(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);

// CONFIG 原始寄存器值；读失败返回 -1。用于真机诊断睡眠/正常状态。
int bsp_battery_config(void);
