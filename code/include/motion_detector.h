//
// Created by Zhang Zhimeng on 2026/9/5.
//

//
// 动作检测引擎：吃 MPU6500 的 IMUData，输出「左右甩动 / 双击 / 摇一摇」三类手势，
// 以及静止时的重力朝向（离散 6 面 + 连续 pitch/roll）。
//
// 设计要点（鲁棒性）：
//  - 用低通(EMA)估计重力，再从加速度里减掉 → 得到「线性加速度」；所有手势都在
//    线性加速度上判定，天然不受静态倾斜干扰。
//  - dt 由帧间时间戳自适应，对主循环抖动(HTTP/重绘卡顿)不敏感。
//  - 左右方向不写死轴：上电用重力定「竖直轴」，甩动时取水平面内运动最大的轴，
//    按其符号判左/右；左右反了用 setSwipeInvert() 翻。
//  - 三类手势共用「优先级 + 不应期(refractory)」，一个物理动作只吐一种事件、不重复。
//
// 用法：每个 loop 先 mpu.Read(d)，再 detector.update(d)。事件走回调或轮询（二选一）。
//
#pragma once
#include "mpu6500.h"   // IMUData

#include <Arduino.h>

// 手势类型
enum class GestureType : std::uint8_t {
    None = 0,
    SwipeLeft, // 向左甩
    SwipeRight, // 向右甩
    DoubleTap, // 双击（best-effort）
    Shake, // 摇一摇
};

// 离散 6 面朝向：重力主要指向板子的哪个轴向。
// 命名按 board frame（IMU 自身坐标），「朝上/朝左」的物理含义以上机串口确认为准。
enum class FaceOrientation : std::uint8_t {
    Unknown = 0,
    FaceUp, // 重力 -Z
    FaceDown, // 重力 +Z
    Left, // 重力 -X
    Right, // 重力 +X
    Top, // 重力 -Y
    Bottom, // 重力 +Y
};

// 静止朝向状态（随时可读）
struct OrientationState {
    FaceOrientation face = FaceOrientation::Unknown; // 离散 6 面（带迟滞，不抖）
    float pitch_deg = 0.0f; // 连续俯仰角
    float roll_deg = 0.0f; // 连续横滚角
    float gx = 0.0f, gy = 0.0f, gz = 0.0f; // 归一化重力向量(单位 g, board frame)
};

// 一次手势事件
struct GestureEvent {
    GestureType type = GestureType::None;
    int8_t axis = -1; // 命中的轴：0=X 1=Y 2=Z（调试/校准用；tap/shake 可能为 -1）
    float strength = 0.0f; // 峰值线性加速度 (m/s²)，供调参参考
    std::uint32_t t = 0; // 事件时间戳 (ms)
};

class MotionDetector {
public:
    using GestureCallback = void (*)(const GestureEvent &);
    using OrientationCallback = void (*)(const OrientationState &);

    MotionDetector();

    // 每个 loop 喂一帧（先 mpu.Read(d) 再 update(d)）。内部自适应 dt。
    void Update(const IMUData &d);

    // ---- 事件输出：回调（在 update 内即时触发）或轮询，二选一 ----
    void OnGesture(const GestureCallback cb) {
        gesture_cb_ = cb;
    }

    void OnOrientationChange(const OrientationCallback cb) {
        orient_cb_ = cb;
    }

    // 轮询式：取出一个待处理手势，返回 false 表示当前无。（未注册回调时用）
    bool PollGesture(GestureEvent &out);

    // ---- 朝向查询（随时可读）----
    [[nodiscard]] const OrientationState &Orientation() const {
        return orient_;
    }

    // ---- 校准 / 调参（都有默认值，供上机微调鲁棒性）----
    void SetSerialDebug(const bool on) {
        debug_ = on;
    } // 串口打印峰值/事件/朝向

    void SetSwipeInvert(const bool inv) {
        swipe_invert_ = inv;
    } // 左右反了翻这个

    void SetSwipeThresholds(float enter, float peak, std::uint16_t min_ms, std::uint16_t max_ms);

