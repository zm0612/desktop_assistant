//
// Created by Zhang Zhimeng on 2026/9/30.
//
#include "motion_router.h"

void MotionRouter::Begin(PageManager *pm, MotionDetector *det) {
    pm_ = pm;
    det_ = det;

    const Config &C = Config::Get();
    for (int i = 0; i < FACE_COUNT; i++) {
        face_map_[i] = C.sw.face_map_[i];
    }
    for (int i = 0; i < GESTURE_COUNT; i++) {
        gesture_map_[i] = C.sw.gesture_map_[i];
    }

    int hs = C.sw.gesture_hold_s_;
    if (hs < 1) hs = 1; // 夹取：至少停留 1s，避免 0 导致刚切走就被朝向覆盖
    if (hs > 60) hs = 60;
    hold_ms_ = static_cast<uint32_t>(hs) * 1000UL;
}

// 页 id -> 启用顺序下标；空串/无 PM/未启用 -> -1。
int MotionRouter::ResolveId(const String &id) const {
    if (!pm_ || id.length() == 0) {
        return -1;
    }
    return pm_->OrderIndexOfId(id.c_str());
}

// 手势回调转发：命中映射（且目标页已启用）才锁定并计时；否则忽略该手势。
void MotionRouter::HandleGesture(const GestureEvent &e) {
    const int gi = static_cast<int>(e.type) - 1; // SwipeLeft=1 -> 0 … Shake=4 -> 3
    if (gi < 0 || gi >= GESTURE_COUNT) {
        return; // None 或越界
    }

    const int order = ResolveId(gesture_map_[gi]);
    if (order < 0) {
        return; // 该手势未映射 / 目标未启用：不打断当前显示
    }

    hold_order_ = order;
    hold_until_ = millis() + hold_ms_;
}

int MotionRouter::OrientationPage() {
    // 1) 手势停留窗口内：锁定其页，忽略朝向。
    //    有符号差判定 now<hold_until_，天然抗 millis() 回绕（间隔 ≤60s « 2^31ms）。
    if (hold_order_ >= 0) {
        if (static_cast<int32_t>(millis() - hold_until_) < 0) {
            return hold_order_; // 仍在停留
        }
        hold_order_ = -1; // 到期：退回按朝向显示
    }

    // 2) 按当前静止朝向映射（离散 6 面，带迟滞，不抖）。
    const FaceOrientation f = det_ ? det_->Orientation().face : FaceOrientation::Unknown;
    const int fi = static_cast<int>(f) - 1; // FaceUp=1 -> 0 … Bottom=6 -> 5
    if (fi >= 0 && fi < FACE_COUNT) {
        return ResolveId(face_map_[fi]);
    }

    return -1; // 朝向未知：维持当前
}
