//
// Created by Zhang Zhimeng on 2026/8/9.
//

#include "mpu6500.h"

// ============================================================
//  常量
// ============================================================
static constexpr float GRAVITY = 9.80665f; // m/s²
static constexpr float DEG_TO_RAD_F = PI / 180.0f;
static constexpr uint8_t WHO_AM_I_ID = 0x70; // MPU6500

// 加速度灵敏度表 (LSB/g)
static constexpr float ACCEL_SCALE_TABLE[] = {
    16384.0f, // ±2g
    8192.0f, // ±4g
    4096.0f, // ±8g
    2048.0f // ±16g
};

// 陀螺仪灵敏度表 (LSB/(°/s))
static constexpr float GYRO_SCALE_TABLE[] = {
    131.0f, // ±250°/s
    65.5f, // ±500°/s
    32.8f, // ±1000°/s
    16.4f // ±2000°/s
};

// ============================================================
//  构造
// ============================================================
MPU6500::MPU6500(const uint8_t addr, TwoWire *wire)
    : addr_(addr), wire_(wire) {
}

// ============================================================
//  初始化
// ============================================================
bool MPU6500::Begin(const AccelRange accel_range, const GyroRange gyro_range,
                    const DLPFBandwidth dlpf, const uint16_t sample_rate) {
    wire_->begin();
    wire_->setClock(400000); // 400 kHz I2C

    // 等待 I2C 总线稳定
    delay(200);

    // ★ WHO_AM_I 带重试（解决首次读取 0x2E 问题）
    uint8_t id = 0;
    for (int retry = 0; retry < 5; retry++) {
        id = ReadRegister(MPU6500_WHO_AM_I);
        if (id == WHO_AM_I_ID) {
            break;
        }
        Serial.printf("[MPU6500] WHO_AM_I 重试 %d/5: 0x%02X\n", retry + 1, id);
        delay(50);
    }

    if (id != WHO_AM_I_ID) {
        Serial.printf("[MPU6500] WHO_AM_I 失败! 期望 0x%02X, 实际 0x%02X\n",
                      WHO_AM_I_ID, id);
        return false;
    }

    // 复位
    Reset();
    delay(100);

    // 唤醒 (退出睡眠), 时钟源 = PLL with X-axis gyro (最佳精度)
    WriteRegister(MPU6500_PWR_MGMT_1, 0x01);
    delay(10);

    // 启用所有轴
    WriteRegister(MPU6500_PWR_MGMT_2, 0x00);

    // 配置 DLPF
    SetDLPF(dlpf);

    // 配置采样率
    SetSampleRate(sample_rate);

    // 配置量程
    SetAccelRange(accel_range);
    SetGyroRange(gyro_range);

    // 中断配置: 数据就绪中断, 读清除, 低电平有效
    WriteRegister(MPU6500_INT_PIN_CFG, 0x30);
    WriteRegister(MPU6500_INT_ENABLE, 0x01); // 启用数据就绪中断

    Serial.printf("[MPU6500] 初始化成功 | Accel: ±%dg | Gyro: ±%d°/s\n",
                  (int[]){2, 4, 8, 16}[static_cast<uint8_t>(accel_range_)],
                  (int[]){250, 500, 1000, 2000}[static_cast<uint8_t>(gyro_range_)]);

    return true;
}

// ============================================================
//  连接检测
// ============================================================
bool MPU6500::IsConnected() const {
    return ReadRegister(MPU6500_WHO_AM_I) == WHO_AM_I_ID;
}

// ============================================================
//  复位
// ============================================================
void MPU6500::Reset() const {
    WriteRegister(MPU6500_PWR_MGMT_1, 0x80); // bit7 = H_RESET
    delay(100); // 等待复位完成
}

// ============================================================
//  睡眠
// ============================================================
void MPU6500::Sleep(const bool enable) const {
    uint8_t val = ReadRegister(MPU6500_PWR_MGMT_1);
    if (enable) {
        val |= 0x40; // bit6 = SLEEP
    } else {
        val &= ~0x40;
    }
    WriteRegister(MPU6500_PWR_MGMT_1, val);
}

// ============================================================
//  数据就绪
// ============================================================
bool MPU6500::DataReady() const {
    return (ReadRegister(MPU6500_INT_STATUS) & 0x01);
}

