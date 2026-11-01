//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once
#include "page.h"

// 全屏酷炫时钟：7 段大字时:分 + 边缘秒进度环(平滑扫过) + 星期日期
class ClockPage : public Page {
public:
    explicit ClockPage(AppContext &ctx) : Page(ctx) {
    }

    void OnEnter() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 12000; }

private:
    void DrawTime(bool force);

    void DrawDate(bool force);

    void DrawSecondsRing(int sec, int prevSec) const;

    void DrawDot() const;

    String shown_hm_, shown_date_;
    int shown_sec_ = -1;
    int shown_dot_ = -1;
};
