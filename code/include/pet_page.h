//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once
#include "page.h"

// 萌宠页：绘图指令画的萌系橙猫（幼态比例 + 抗锯齿平滑边缘）
// 白天随机小动作：眨眼 / 双眨 / 挤单眼 / 喵(张嘴) / 兴奋(大眼+爱心+快甩尾) + 摇尾
// 夜间(22:00-7:00)：闭眼睡觉 + 冒 Zzz
class PetPage : public Page {
public:
    explicit PetPage(AppContext &ctx) : Page(ctx) {
    }

    void OnEnter() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 14000; }

private:
    void DrawHeader() const;

    void DrawClock();

    void DrawStaticCat() const;

    void DrawNoseMouth() const;

    void DrawMeowMouth() const;

    void DrawEyes(int mode) const; // 0开 1闭 2眨左 3眨右 4大眼

    void DrawOneEye(int cx, int st) const; // 单眼：0开 1闭 2大

    void DrawTail(int phase) const;

    void DrawHeart(int x, int y, uint16_t c) const;

    void UpdateHearts();

    void ClearHearts();

    void DrawZzz(int phase) const;

    static bool IsNight() ;

    struct Heart {
        bool on_ = false;
        int x_ = 0;
        int y_ = 0;
    };

    Heart hearts_[3];

    uint32_t last_frame_ms_ = 0, last_heart_ms_ = 0;
    int gesture_ = 0, g_phase_ = 0; // 当前动作 / 子阶段
    uint32_t g_start_ = 0, next_gesture_ms_ = 0; // 动作起点 / 下次触发
    uint32_t tail_fast_until_ = 0; // 快甩尾持续到
    int tail_phase_ = 0, zzz_phase_ = 0;
    int shown_night_ = -1;
    String shown_time_;
    int shown_dot_ = -1;
};
