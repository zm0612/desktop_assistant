//
// Created by Zhang Zhimeng on 2026/9/5.
//
//
// MotionDetector 实现。见 MotionDetector.h 顶部的设计说明。
//
#include "motion_detector.h"

// ============================================================
//  常量 / 默认参数
// ============================================================
static constexpr float GRAV_TAU = 0.40f; // 重力低通时间常数 (s)：越大越能扛住甩动干扰
static constexpr float RAD2DEG = 57.2957795f;
static constexpr float FACE_ENTER = 0.70f; // 归一化重力分量 > 此值才认某一面
static constexpr std::uint32_t FACE_STABLE_MS = 200; // 面需稳定这么久才切（迟滞）

// ============================================================
//  小工具
// ============================================================
static char AxisChar(const int8_t a) {
    switch (a) {
        case 0: return 'X';
        case 1: return 'Y';
        case 2: return 'Z';
        default: return '?';
    }
}

static const char *GestureName(const GestureType t) {
    switch (t) {
        case GestureType::SwipeLeft: {
            return "SwipeLeft";
        }
        case GestureType::SwipeRight: {
            return "SwipeRight";
        }
        case GestureType::DoubleTap: {
            return "DoubleTap";
        }
        case GestureType::Shake: {
            return "Shake";
        }
        default: {
            return "None";
        }
    }
}

static const char *FaceName(const FaceOrientation f) {
    switch (f) {
        case FaceOrientation::FaceUp: {
            return "FaceUp";
        }
        case FaceOrientation::FaceDown: {
            return "FaceDown";
        }
        case FaceOrientation::Left: {
            return "Left";
        }
        case FaceOrientation::Right: {
            return "Right";
        }
        case FaceOrientation::Top: {
            return "Top";
        }
        case FaceOrientation::Bottom: {
            return "Bottom";
        }
        default: {
            return "Unknown";
        }
    }
}

// ============================================================
//  构造：装入默认参数
// ============================================================
MotionDetector::MotionDetector() {
    swipe_enter_ = 5.0f;
    swipe_peak_ = 8.0f;
    swipe_min_ms_ = 60;
    swipe_max_ms_ = 500;
    swipe_exit_ = swipe_enter_ * 0.5f;

    shake_thresh_ = 9.0f;
    shake_min_reversals_ = 4;
    shake_window_ms_ = 1000;

    tap_peak_ = 14.0f; // 高于 swipe_peak_：靠更强的“敲击”尖峰区分
    tap_quiet_ = 3.0f;
    tap_max_ms_ = 60;
    tap_gap_min_ms_ = 60;
    tap_gap_max_ms_ = 400;

    global_refractory_ms_ = 350;
}

// ============================================================
//  调参接口
// ============================================================
void MotionDetector::SetSwipeThresholds(float enter, float peak, std::uint16_t min_ms, std::uint16_t max_ms) {
    swipe_enter_ = enter;
    swipe_exit_ = enter * 0.5f;
    swipe_peak_ = peak;
    swipe_min_ms_ = min_ms;
    swipe_max_ms_ = max_ms;
}

void MotionDetector::SetShakeParams(float thresh, std::uint8_t min_reversals, std::uint16_t window_ms) {
    shake_thresh_ = thresh;
    shake_min_reversals_ = min_reversals;
    shake_window_ms_ = window_ms;
}

void MotionDetector::SetTapParams(float peak, std::uint16_t max_tap_ms, std::uint16_t gap_min_ms,
                                  std::uint16_t gap_max_ms) {
    tap_peak_ = peak;
    tap_max_ms_ = max_tap_ms;
    tap_gap_min_ms_ = gap_min_ms;
    tap_gap_max_ms_ = gap_max_ms;
}

void MotionDetector::EnableGesture(GestureType t, bool en) {
    std::uint8_t i = static_cast<std::uint8_t>(t);
    if (i < 5) enabled_[i] = en;
}

bool MotionDetector::GestureEnabled(GestureType t) const {
    std::uint8_t i = static_cast<std::uint8_t>(t);
    return (i < 5) ? enabled_[i] : false;
}

bool MotionDetector::InRefractory(std::uint32_t ts) const {
    return last_event_ts_ != 0 && (ts - last_event_ts_) < global_refractory_ms_;
}

