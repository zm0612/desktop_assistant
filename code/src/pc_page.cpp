//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "pc_page.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ctime>

// ---- 布局坐标（240x240）----
#define PC_DIV1     28
#define PC_GAUGE_CY 92
#define PC_CPU_CX   68
#define PC_RAM_CX   172
#define PC_GAUGE_R  40
#define PC_DIV2     140
#define PC_VOL_Y    156
#define PC_VOL_BAR_Y 165
#define PC_DIV3     190
#define PC_NET_Y    220

static uint16_t Lerp565(uint16_t a, uint16_t b, int t) {
    const int ar = (a >> 11) & 0x1F;
    // t: 0..100
    const int ag = (a >> 5) & 0x3F;
    const int ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F;
    const int bg = (b >> 5) & 0x3F;
    const int bb = b & 0x1F;
    const int r = ar + (br - ar) * t / 100;
    int g = ag + (bg - ag) * t / 100;
    const int bl = ab + (bb - ab) * t / 100;
    return (r << 11) | (g << 5) | bl;
}

uint16_t PcPage::LoadColor(const int pct) {
    if (pct < 0) return COL_LABEL;
    if (pct <= 60) return Lerp565(0x07E0, 0xFE60, pct * 100 / 60);
    return Lerp565(0xFE60, 0xF800, (pct - 60) * 100 / 40);
}

// ================================ 生命周期 ================================
void PcPage::OnEnter() {
    down_last_.clear();
    up_last_.clear();
    cpu_last_ = -1;
    ram_last_ = -1;
    vol_last_ = -1;

    app_ctx_.tft_.fillScreen(COL_BG);

    DrawHeader();

    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    shown_online_ = -1;
    last_clock_ms_ = 0;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    DrawClock();

    DrawBody();
}

void PcPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }

    DrawClock();

    const bool on = valid_ && millis() - last_recv_ms_ < STALE_MS; // PC 是否在线
    if (shown_online_ != static_cast<int>(on)) {
        online_ = on;
        dirty_ = true;
    }

    if (dirty_) {
        dirty_ = false;
        shown_online_ = on;
        online_ = on;
        DrawBody();
    }
}

// ================================ 接收 PC 推送 ================================
// 由 WebPortal 的 /pc 路由调用，传入共用的 80 端口 server（Task 5）
void PcPage::HandlePost(ESP8266WebServer &s) {
    String body = s.arg("plain");
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    if (deserializeJson(doc, body)) {
        s.send(400, "text/plain", "bad json");
        return;
    }

    if (!doc["cpu"].isNull()) {
        cpu_ = doc["cpu"].as<int>();
    }

    if (!doc["ram"].isNull()) {
        ram_ = doc["ram"].as<int>();
    }

    if (!doc["vol"].isNull()) {
        vol_ = doc["vol"].as<int>();
    }

    if (!doc["down"].isNull()) {
        down_ = doc["down"].as<String>();
    }

    if (!doc["up"].isNull()) {
        up_ = doc["up"].as<String>();
    }

    valid_ = true;
    dirty_ = true;
    last_recv_ms_ = millis();
    s.send(200, "text/plain", "ok");
}

// ================================ 渲染 ================================
void PcPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("PC", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, PC_DIV1, 224, COL_DIV);
}

void PcPage::DrawClock() {
    char b[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(b, "--:--");
    }

    const String s(b);
    if (s == shown_time_) {
        return;
    }

    shown_time_ = s;
    app_ctx_.tft_.fillRect(150, 2, 74, 18, COL_BG);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, 212, 11, 2);
}

void PcPage::DrawGauge(const int cx, const int cy, const int pct, const char *label, const String &valTxt) const {
    // 获取当前显示区域尺寸
    const int max_width = app_ctx_.tft_.textWidth("100%", 4); // 最大可能宽度
    const int font_height = app_ctx_.tft_.fontHeight(4);

    // 清除区域（用背景色填充）
    app_ctx_.tft_.fillRect(cx - max_width / 2, cy - 4 - font_height / 2, max_width, font_height, COL_BG);

    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(pct >= 0 ? COL_TIME : COL_LABEL, COL_BG);
    app_ctx_.tft_.drawString(pct >= 0 ? valTxt : String("--"), cx, cy - 4, 4);

    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString(label, cx, cy + 20, 2);

    constexpr int r0 = PC_GAUGE_R;
    constexpr int r1 = PC_GAUGE_R - 8;
    app_ctx_.tft_.drawSmoothArc(cx, cy, r0, r1, 30, 330, COL_DIV, COL_BG);
    if (pct >= 0) {
        const int p = pct > 100 ? 100 : pct;
        const int end = 30 + (330 - 30) * p / 100;
        if (end > 31) app_ctx_.tft_.drawSmoothArc(cx, cy, r0, r1, 30, end, LoadColor(pct), COL_BG);
    }
}

