//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "stock_page.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ctime>
#include <cstring>

// ---- 布局坐标（240x240）----
#define STK_DIV1_Y   26
#define STK_ROW0_Y   52
#define STK_ROW_DY   34
#define STK_ALIAS_X  10
#define STK_PRICE_X  70
#define STK_CHG_RX   232
#define STK_DIV2_Y   156
#define STK_TICK_LABEL_Y 168
#define STK_TICK_BAND_TOP 178
#define STK_TICK_BAND_H  26
#define STK_TICK_Y   191        // 跑马灯基线(ML)

void StockPage::Begin() {
    reqn_ = 0;
    const String s(symbols_);
    int start = 0;
    while (reqn_ < MAX_Q) {
        const int c = s.indexOf(',', start);
        String tok = (c < 0) ? s.substring(start) : s.substring(start, c);
        tok.trim();
        if (tok.length()) req_[reqn_++] = tok;
        if (c < 0) break;
        start = c + 1;
    }
}

// ================================ 生命周期 ================================
void StockPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    last_clock_ms_ = 0;
    scroll_x_ = 240;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    shown_dot_ = app_ctx_.wifi_up_;
    DrawClock();
    DrawIndices();
    DrawTicker();
}

void StockPage::Tick() {
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_fetch_ms_ >= STOCK_INTERVAL)) {
        need_fetch_ = false;
        last_fetch_ms_ = millis();
        FetchQuotes();
    }

    if (qn_ > idx_count_ && millis() - last_scroll_ms_ >= 40) {
        // 跑马灯步进
        last_scroll_ms_ = millis();
        scroll_x_ -= 3;
        if (ticker_total_ > 0 && scroll_x_ <= -ticker_total_) scroll_x_ = 240;
        ticker_dirty_ = true;
    }
}

void StockPage::Render() {
    DrawStatusDot();
    if (AppContext::TimeReady() && millis() - last_clock_ms_ >= 1000) {
        last_clock_ms_ = millis();
        DrawClock();
    }
    if (dirty_) {
        dirty_ = false;
        DrawIndices();
    }
    if (ticker_dirty_) {
        ticker_dirty_ = false;
        DrawTicker();
    }
}

// ================================ 数据 ================================
bool StockPage::FetchQuotes() {
    if (!app_ctx_.wifi_up_) return false;
    const String url = String("http://qt.gtimg.cn/q=") + symbols_;
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    bool ok = false;
    if (status == 200) {
        const String p = http.getString();
        // Serial.println(p); // 调试：核对腾讯返回字段用，稳定后可删
        int from = 0;
        qn_ = 0;
        while (qn_ < MAX_Q) {
            // 顺序解析每段 ="..."; 与请求顺序一致
            const int q1 = p.indexOf("=\"", from);
            if (q1 < 0) break;
            const int q2 = p.indexOf('"', q1 + 2);
            if (q2 < 0) break;
            ParseSeg(p.substring(q1 + 2, q2), q_[qn_]);
            qn_++;
            from = q2 + 1;
        }
        if (qn_ > 0) {
            BuildTickerMetrics();
            dirty_ = true;
            ok = true;
        }
    }
    http.end();
    return ok;
}

// 腾讯字段：0~market 1~名称 2~代码 3~现价 4~昨收 ...
void StockPage::ParseSeg(const String &seg, Quote &q) {
    int f = 0, start = 0;
    const int len = static_cast<int>(seg.length());
    String code, price, prev;
    for (int i = 0; i <= len; i++) {
        if (i == len || seg[i] == '~') {
            const String tok = seg.substring(start, i);
            if (f == 2) code = tok;
            else if (f == 3) price = tok;
            else if (f == 4) {
                prev = tok;
                break;
            }
            f++;
            start = i + 1;
        }
    }
    strncpy(q.code, code.c_str(), sizeof(q.code) - 1);
    q.code[sizeof(q.code) - 1] = 0;
    q.price = price.toFloat();
    q.prevClose = prev.toFloat();
    q.changePct = (q.prevClose > 0) ? (q.price - q.prevClose) / q.prevClose * 100.0f : 0;
    q.valid = q.prevClose > 0;
}

void StockPage::BuildTickerMetrics() {
    ticker_total_ = 0;
    constexpr int gap = 26;
    for (int i = idx_count_; i < qn_; i++) {
        char alias[12];
        AliasFor(req_[i].c_str(), alias, sizeof(alias));
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %.2f %+.2f%%", alias, q_[i].price, q_[i].changePct);
        ticker_total_ += app_ctx_.tft_.textWidth(buf, 2) + gap;
    }
}

// ================================ 渲染 ================================
void StockPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("Stocks", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, STK_DIV1_Y, 224, COL_DIV);
    app_ctx_.tft_.drawFastHLine(8, STK_DIV2_Y, 224, COL_DIV);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("Watchlist", 10, STK_TICK_LABEL_Y, 2);
}

