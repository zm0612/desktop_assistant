//
// Created by Zhang Zhimeng on 2026/8/22.
//

#ifndef DESKTOP_WEATHER_ASSISTANT_BATTERY_MONITOR_H
#define DESKTOP_WEATHER_ASSISTANT_BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
public:
    // 构造函数：指定分压上端电阻 R1（连接电池正极）和下端电阻 R2（连接 GND）
    // 默认值：100kΩ 上端，27kΩ 下端（常见组合）
    explicit BatteryMonitor(float r1 = 100000.0f, float r2 = 27000.0f);

    // 初始化（可做 ADC 配置，目前无需额外操作）
    static void Begin();

    // 读取电池电压（单位：伏特）
    [[nodiscard]] float ReadVoltage() const;

    // 读取 ADC 原始值（0~1023）
    static int ReadRaw();

    // 校准：修正参考电压或分压电阻误差
    // factor 为乘法系数，offset 为加法偏移（单位：伏特）
    void SetCalibration(float factor = 1.0f, float offset = 0.0f);

    // 获取当前分压比（用于调试）
    [[nodiscard]] float GetDividerRatio() const;

private:
    float r1_; // 上端电阻（Ω）
    float r2_; // 下端电阻（Ω）
    float divider_ratio_; // 分压比倒数 = (R1+R2)/R2
    float calib_factor_; // 校准系数
    float calib_offset_; // 校准偏移（V）
    float v_ref_; // ADC 参考电压，默认 1.0V
};

#endif //DESKTOP_WEATHER_ASSISTANT_BATTERY_MONITOR_H
