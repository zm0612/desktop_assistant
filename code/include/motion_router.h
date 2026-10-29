//
// Created by Zhang Zhimeng on 2026/9/30.
//
// 体感翻页路由器：把 MotionDetector 的输出（静止朝向 + 手势）翻译成「当前应显示哪一页」，
// 实现 IOrient 供 PageManager 每轮拉取决策，从而让 PageManager 与具体传感器解耦。
//
// 语义：
//  - 朝向(face) 是「持续态」：某个面持续朝向时，一直显示该面映射的页。
//  - 手势(gesture) 是「瞬时态」：触发瞬间锁定其映射页并强制停留 holdMs，其间忽略朝向；
//    到期后退回「按当前朝向显示」。以此化解手势与朝向的冲突（见 CLAUDE/需求）。
//
// 映射来自 Config.sw（页面 id 字符串，空串=不映射）；id 经 PageManager 解析成「启用顺序下标」。
// 目标页未启用/不存在 => 视为无映射（orientationPage 返回 -1 = 维持当前）。
//
#pragma once
#include "page_manager.h"     // IOrient
#include "motion_detector.h"  // GestureEvent / FaceOrientation / MotionDetector
#include "config.h"           // FACE_COUNT / GESTURE_COUNT / Config

class MotionRouter final : public IOrient {
public:
    // 载入 Config.sw 映射快照并记录依赖。须在 Config::Load() 与 PageManager::Begin() 之后调用。
    void Begin(PageManager *pm, MotionDetector *det);

    // 由 MotionDetector 手势回调转发：命中映射则锁定该页并开始 hold 计时。
    void HandleGesture(const GestureEvent &e);

    // IOrient：返回「启用顺序下标」；-1 = 维持当前（无映射/朝向未知）。
    int OrientationPage() override;

private:
    [[nodiscard]] int ResolveId(const String &id) const; // 页 id -> 启用顺序下标(-1)

    PageManager *pm_ = nullptr;
    MotionDetector *det_ = nullptr;

    // 映射快照（Begin 时从 Config 拷贝；本设备 save→重启 模型下，运行期不再变）
    String face_map_[FACE_COUNT];       // i = FaceOrientation 值-1
    String gesture_map_[GESTURE_COUNT]; // i = GestureType 值-1
    uint32_t hold_ms_ = 5000;

    // 手势停留锁存
    int hold_order_ = -1;      // 停留目标（启用顺序下标）；-1 = 当前不在停留
    uint32_t hold_until_ = 0;  // 停留截止时刻 (millis)
};