    void SetShakeParams(float thresh, std::uint8_t min_reversals, std::uint16_t window_ms);

    void SetTapParams(float peak, std::uint16_t max_tap_ms, std::uint16_t gap_min_ms, std::uint16_t gap_max_ms);

    void EnableGesture(GestureType t, bool en); // 单独开关某手势（如关掉 tap）

private:
    // ---- 内部辅助 ----
    void UpdateOrientation(); // 由 grav_ 推 orient_（含迟滞）

    void Emit(GestureType type, int8_t axis, float strength, std::uint32_t ts);

    void PushEvent(const GestureEvent &e); // 入环形缓冲（无回调时）

    [[nodiscard]] bool GestureEnabled(GestureType t) const;

    [[nodiscard]] bool InRefractory(std::uint32_t ts) const; // 全局不应期内？

    // ---- 回调 ----
    GestureCallback gesture_cb_ = nullptr;
    OrientationCallback orient_cb_ = nullptr;

    // ---- 采样 / 滤波 ----
    bool inited_ = false;
    std::uint32_t last_ts_ = 0;
    float grav_[3] = {0, 0, 0}; // 重力估计 (m/s², EMA)

    // ---- 朝向 ----
    OrientationState orient_;
    FaceOrientation face_cand_ = FaceOrientation::Unknown; // 迟滞候选
    std::uint32_t face_cand_ts_ = 0;

    // ---- swipe 状态机 ----
    enum class SwipeState : std::uint8_t {
        Idle = 0,
        Active
    };

    SwipeState swipe_state_ = SwipeState::Idle;
    std::uint32_t swipe_start_ts_ = 0;
    float swipe_peak_abs_ = 0.0f; // 本次手势水平峰值 |lin|
    float swipe_peak_signed_ = 0.0f; // 峰值带符号（定左右）
    int8_t swipe_peak_axis_ = -1;
    float swipe_vert_peak_ = 0.0f; // 竖直轴峰值（用于排除“抬起/放下”）

    // ---- shake（变号计数）----
    int shake_last_sign_ = 0;
    std::uint8_t shake_count_ = 0;
    std::uint32_t shake_first_rev_ts_ = 0;
    std::uint32_t shake_last_rev_ts_ = 0;

    // ---- double-tap ----
    bool tap_armed_ = false; // 上一刻接近静止（可接一次 tap）
    std::uint32_t tap_spike_ts_ = 0;
    std::uint8_t tap_count_ = 0;
    std::uint32_t last_tap_ts_ = 0;

    // ---- 全局不应期 ----
    std::uint32_t last_event_ts_ = 0;

    // ---- 事件环形缓冲（轮询用）----
    static constexpr std::uint8_t RING = 8;
    GestureEvent ring_[RING];
    std::uint8_t ring_head_ = 0, ring_tail_ = 0;

    // ---- 开关 ----
    bool enabled_[5] = {true, true, true, true, true}; // 按 GestureType 值索引

    // ---- 可调参数（构造里给默认值）----
    float swipe_enter_; // 进入阈值 (m/s²)
    float swipe_exit_; // 回落阈值 (m/s²) = enter*0.5
    float swipe_peak_; // 峰值阈值 (m/s²)
    std::uint16_t swipe_min_ms_; // 脉冲最短时长
    std::uint16_t swipe_max_ms_; // 脉冲最长时长
    bool swipe_invert_ = false;

    float shake_thresh_; // 变号所需幅值 (m/s²)
    std::uint8_t shake_min_reversals_;
    std::uint16_t shake_window_ms_;

    float tap_peak_; // tap 尖峰阈值 (m/s²，应高于 swipe_peak_)
    float tap_quiet_; // 视为“静止”的幅值上限
    std::uint16_t tap_max_ms_; // 单次 tap 最长（用于重新武装）
    std::uint16_t tap_gap_min_ms_;
    std::uint16_t tap_gap_max_ms_;

    std::uint16_t global_refractory_ms_; // 事件后全局不应期

    bool debug_ = false;
};
