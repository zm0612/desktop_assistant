//
// Created by Zhang Zhimeng on 2026/8/22.
//

#include "battery_monitor.h"

// 构造函数
BatteryMonitor::BatteryMonitor(const float r1, const float r2)
    : r1_(r1), r2_(r2), calib_factor_(1.0f), calib_offset_(0.0f), v_ref_(1.0f) {
    // 计算分压比倒数：(R1+R2)/R2
    divider_ratio_ = (r1_ + r2_) / r2_;
}

// 初始化
void BatteryMonitor::Begin() {
    // ESP8266 的 ADC 引脚无需特殊初始化，直接使用 analogRead(A0)
    // 可在此添加校准预读（略）
}

// 读取原始 ADC 值（0~1023）
int BatteryMonitor::ReadRaw() {
    return analogRead(A0); // ESP8266 的 ADC 引脚是 A0
}

// 读取电池电压（单位：伏特）
float BatteryMonitor::ReadVoltage() const {
    const int raw = ReadRaw();
    // ADC 电压 = (raw / 1023.0) * Vref
    const float adc_voltage = static_cast<float>(raw) / 1023.0f * v_ref_;
    // 电池电压 = ADC电压 * 分压比倒数
    float battery_voltage = adc_voltage * divider_ratio_;
    // 应用校准
    battery_voltage = battery_voltage * calib_factor_ + calib_offset_;
    return battery_voltage;
}

// 校准设置
void BatteryMonitor::SetCalibration(const float factor, const float offset) {
    calib_factor_ = factor;
    calib_offset_ = offset;
}

// 获取分压比（调试用）
float BatteryMonitor::GetDividerRatio() const {
    return divider_ratio_;
}