void StockPage::DrawClock() {
    char b[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(b, "--:--");
    }
    String s(b);
    if (s == shown_time_) return;
    shown_time_ = s;
    app_ctx_.tft_.fillRect(150, 2, 74, 18, COL_BG);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, 212, 11, 2);
}

void StockPage::DrawStatusDot() {
    if (shown_dot_ == app_ctx_.wifi_up_) return;
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void StockPage::DrawIndices() const {
    app_ctx_.tft_.fillRect(0, STK_DIV1_Y + 2, 240, STK_DIV2_Y - (STK_DIV1_Y + 2), COL_BG);
    if (qn_ == 0) {
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("loading...", 120, (STK_DIV1_Y + STK_DIV2_Y) / 2, 4);
        return;
    }

    const int rows = (idx_count_ < qn_) ? idx_count_ : qn_;

    for (int i = 0; i < rows; i++) {
        int y = STK_ROW0_Y + i * STK_ROW_DY;
        char alias[12];
        AliasFor(req_[i].c_str(), alias, sizeof(alias));
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_TIME);
        app_ctx_.tft_.drawString(alias, STK_ALIAS_X, y, 4);

        char pbuf[16];
        // snprintf(pbuf, sizeof(pbuf), "%.1f", _q[i].price);
        snprintf(pbuf, sizeof(pbuf), "%i", static_cast<int>(q_[i].price));
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString(pbuf, STK_PRICE_X, y, 4);

        char cbuf[16];
        snprintf(cbuf, sizeof(cbuf), "%+.2f%%", q_[i].changePct);
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(q_[i].changePct >= 0 ? COL_UP : COL_DOWN);
        app_ctx_.tft_.drawString(cbuf, STK_CHG_RX, y, 4);
    }
}

void StockPage::DrawTicker() const {
    // 1. 创建与TICK区域大小一致的离屏缓冲区（宽度240，高度 STK_TICK_BAND_H）
    // 注意：TFT_eSPI 的 Sprite 构造函数需要传入 tft 对象指针
    auto spr = TFT_eSprite(&app_ctx_.tft_);
    spr.createSprite(240, STK_TICK_BAND_H);

    // 2. 在缓冲区中绘制背景（相当于之前的 fillRect，但不会显示在屏幕上）
    spr.fillSprite(COL_BG);

    // 3. 设置文字对齐（注意：坐标相对 Sprite 自身左上角，而非屏幕绝对坐标）
    spr.setTextDatum(ML_DATUM);

    int x = scroll_x_;
    constexpr int gap = 26;

    for (int i = idx_count_; i < qn_; i++) {
        char alias[12];
        AliasFor(req_[i].c_str(), alias, sizeof(alias));

        char buf[48];
        snprintf(buf, sizeof(buf), "%s %.2f %+.2f%%", alias, q_[i].price, q_[i].changePct);

        // 在缓冲区中绘制文字
        spr.setTextColor(q_[i].changePct >= 0 ? COL_UP : COL_DOWN);
        spr.drawString(buf, x, STK_TICK_Y - STK_TICK_BAND_TOP, 2); // Y坐标需要转为相对坐标

        x += spr.textWidth(buf, 2) + gap;
    }

    // 4. 【关键】将整个缓冲区原子性地推送到屏幕的指定位置，彻底消除撕裂和闪烁
    spr.pushSprite(0, STK_TICK_BAND_TOP);

    // 5. 释放占用的内存（必须释放，否则会内存泄漏）
    spr.deleteSprite();
}

// void StockPage::drawTicker() {
//     _ctx.tft.fillRect(0, STK_TICK_BAND_TOP, 240, STK_TICK_BAND_H, COL_BG);
//     _ctx.tft.setTextDatum(ML_DATUM);
//     int x = _scrollX;
//     const int gap = 26;
//     for (int i = _idxCount; i < _qn; i++) {
//         char alias[12];
//         aliasFor(_req[i].c_str(), alias, sizeof(alias));
//         char buf[48];
//         snprintf(buf, sizeof(buf), "%s %.2f %+.2f%%", alias, _q[i].price, _q[i].changePct);
//         _ctx.tft.setTextColor(_q[i].changePct >= 0 ? COL_UP : COL_DOWN);
//         _ctx.tft.drawString(buf, x, STK_TICK_Y, 2);
//         x += _ctx.tft.textWidth(buf, 2) + gap;
//     }
// }

void StockPage::AliasFor(const char *reqCode, char *out, size_t n) {
    if (!strcmp(reqCode, "sh000001")) {
        strncpy(out, "SH", n);
    } else if (!strcmp(reqCode, "sz399001")) {
        strncpy(out, "SZ", n);
    } else if (!strcmp(reqCode, "sz399006")) {
        strncpy(out, "CY", n);
    } else if (!strcmp(reqCode, "sh000300")) {
        strncpy(out, "HS300", n);
    } else {
        const char *pfx = (strlen(reqCode) > 2) ? reqCode + 2 : reqCode; // 去掉 sh/sz
        strncpy(out, pfx, n);
    }
    out[n - 1] = 0;
}
