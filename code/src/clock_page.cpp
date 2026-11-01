//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "clock_page.h"
#include <ctime>

#define CX 120
#define CY 120
#define R0 118          // 秒环外半径
#define R1 111          // 秒环内半径
#define RINGC 0x07FF    // 秒环颜色(青)
#define TIME_CY 102     // 时:分中心
#define DATE_CY 154     // 日期中心

void ClockPage::DrawDot() const {
    app_ctx_.tft_.fillCircle(228, 12, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void ClockPage::OnEnter() {
    shown_hm_ = "";
    shown_date_ = "";
    shown_sec_ = -1;
    shown_dot_ = app_ctx_.wifi_up_;
    DrawDot();
    app_ctx_.tft_.drawSmoothArc(CX, CY, R0, R1, 0, 360, COL_DIV, COL_BG); // 底环(只此一次)

    DrawTime(true);

    DrawDate(true);

    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t{};
        localtime_r(&n, &t);
        DrawSecondsRing(t.tm_sec, 0); // 底环已铺，只画进度
        shown_sec_ = t.tm_sec;
    }
}

void ClockPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        DrawDot();
    }
    if (!AppContext::TimeReady()) {
        DrawTime(false);
        return;
    }
    const time_t n = time(nullptr);
    tm t{};
    localtime_r(&n, &t);
    if (t.tm_sec != shown_sec_) {
        const int prev = shown_sec_;
        shown_sec_ = t.tm_sec;
        DrawSecondsRing(t.tm_sec, prev);
        DrawTime(false);
        DrawDate(false);
    }
}

void ClockPage::DrawTime(const bool force) {
    char buf[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t{};
        localtime_r(&n, &t);
        sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(buf, "--:--");
    }

    const String s(buf);
    if (!force && s == shown_hm_) {
        return;
    }

    shown_hm_ = s;
    app_ctx_.tft_.fillRect(24, TIME_CY - 28, 192, 56, COL_BG);
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, CX, TIME_CY, 7); // Font7 七段大字
}

void ClockPage::DrawDate(const bool force) {
    static const char *WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    String s;
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t{};
        localtime_r(&n, &t);
        char b[24];
        sprintf(b, "%s  %d %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
        s = b;
    }

    if (!force && s == shown_date_) {
        return;
    }

    shown_date_ = s;
    app_ctx_.tft_.fillRect(20, DATE_CY - 14, 200, 28, COL_BG);

    if (s.length()) {
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_DATE);
        app_ctx_.tft_.drawString(s, CX, DATE_CY, 4);
    }
}

// 秒进度环：只在新的一分钟才重铺底环；平时只同色重画进度弧 -> 不闪
void ClockPage::DrawSecondsRing(const int sec, const int prevSec) const {
    if (prevSec < 0 || sec < prevSec) {
        // 首次 / 回绕(新一分钟)
        app_ctx_.tft_.drawSmoothArc(CX, CY, R0, R1, 0, 360, COL_DIV, COL_BG);
    }

    if (sec <= 0) {
        return;
    }

    const float a1 = 180.0f + static_cast<float>(sec) / 60.0f * 360.0f; // 180° = 12 点

    if (a1 <= 360.0f) {
        app_ctx_.tft_.drawSmoothArc(CX, CY, R0, R1, 180, static_cast<int>(a1), RINGC, COL_BG);
    } else {
        app_ctx_.tft_.drawSmoothArc(CX, CY, R0, R1, 180, 360, RINGC, COL_BG);
        if (static_cast<int>(a1 - 360.0f) >= 1) {
            app_ctx_.tft_.drawSmoothArc(CX, CY, R0, R1, 0, static_cast<int>(a1 - 360.0f), RINGC, COL_BG);
        }
    }
}
