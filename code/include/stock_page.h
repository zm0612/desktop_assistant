//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once

#include "page.h"

// 股票页：顶部固定显示若干指数，底部横向跑马灯滚动个股
// symbols：腾讯行情代码，逗号分隔，如 "sh000001,sz399001,sz399006,sh600519"
// idxCount：前 idxCount 个当"指数"固定显示，其余进底部滚动
class StockPage : public Page {
public:
    StockPage(AppContext &ctx, const char *symbols, const int idxCount)
        : Page(ctx), symbols_(symbols), idx_count_(idxCount) {
    }

    void Begin() override; // 拆分 symbols

    void OnEnter() override;

    void Tick() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 12000; }

private:
    struct Quote {
        char code[12] = "";
        float price = 0;
        float prevClose = 0;
        float changePct = 0;
        bool valid = false;
    };

    bool FetchQuotes();

    static void ParseSeg(const String &seg, Quote &q);

    void BuildTickerMetrics(); // 预算跑马灯总宽

    void DrawHeader() const;

    void DrawClock();

    void DrawStatusDot();

    void DrawIndices() const;

    void DrawTicker() const;

    static void AliasFor(const char *reqCode, char *out, size_t n);

    const char *symbols_;
    int idx_count_;

    static constexpr int MAX_Q = 16;
    String req_[MAX_Q]; // 请求代码（带 sh/sz 前缀，用于别名）
    int reqn_ = 0;
    Quote q_[MAX_Q];
    int qn_ = 0;

    bool dirty_ = false; // 有新行情 -> 重绘指数块
    bool ticker_dirty_ = false; // 跑马灯需重绘
    bool need_fetch_ = true;
    uint32_t last_fetch_ms_ = 0;
    uint32_t last_scroll_ms_ = 0;
    uint32_t last_clock_ms_ = 0;
    int scroll_x_ = 240;
    int ticker_total_ = 0;

    String shown_time_;
    bool shown_dot_ = false;

    static constexpr uint32_t STOCK_INTERVAL = 60UL * 1000UL; // 行情刷新 60s
};
