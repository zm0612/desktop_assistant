//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once
#include "page.h"

// 系统状态页：ESP8266 自身运行信息（全部本地读取，无需联网）
class SystemPage : public Page {
public:
    explicit SystemPage(AppContext &ctx) : Page(ctx) {
    }

    void OnEnter() override;

    void Tick() override;

    void BackgroundTick() override; // 持续采样空闲堆，供趋势图（所有页都跑）

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override {
        return 9000;
    }

private:
    void ReadStats();

    void DrawHeader() const;

    void DrawClock();

    void DrawStatusDot();

    void DrawBody(bool force = false);

    void DrawBar(int x, int y, int w, int h, int pct, uint16_t color,
                 int oldPct, uint16_t oldCol, bool force) const;

    void DrawSignal(int x, int y, int level); // 5 格信号，电平变化才重画

    void DrawSparkline(bool force = false);

    // 原位文本替换（见 system_page.cpp）：只擦除重画变化的字符
    void ReplaceText(const String &oldS, const String &newS,
                     int32_t x, int32_t y, uint8_t font, uint16_t fg, uint8_t datum) const;

    static int RssiToPct(int8_t rssi);

    static int RssiToLevel(long rssi);

    // 采集到的状态
    int8_t rssi_ = 0;
    String ssid_, ip_, reset_;
    uint32_t free_heap_ = 0, max_block_ = 0;
    uint8_t frag_ = 0, cpu_ = 0;
    uint32_t sketch_used_ = 0, sketch_free_ = 0;
    float battery_v_ = 0.0f; // 电池电压(V)，取自 AppContext（main.cpp 每秒采样）

    // 空闲堆趋势环形缓冲
    static constexpr int HB = 110;
    uint16_t heap_hist_[HB]{};
    int hist_count_ = 0, hist_head_ = 0;

    uint32_t last_sample_ms_ = 0, last_refresh_ms_ = 0;
    bool dirty_ = true;

    // 原位重绘缓存：与「已显示」值比较，仅变化项局部重画，避免整区 fillRect 闪烁
    int shown_dot_ = -1;
    int shown_level_ = -1; // 信号条电平，-1=尚未画
    int prev_count_ = 0; // 上次折线点数（<2 表示无线）
    String shown_time_;
    String shown_wifi_, shown_ssid_, shown_ip_;
    String shown_heap_, shown_frag_txt_, shown_flash_txt_, shown_flash_pct_;
    String shown_up_, shown_cpu_;
    String shown_bat_; // 电池电压文本原位重绘缓存
    uint8_t shown_frag_ = -1, shown_flash_ = -1; // 两条进度条的当前百分比
    uint16_t shown_frag_col_ = 0, shown_flash_col_ = 0;
    int prev_y_[HB]{}; // 上次画出的折线 y 坐标（原位擦除用）
};
