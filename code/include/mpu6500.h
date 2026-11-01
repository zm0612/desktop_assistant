//
// Created by Zhang Zhimeng on 2026/8/9.
//
#pragma once

#include <Arduino.h>
#include <Wire.h>

// ============================================================
//  MPU6500 寄存器地址
// ============================================================
#define MPU6500_ADDR_DEFAULT    0x68
#define MPU6500_ADDR_ALT        0x69

#define MPU6500_WHO_AM_I        0x75   // 应返回 0x70
#define MPU6500_PWR_MGMT_1      0x6B
#define MPU6500_PWR_MGMT_2      0x6C
#define MPU6500_SMPLRT_DIV      0x19
#define MPU6500_CONFIG          0x1A
#define MPU6500_GYRO_CONFIG     0x1B
#define MPU6500_ACCEL_CONFIG    0x1C
#define MPU6500_ACCEL_CONFIG2   0x1D
#define MPU6500_INT_PIN_CFG     0x37
#define MPU6500_INT_ENABLE      0x38
#define MPU6500_INT_STATUS      0x3A
#define MPU6500_ACCEL_XOUT_H    0x3B
#define MPU6500_TEMP_OUT_H      0x41
#define MPU6500_GYRO_XOUT_H     0x43
#define MPU6500_USER_CTRL       0x6A
#define MPU6500_FIFO_EN         0x23
#define MPU6500_FIFO_COUNTH     0x72
#define MPU6500_FIFO_R_W        0x74
#define MPU6500_SIGNAL_PATH_RESET 0x68

// ============================================================
//  量程枚举
// ============================================================
enum class AccelRange : uint8_t {
    AFS_2G = 0x00,
    AFS_4G = 0x01,
    AFS_8G = 0x02,
    AFS_16G = 0x03
};

enum class GyroRange : uint8_t {
    GFS_250DPS = 0x00,
    GFS_500DPS = 0x01,
    GFS_1000DPS = 0x02,
    GFS_2000DPS = 0x03
};

enum class DLPFBandwidth : uint8_t {
    BW_250Hz = 0x00,
    BW_184Hz = 0x01,
    BW_92Hz = 0x02,
    BW_41Hz = 0x03,
    BW_20Hz = 0x04,
    BW_10Hz = 0x05,
    BW_5Hz = 0x06
};

// ============================================================
//  IMU 数据结构体 (标准单位)
// ============================================================
struct IMUData {
    // 加速度 (m/s²)
    float ax_;
    float ay_;
    float az_;

    // 角速度 (rad/s)
    float gx_;
    float gy_;
    float gz_;

    // 温度 (°C)
    float temperature_;

    // 时间戳 (ms)
    uint32_t timestamp_;
};

// ============================================================
//  MPU6500 传感器类
// ============================================================
class MPU6500 {
public:
    /**
     * @brief 构造函数
     * @param addr  I2C 地址 (默认 0x68)
     * @param wire  Wire 实例指针 (默认 &Wire)
     */
    explicit MPU6500(uint8_t addr = MPU6500_ADDR_DEFAULT,
                     TwoWire *wire = &Wire);

    ~MPU6500() = default;

    // ---- 初始化与配置 ----

    /**
     * @brief  初始化传感器
     * @param  accel_range  加速度计量程
     * @param  gyro_range   陀螺仪量程
     * @param  dlpf        数字低通滤波器带宽
     * @param  sample_rate  采样率 (Hz), 实际 = 1kHz / (1 + SMPLRT_DIV)
     * @return true = 初始化成功
     */
    bool Begin(AccelRange accel_range = AccelRange::AFS_2G,
               GyroRange gyro_range = GyroRange::GFS_250DPS,
               DLPFBandwidth dlpf = DLPFBandwidth::BW_41Hz,
               uint16_t sample_rate = 200);

    /**
     * @brief  检查设备是否在线 (读 WHO_AM_I)
     * @return true = 芯片在线
     */
    [[nodiscard]] bool IsConnected() const;

    /**
     * @brief  软件复位传感器
     */
    void Reset() const;

    /**
     * @brief  进入/退出睡眠模式
     */
    void Sleep(bool enable) const;

    // ---- 数据读取 ----

    /**
     * @brief  是否有新数据就绪 (检查中断标志)
     */
    [[nodiscard]] bool DataReady() const;

    /**
     * @brief  读取全部传感器数据并解析为标准单位
     * @param  data  输出参数
     * @return true = 读取成功
     */
    bool Read(IMUData &data) const;

    /**
     * @brief  仅读取原始寄存器值 (14 字节)
     * @param  raw  输出缓冲区 (至少 14 字节)
     * @return true = 读取成功
     */
    bool ReadRaw(uint8_t *raw) const;

    // ---- 校准 ----

    /**
     * @brief  自动校准 (传感器需水平静置)
     * @param  samples  采样次数
     */
    void Calibrate(uint16_t samples = 2000);

    // ---- 配置读写 ----

    void SetAccelRange(AccelRange range);

    void SetGyroRange(GyroRange range);

    void SetDLPF(DLPFBandwidth bw) const;

    void SetSampleRate(uint16_t rate_hz) const;

    [[nodiscard]] AccelRange GetAccelRange() const;

    [[nodiscard]] GyroRange GetGyroRange() const;

    /**
     * @brief  获取当前加速度分辨率 (LSB/g)
     */
    [[nodiscard]] float GetAccelScale() const;

    /**
     * @brief  获取当前陀螺仪分辨率 (LSB/(°/s))
     */
    [[nodiscard]] float GetGyroScale() const;

private:
    uint8_t addr_{};
    TwoWire *wire_{};

    AccelRange accel_range_{};
    GyroRange gyro_range_{};

    // 校准偏移量 (原始 LSB)
    float accel_bias_[3] = {0, 0, 0};
    float gyro_bias_[3] = {0, 0, 0};

    // ---- I2C 底层操作 ----
    void WriteRegister(uint8_t reg, uint8_t value) const;

    [[nodiscard]] uint8_t ReadRegister(uint8_t reg) const;

    void ReadRegisters(uint8_t reg, uint8_t *buf, uint8_t len) const;
};