// ============================================================
//  主更新
// ============================================================
void MotionDetector::Update(const IMUData &d) {
    // 首帧：初始化重力估计为当前加速度，避免从 0 缓慢收敛
    if (!inited_) {
        inited_ = true;
        last_ts_ = d.timestamp_;
        grav_[0] = d.ax_;
        grav_[1] = d.ay_;
        grav_[2] = d.az_;
        return;
    }

    // dt（秒），夹取以抗首帧/大卡顿
    float dt = (d.timestamp_ - last_ts_) / 1000.0f;
    last_ts_ = d.timestamp_;
    if (dt <= 0.0f) {
        dt = 0.005f;
    }

    if (dt > 0.10f) {
        dt = 0.10f;
    }
    const std::uint32_t ts = d.timestamp_;

    // 重力低通(EMA) → 去重力得线性加速度
    const float alpha = dt / (GRAV_TAU + dt);
    grav_[0] += alpha * (d.ax_ - grav_[0]);
    grav_[1] += alpha * (d.ay_ - grav_[1]);
    grav_[2] += alpha * (d.az_ - grav_[2]);

    const float lin[3] = {d.ax_ - grav_[0], d.ay_ - grav_[1], d.az_ - grav_[2]};

    // 朝向
    UpdateOrientation();

    // 竖直轴 = 重力最大分量的轴；其余两轴为水平面
    int vert = 0;
    {
        const float m0 = fabsf(grav_[0]), m1 = fabsf(grav_[1]), m2 = fabsf(grav_[2]);
        if (m1 >= m0 && m1 >= m2) vert = 1;
        else if (m2 >= m0 && m2 >= m1) vert = 2;
        else vert = 0;
    }

    int h0, h1;
    if (vert == 0) {
        h0 = 1;
        h1 = 2;
    } else if (vert == 1) {
        h0 = 0;
        h1 = 2;
    } else {
        h0 = 0;
        h1 = 1;
    }

    // 本帧水平面主导轴
    const int h = (fabsf(lin[h0]) >= fabsf(lin[h1])) ? h0 : h1;
    const float linh = lin[h];
    const float linh_abs = fabsf(linh);

    // ---------- 摇一摇（先判：优先级高于 swipe）----------
    // 水平主导轴上高幅值来回变号；窗口内累计够次数即判定。
    if (linh_abs > shake_thresh_) {
        const int s = (linh > 0.0f) ? 1 : -1;
        if (shake_last_sign_ != 0 && s != shake_last_sign_) {
            if (shake_count_ == 0 || (ts - shake_first_rev_ts_) > shake_window_ms_) {
                shake_count_ = 1;
                shake_first_rev_ts_ = ts;
            } else {
                shake_count_++;
            }
            shake_last_rev_ts_ = ts;
            if (shake_count_ >= shake_min_reversals_ &&
                GestureEnabled(GestureType::Shake) && !InRefractory(ts)) {
                Emit(GestureType::Shake, -1, linh_abs, ts);
                shake_count_ = 0;
                swipe_state_ = SwipeState::Idle; // 摇动吞掉进行中的 swipe
            }
        }
        shake_last_sign_ = s;
    }
    // 太久没有高幅值变号 → 复位摇动计数
    if (shake_count_ > 0 && (ts - shake_last_rev_ts_) > shake_window_ms_) {
        shake_count_ = 0;
        shake_last_sign_ = 0;
    }

    // ---------- 左右甩动 ----------
    switch (swipe_state_) {
        case SwipeState::Idle:
            if (linh_abs > swipe_enter_) {
                swipe_state_ = SwipeState::Active;
                swipe_start_ts_ = ts;
                swipe_peak_abs_ = linh_abs;
                swipe_peak_signed_ = linh;
                swipe_peak_axis_ = static_cast<int8_t>(h);
                swipe_vert_peak_ = fabsf(lin[vert]);
                if (debug_) {
                    Serial.printf("[MOT] impulse axis=%c val=%+.1f (vert=%c)\n",
                                  AxisChar(static_cast<int8_t>(h)), linh, AxisChar(static_cast<int8_t>(vert)));
                }
            }
            break;

        case SwipeState::Active: {
            if (linh_abs > swipe_peak_abs_) {
                swipe_peak_abs_ = linh_abs;
                swipe_peak_signed_ = linh;
                swipe_peak_axis_ = static_cast<int8_t>(h);
            }

            const float vabs = fabsf(lin[vert]);
            if (vabs > swipe_vert_peak_) {
                swipe_vert_peak_ = vabs;
            }

            const bool below_exit = (fabsf(lin[h0]) < swipe_exit_ && fabsf(lin[h1]) < swipe_exit_);
            if (below_exit) {
                const std::uint32_t dur = ts - swipe_start_ts_;
                const bool shaking = (shake_count_ >= 2); // 摇动尾巴不当 swipe
                const bool valid = swipe_peak_abs_ >= swipe_peak_ &&
                                   dur >= swipe_min_ms_ && dur <= swipe_max_ms_ &&
                                   swipe_peak_abs_ > 1.3f * swipe_vert_peak_ && // 排除“抬起/放下”
                                   !shaking && !InRefractory(ts);
                if (valid) {
                    bool positive = swipe_peak_signed_ > 0.0f; // 正向默认=右（board frame，上机校准）
                    if (swipe_invert_) {
                        positive = !positive;
                    }

                    const GestureType t = positive ? GestureType::SwipeRight : GestureType::SwipeLeft;
                    if (GestureEnabled(t)) {
                        Emit(t, swipe_peak_axis_, swipe_peak_abs_, ts);
                    }
                }
                swipe_state_ = SwipeState::Idle;
            }
            break;
        }
    }

    // ---------- 双击（best-effort）----------
    // tap = 从“接近静止”起的一次短促强尖峰；两次间隔在 [gap_min, gap_max] → 双击。
    const float linmag = sqrtf(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
    if (linmag < tap_quiet_) {
        if (!tap_armed_ && (ts - tap_spike_ts_) > tap_max_ms_) tap_armed_ = true;
    }
    if (tap_armed_ && linmag > tap_peak_) {
        tap_armed_ = false;
        tap_spike_ts_ = ts;
        const bool busy = (swipe_state_ == SwipeState::Active) || (shake_count_ >= 2);
        if (tap_count_ > 0 &&
            (ts - last_tap_ts_) >= tap_gap_min_ms_ &&
            (ts - last_tap_ts_) <= tap_gap_max_ms_) {
            if (GestureEnabled(GestureType::DoubleTap) && !InRefractory(ts) && !busy) {
                Emit(GestureType::DoubleTap, -1, linmag, ts);
            }
            tap_count_ = 0;
            last_tap_ts_ = 0;
        } else {
            tap_count_ = 1;
            last_tap_ts_ = ts;
        }
    }
    if (tap_count_ > 0 && (ts - last_tap_ts_) > tap_gap_max_ms_) {
        tap_count_ = 0;
    } // 单击超时作废
}

// ============================================================
//  朝向：由重力估计推离散面(迟滞) + pitch/roll
// ============================================================
void MotionDetector::UpdateOrientation() {
    const float gmag = sqrtf(grav_[0] * grav_[0] + grav_[1] * grav_[1] + grav_[2] * grav_[2]);
    if (gmag < 1e-3f) {
        return;
    }

    const float ux = grav_[0] / gmag, uy = grav_[1] / gmag, uz = grav_[2] / gmag;
    orient_.gx = ux;
    orient_.gy = uy;
    orient_.gz = uz;
    orient_.roll_deg = atan2f(uy, uz) * RAD2DEG;
    orient_.pitch_deg = atan2f(-ux, sqrtf(uy * uy + uz * uz)) * RAD2DEG;

    // 主导轴 → 候选面
    int ax = 0;
    float mx = fabsf(ux);
    if (fabsf(uy) > mx) {
        mx = fabsf(uy);
        ax = 1;
    }
    if (fabsf(uz) > mx) {
        mx = fabsf(uz);
        ax = 2;
    }
    auto cand = FaceOrientation::Unknown;
    if (mx > FACE_ENTER) {
        switch (ax) {
            case 0: cand = (ux > 0) ? FaceOrientation::Right : FaceOrientation::Left;
                break;
            case 1: cand = (uy > 0) ? FaceOrientation::Bottom : FaceOrientation::Top;
                break;
            case 2: cand = (uz > 0) ? FaceOrientation::FaceDown : FaceOrientation::FaceUp;
                break;
            default: ;
        }
    }

    // 迟滞：候选面稳定 FACE_STABLE_MS 才切
    if (cand != face_cand_) {
        face_cand_ = cand;
        face_cand_ts_ = last_ts_;
    }

    if (cand != FaceOrientation::Unknown && cand != orient_.face &&
        last_ts_ - face_cand_ts_ >= FACE_STABLE_MS) {
        orient_.face = cand;
        if (debug_) {
            Serial.printf("[MOT] face=%s pitch=%.0f roll=%.0f\n",
                          FaceName(cand), orient_.pitch_deg, orient_.roll_deg);
        }

        if (orient_cb_) {
            orient_cb_(orient_);
        }
    }
}

// ============================================================
//  事件产出
// ============================================================
void MotionDetector::Emit(const GestureType type, const int8_t axis, const float strength, const std::uint32_t ts) {
    last_event_ts_ = ts;
    const GestureEvent e{.type = type, .axis = axis, .strength = strength, .t = ts};

    // 无回调时才打印事件行，避免与用户回调重复；诊断(impulse/face)始终按 debug_ 打印
    if (debug_ && !gesture_cb_) {
        Serial.printf("[MOT] %s axis=%c strength=%.1f\n", GestureName(type), AxisChar(axis), strength);
    }

    if (gesture_cb_) {
        gesture_cb_(e);
    } else {
        PushEvent(e);
    }
}

void MotionDetector::PushEvent(const GestureEvent &e) {
    ring_[ring_head_] = e;
    ring_head_ = (ring_head_ + 1) % RING;

    if (ring_head_ == ring_tail_) {
        ring_tail_ = (ring_tail_ + 1) % RING;
    } // 满则丢最旧
}

bool MotionDetector::PollGesture(GestureEvent &out) {
    if (ring_head_ == ring_tail_) {
        return false;
    }

    out = ring_[ring_tail_];
    ring_tail_ = (ring_tail_ + 1) % RING;
    return true;
}