void PcPage::DrawVolume() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL, COL_BG);
    app_ctx_.tft_.drawString("VOL", 12, PC_VOL_BAR_Y - 1, 2);

    constexpr int x = 46;
    constexpr int w = 150;
    constexpr int y = PC_VOL_BAR_Y - 6;
    constexpr int h = 10;
    app_ctx_.tft_.drawRoundRect(x, y, w, h, 3, COL_DIV);
    const int fw = (vol_ >= 0) ? (w - 2) * (vol_ > 100 ? 100 : vol_) / 100 : 0;
    app_ctx_.tft_.fillRoundRect(x + 1, y + 1, fw, h - 2, 2, COL_DATE);
    app_ctx_.tft_.fillRect(x + 1 + fw, y + 1, (w - 2) - fw, h - 2, COL_BG);

    char vb[8];
    snprintf(vb, sizeof(vb), "%d%%", vol_ < 0 ? 0 : vol_);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(vol_ >= 0 ? COL_TIME : COL_LABEL, COL_BG);
    app_ctx_.tft_.drawString(vol_ >= 0 ? vb : "--", 230, PC_VOL_BAR_Y - 1, 2);
}

void PcPage::DrawBody() {
    if (!valid_) {
        // 还没收到 PC 数据 -> 告诉用户往哪发
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("Waiting for PC...", 120, 96, 4);
        app_ctx_.tft_.setTextColor(COL_DIV);
        app_ctx_.tft_.drawString("POST to", 120, 130, 2);
        const String url = "http://" + WiFi.localIP().toString() + "/pc"; // 数据端点已并入 80 端口
        app_ctx_.tft_.setTextColor(COL_DATE);
        app_ctx_.tft_.drawString(url, 120, 150, 2);
        valid_last_ = false;
        return;
    }

    if (!valid_last_ && valid_) {
        app_ctx_.tft_.fillRect(0, 96 - 13, 240, 150 + 8, COL_BG);
        valid_last_ = true;
    }

    // 离线小提示（数据变旧）
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(online_ ? COL_BG : COL_OFFLINE);
    app_ctx_.tft_.drawString("PC offline", 120, 40, 2);

    if (cpu_ != cpu_last_) {
        char cbuf[8];
        snprintf(cbuf, sizeof(cbuf), "%d%%", cpu_ < 0 ? 0 : cpu_);
        DrawGauge(PC_CPU_CX, PC_GAUGE_CY, cpu_, "CPU", cbuf);
        cpu_last_ = cpu_;
    }

    if (ram_ != ram_last_) {
        char rbuf[8];
        snprintf(rbuf, sizeof(rbuf), "%d%%", ram_ < 0 ? 0 : ram_);
        DrawGauge(PC_RAM_CX, PC_GAUGE_CY, ram_, "RAM", rbuf);
        ram_last_ = ram_;
    }

    app_ctx_.tft_.drawFastHLine(8, PC_DIV2, 224, COL_DIV);

    if (vol_ != vol_last_) {
        DrawVolume();
        vol_last_ = vol_;
    }

    if (down_ != down_last_) {
        app_ctx_.tft_.drawFastHLine(8, PC_DIV3, 224, COL_DIV);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_ONLINE, COL_BG);
        if (down_.length() >= down_last_.length()) {
            app_ctx_.tft_.setTextColor(COL_ONLINE, COL_BG);
            app_ctx_.tft_.drawString("DN " + (down_.length() ? down_ : String("--")), 12, PC_NET_Y, 4);
        } else {
            app_ctx_.tft_.setTextColor(COL_BG, COL_BG);
            app_ctx_.tft_.drawString("DN " + (down_last_.length() ? down_last_ : String("--")), 12, PC_NET_Y, 4);
            app_ctx_.tft_.setTextColor(COL_ONLINE, COL_BG);
            app_ctx_.tft_.drawString("DN " + (down_.length() ? down_ : String("--")), 12, PC_NET_Y, 4);
        }
        down_last_ = down_;
    }

    if (up_ != up_last_) {
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(0xFD20, COL_BG);
        if (up_.length() >= up_last_.length()) {
            app_ctx_.tft_.setTextColor(0xFD20, COL_BG);
            app_ctx_.tft_.drawString("UP " + (up_.length() ? up_ : String("--")) + pc_cfg_.net_unit_, 230, PC_NET_Y, 4);
        } else {
            app_ctx_.tft_.setTextColor(COL_BG, COL_BG);
            app_ctx_.tft_.drawString("UP " + (up_last_.length() ? up_last_ : String("--")) + pc_cfg_.net_unit_, 230,
                                     PC_NET_Y, 4);
            app_ctx_.tft_.setTextColor(0xFD20, COL_BG);
            app_ctx_.tft_.drawString("UP " + (up_.length() ? up_ : String("--")) + pc_cfg_.net_unit_, 230, PC_NET_Y, 4);
        }
        up_last_ = up_;
    }
}