// ============================================================
//  读取并解析数据
// ============================================================
bool MPU6500::Read(IMUData &data) const {
    uint8_t raw[14];
    if (!ReadRaw(raw)) return false;

    // 解析原始数据 (大端序: High byte first)
    const auto raw_ax = static_cast<int16_t>((raw[0] << 8) | raw[1]);
    const auto raw_ay = static_cast<int16_t>((raw[2] << 8) | raw[3]);
    const auto raw_az = static_cast<int16_t>((raw[4] << 8) | raw[5]);
    const auto raw_temp = static_cast<int16_t>((raw[6] << 8) | raw[7]);
    const auto raw_gx = static_cast<int16_t>((raw[8] << 8) | raw[9]);
    const auto raw_gy = static_cast<int16_t>((raw[10] << 8) | raw[11]);
    const auto raw_gz = static_cast<int16_t>((raw[12] << 8) | raw[13]);

    // 减去校准偏移
    const float fax = static_cast<float>(raw_ax) - accel_bias_[0];
    const float fay = static_cast<float>(raw_ay) - accel_bias_[1];
    const float faz = static_cast<float>(raw_az) - accel_bias_[2];
    const float fgx = static_cast<float>(raw_gx) - gyro_bias_[0];
    const float fgy = static_cast<float>(raw_gy) - gyro_bias_[1];
    const float fgz = static_cast<float>(raw_gz) - gyro_bias_[2];

    // 获取比例因子
    const float a_scale = GetAccelScale(); // LSB/g
    const float g_scale = GetGyroScale(); // LSB/(°/s)

    // ---- 加速度: raw → g → m/s² ----
    data.ax_ = (fax / a_scale) * GRAVITY;
    data.ay_ = (fay / a_scale) * GRAVITY;
    data.az_ = (faz / a_scale) * GRAVITY;

    // ---- 角速度: raw → °/s → rad/s ----
    data.gx_ = (fgx / g_scale) * DEG_TO_RAD_F;
    data.gy_ = (fgy / g_scale) * DEG_TO_RAD_F;
    data.gz_ = (fgz / g_scale) * DEG_TO_RAD_F;

    // ---- 温度: raw → °C ----
    // MPU6500: Temp = (raw - 21) / 333.87 + 21
    data.temperature_ = (static_cast<float>(raw_temp) - 21.0f) / 333.87f + 21.0f;

    // 时间戳
    data.timestamp_ = millis();

    return true;
}

// ============================================================
//  读取原始寄存器
// ============================================================
bool MPU6500::ReadRaw(uint8_t *raw) const {
    ReadRegisters(MPU6500_ACCEL_XOUT_H, raw, 14);
    return true;
}

// ============================================================
//  校准
// ============================================================
void MPU6500::Calibrate(const uint16_t samples) {
    Serial.println("[MPU6500] 开始校准, 请保持传感器水平静止...");

    float accSum[3] = {0, 0, 0};
    float gyrSum[3] = {0, 0, 0};

    for (uint16_t i = 0; i < samples; i++) {
        uint8_t raw[14];
        ReadRaw(raw);

        accSum[0] += static_cast<float>((raw[0] << 8) | raw[1]);
        accSum[1] += static_cast<float>((raw[2] << 8) | raw[3]);
        accSum[2] += static_cast<float>((raw[4] << 8) | raw[5]);
        gyrSum[0] += static_cast<float>((raw[8] << 8) | raw[9]);
        gyrSum[1] += static_cast<float>((raw[10] << 8) | raw[11]);
        gyrSum[2] += static_cast<float>((raw[12] << 8) | raw[13]);

        delay(2); // ~500Hz
    }

    // 求平均
    for (int i = 0; i < 3; i++) {
        accel_bias_[i] = accSum[i] / static_cast<float>(samples);
        gyro_bias_[i] = gyrSum[i] / static_cast<float>(samples);
    }

    // Z轴加速度在水平时应为 +1g
    // 保留 1g 分量: bias_z = bias_z - 1g_in_LSB
    const float oneG_lsb = GetAccelScale(); // 1g 对应的 LSB 值
    accel_bias_[2] -= oneG_lsb;

    Serial.printf("[MPU6500] 校准完成 | AccelBias: [%.1f, %.1f, %.1f] | "
                  "GyroBias: [%.1f, %.1f, %.1f]\n",
                  accel_bias_[0], accel_bias_[1], accel_bias_[2],
                  gyro_bias_[0], gyro_bias_[1], gyro_bias_[2]);
}

// ============================================================
//  量程 / 滤波器配置
// ============================================================
void MPU6500::SetAccelRange(AccelRange range) {
    accel_range_ = range;
    WriteRegister(MPU6500_ACCEL_CONFIG, static_cast<uint8_t>(range) << 3);
}

void MPU6500::SetGyroRange(GyroRange range) {
    gyro_range_ = range;
    WriteRegister(MPU6500_GYRO_CONFIG, static_cast<uint8_t>(range) << 3);
}

void MPU6500::SetDLPF(DLPFBandwidth bw) const {
    WriteRegister(MPU6500_CONFIG, static_cast<uint8_t>(bw));
}

void MPU6500::SetSampleRate(const uint16_t rate_hz) const {
    // 内部陀螺仪采样率 = 1kHz (当 DLPF 启用时)
    // SMPLRT_DIV = (1000 / rateHz) - 1
    const auto div = static_cast<uint8_t>(1000 / rate_hz - 1);
    WriteRegister(MPU6500_SMPLRT_DIV, div);
}

AccelRange MPU6500::GetAccelRange() const {
    return accel_range_;
}

GyroRange MPU6500::GetGyroRange() const {
    return gyro_range_;
}

float MPU6500::GetAccelScale() const {
    return ACCEL_SCALE_TABLE[static_cast<uint8_t>(accel_range_)];
}

float MPU6500::GetGyroScale() const {
    return GYRO_SCALE_TABLE[static_cast<uint8_t>(gyro_range_)];
}

// ============================================================
//  I2C 底层
// ============================================================
void MPU6500::WriteRegister(const uint8_t reg, const uint8_t value) const {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(value);
    wire_->endTransmission();
}

uint8_t MPU6500::ReadRegister(const uint8_t reg) const {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->endTransmission(false);

    wire_->requestFrom(addr_, static_cast<uint8_t>(1));
    return wire_->read();
}

void MPU6500::ReadRegisters(const uint8_t reg, uint8_t *buf, const uint8_t len) const {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->endTransmission(false);

    wire_->requestFrom(addr_, len);
    for (uint8_t i = 0; i < len && wire_->available(); i++) {
        buf[i] = wire_->read();
    }
}
