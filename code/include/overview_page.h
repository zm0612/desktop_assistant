//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once
#include "page.h"

#include "gpu_page.h"

// 概览仪表盘：一屏四格 —— 时间 / 天气 / 一只自选股 / GPU 占用
struct OverviewConfig {
    const char *api_key_; // 心知 key
    const char *location_; // 城市
    const char *stock_; // 一只腾讯代码，如 "sh600519"
};

class OverviewPage : public Page {
public:
    OverviewPage(AppContext &ctx, const OverviewConfig &cfg, GpuPage &gpu)
        : Page(ctx), overview_cfg_(cfg), gpu_(gpu) {
    }

    void OnEnter() override;

    void Tick() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 10000; }

private:
    bool FetchWeather();

    bool FetchStock();

    void DrawTime(bool force);

    void DrawWeatherCell(bool force = false);

    void DrawStockCell(bool force = false);

    void DrawGpuCell(bool force);

    // 原位文本替换（见 overview_page.cpp）：只擦除重画变化的字符，避免整块 fillRect 闪烁
    void ReplaceText(const String &old_s, const String &new_s,
                     int32_t ox, int32_t oy, int32_t nx, int32_t ny,
                     uint8_t font, uint16_t fg) const;

    // 温度度数圈（双圈画法）；用背景色画即为原位擦除
    void DrawDegree(int32_t cx, uint16_t color) const;

    OverviewConfig overview_cfg_;
    GpuPage &gpu_;

    int temp_ = -999, w_code_ = 99;
    bool w_valid_ = false;
    float stk_price_ = NAN, stk_pct_ = NAN;
    bool s_valid = false;

    bool need_fetch_ = true;
    uint32_t last_w_ms_ = 0, last_s_ms_ = 0, last_clock_ms_ = 0;

    // 原位重绘缓存：记录已显示内容，更新时只重画变化的部分
    String shown_hm_;
    String shown_date_;
    String shown_temp_;
    String shown_w_name_;
    String shown_price_;
    String shown_pct_;
    int shown_gpu_ = -2;
    int32_t temp_cx_ = 0; // 度数圈圆心 x，0 表示当前无圈
    uint16_t shown_bar_col_ = 0; // 进度条当前颜色

    static constexpr uint32_t W_INT = 15UL * 60UL * 1000UL;
    static constexpr uint32_t S_INT = 60UL * 1000UL;
};
