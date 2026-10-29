//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "system_page.h"

#include <ESP8266WiFi.h>

// ---- 布局坐标（240x240）----
#define SY_DIV1      28
#define SY_WIFI_Y    44
#define SY_SSID_Y    62
#define SY_IP_Y      80
#define SY_DIV2      94
#define SY_HEAP_Y    106
#define SY_HEAP_BAR  118
#define SY_FLASH_Y   134
#define SY_FLASH_BAR 146
#define SY_DIV3      162
#define SY_UP_Y      174
#define SY_CPU_Y     192
#define SY_SPARK_X   10
#define SY_SPARK_TOP 206
#define SY_SPARK_W   220
#define SY_SPARK_H   28

// ================================ 生命周期 ================================
void SystemPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    ReadStats();
    DrawClock();
    DrawBody(true);
}

void SystemPage::Tick() {
    if (millis() - last_refresh_ms_ >= 1000) {
        // 每秒刷新显示
        last_refresh_ms_ = millis();
        ReadStats();
        dirty_ = true;
    }
}

void SystemPage::BackgroundTick() {
    if (millis() - last_sample_ms_ >= 1000) {
        // 每秒采一次空闲堆（始终运行）
        last_sample_ms_ = millis();
        heap_hist_[hist_head_] = static_cast<uint16_t>(EspClass::getFreeHeap() / 1024);
        hist_head_ = (hist_head_ + 1) % HB;
        if (hist_count_ < HB) {
            hist_count_++;
        }
    }
}

void SystemPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }
    DrawClock();
    if (dirty_) {
        dirty_ = false;
        DrawBody();
    }
}

// ================================ 采集 ================================
void SystemPage::ReadStats() {
    rssi_ = WiFi.RSSI();
    ssid_ = WiFi.SSID();
    ip_ = WiFi.localIP().toString();
    free_heap_ = EspClass::getFreeHeap();
    max_block_ = EspClass::getMaxFreeBlockSize();
    frag_ = EspClass::getHeapFragmentation();
    sketch_used_ = EspClass::getSketchSize();
    sketch_free_ = EspClass::getFreeSketchSpace();
    cpu_ = EspClass::getCpuFreqMHz();
    reset_ = EspClass::getResetReason();
    battery_v_ = app_ctx_.battery_v_; // 电池电压由 main.cpp 每秒写入共享上下文
}

int SystemPage::RssiToPct(const int8_t rssi) {
    int p = 2 * (rssi + 100); // -100dBm→0%  -50dBm→100%

    if (p < 0) {
        p = 0;
    }

    if (p > 100) {
        p = 100;
    }

    return p;
}

int SystemPage::RssiToLevel(const long rssi) {
    if (rssi >= -55) return 5;
    if (rssi >= -65) return 4;
    if (rssi >= -75) return 3;
    if (rssi >= -85) return 2;
    if (rssi >= -95) return 1;
    return 0;
}

// ================================ 渲染 ================================
// 原位文本替换：不整块清屏，只擦除/重画字符串占用的像素，避免闪烁。
// datum 为 ML_DATUM（左对齐）或 MR_DATUM（右对齐），x 为对齐基准、y 为垂直中心。
// 找出新旧串的公共前缀，仅重绘不同的后缀部分；右对齐时长度变化会使整串平移，
// 此时整串原位擦除后重画。
void SystemPage::ReplaceText(const String &oldS, const String &newS,
                             const int32_t x, const int32_t y, const uint8_t font,
                             const uint16_t fg, const uint8_t datum) const {
    if (oldS == newS) return;
    TFT_eSPI &tft = app_ctx_.tft_;
    int n = 0;
    if (datum != MR_DATUM || oldS.length() == newS.length()) {
        while (n < static_cast<int>(oldS.length()) &&
               n < static_cast<int>(newS.length()) && oldS[n] == newS[n]) {
            n++;
        }
    }
    if (n > 0) {
        // 只重绘变化的后缀：先按背景色精确擦掉旧后缀，再画新后缀
        const String ot = oldS.substring(n);
        const String nt = newS.substring(n);
        int32_t txo, txn;
        if (datum == MR_DATUM) {
            txo = x - tft.textWidth(ot, font);
            txn = x - tft.textWidth(nt, font);
        } else {
            txo = x + tft.textWidth(oldS.substring(0, n), font);
            txn = txo; // 前缀相同，宽度一致
        }
        tft.setTextDatum(ML_DATUM);
        if (ot.length()) {
            tft.setTextColor(COL_BG);
            tft.drawString(ot, txo, y, font);
        }
        if (nt.length()) {
            tft.setTextColor(fg);
            tft.drawString(nt, txn, y, font);
        }
    } else {
        tft.setTextDatum(datum);
        if (oldS.length()) {
            tft.setTextColor(COL_BG);
            tft.drawString(oldS, x, y, font);
        }
        if (newS.length()) {
            tft.setTextColor(fg);
            tft.drawString(newS, x, y, font);
        }
    }
}

void SystemPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("System", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, SY_DIV1, 224, COL_DIV);
}

void SystemPage::DrawClock() {
    char b[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(b, "--:--");
    }
    ReplaceText(shown_time_, b, 212, 11, 2, COL_TIME, MR_DATUM);
    shown_time_ = b;
}

void SystemPage::DrawStatusDot() {
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void SystemPage::DrawBar(const int x, const int y, const int w, const int h,
                         int pct, const uint16_t color,
                         const int oldPct, const uint16_t oldCol, const bool force) const {
    if (pct < 0) {
        pct = 0;
    }

    if (pct > 100) {
        pct = 100;
    }

    const int newW = (w - 2) * pct / 100;
    if (force) {
        // 进页面时整条画一次（此时整屏刚被清空，无闪烁）
        app_ctx_.tft_.drawRect(x, y, w, h, COL_DIV);
        app_ctx_.tft_.fillRect(x + 1, y + 1, newW, h - 2, color);
        return;
    }
    if (pct == oldPct && color == oldCol) return;
    const int oldW = (oldPct >= 0) ? (w - 2) * oldPct / 100 : -1;
    if (color == oldCol && oldW >= 0) {
        // 颜色没变：只重画增减的部分
        if (newW > oldW) {
            app_ctx_.tft_.fillRect(x + 1 + oldW, y + 1, newW - oldW, h - 2, color);
        } else if (newW < oldW) {
            app_ctx_.tft_.fillRect(x + 1 + newW, y + 1, oldW - newW, h - 2, COL_BG);
        }
    } else {
        // 颜色变化（或首次）：整条重画
        app_ctx_.tft_.fillRect(x + 1, y + 1, newW, h - 2, color);
        if (oldW >= 0) {
            if (newW < oldW) {
                app_ctx_.tft_.fillRect(x + 1 + newW, y + 1, oldW - newW, h - 2, COL_BG);
            }
        } else {
            app_ctx_.tft_.fillRect(x + 1 + newW, y + 1, (w - 2) - newW, h - 2, COL_BG);
        }
    }
}

void SystemPage::DrawSignal(const int x, const int y, const int level) {
    if (level == shown_level_) return; // 电平没变：一笔不画
    for (int i = 0; i < 5; i++) {
        const int bh = 3 + i * 2; // 递增高度
        const int bx = x + i * 6;
        const int by = y - bh;
        const bool was = (shown_level_ >= 0) && (i < shown_level_);
        const bool is = i < level;
        if (was != is) {
            app_ctx_.tft_.fillRect(bx, by, 4, bh, is ? COL_ONLINE : COL_DIV);
        }
    }
    shown_level_ = level;
}

void SystemPage::DrawBody(const bool force) {
    TFT_eSPI &tft = app_ctx_.tft_;
    if (force) {
        // 进页面：静态分隔线一次画好，原位缓存清零，随后走同一套增量逻辑全绘
        shown_wifi_ = shown_ssid_ = shown_ip_ = shown_heap_ = shown_frag_txt_ = "";
        shown_flash_txt_ = shown_flash_pct_ = shown_up_ = shown_cpu_ = "";
        shown_bat_ = "";
        shown_level_ = -1;
        shown_frag_ = shown_flash_ = -1;
        shown_frag_col_ = shown_flash_col_ = 0;
        prev_count_ = 0;
        tft.drawFastHLine(8, SY_DIV2, 224, COL_DIV);
        tft.drawFastHLine(8, SY_DIV3, 224, COL_DIV);
    }

    // ---- WiFi ----
    DrawSignal(10, SY_WIFI_Y, RssiToLevel(rssi_));

    char wbuf[24];
    snprintf(wbuf, sizeof(wbuf), "%lddBm  %d%%", static_cast<long>(rssi_), RssiToPct(rssi_));
    ReplaceText(shown_wifi_, wbuf, 46, SY_WIFI_Y - 6, 2, COL_TIME, ML_DATUM);
    shown_wifi_ = wbuf;

    ReplaceText(shown_ssid_, "SSID " + ssid_, 10, SY_SSID_Y, 2, COL_LABEL, ML_DATUM);
    shown_ssid_ = "SSID " + ssid_;
    ReplaceText(shown_ip_, "IP   " + ip_, 10, SY_IP_Y, 2, COL_LABEL, ML_DATUM);
    shown_ip_ = "IP   " + ip_;

    // ---- Heap ----
    char hbuf[32];
    snprintf(hbuf, sizeof(hbuf), "HEAP %.1fK free", free_heap_ / 1024.0);
    ReplaceText(shown_heap_, hbuf, 10, SY_HEAP_Y, 2, COL_TIME, ML_DATUM);
    shown_heap_ = hbuf;
    char fbuf[16];
    snprintf(fbuf, sizeof(fbuf), "frag %d%%", frag_);
    const uint16_t fragCol = (frag_ < 25) ? COL_ONLINE : (frag_ < 50) ? 0xFE60 : COL_OFFLINE;
    ReplaceText(shown_frag_txt_, fbuf, 230, SY_HEAP_Y, 2, fragCol, MR_DATUM);
    shown_frag_txt_ = fbuf;
    DrawBar(10, SY_HEAP_BAR, 220, 7, frag_, fragCol, shown_frag_, shown_frag_col_, force); // 碎片率越低越好
    shown_frag_ = frag_;
    shown_frag_col_ = fragCol;

    // ---- Flash(app) ----
    const uint32_t total = sketch_used_ + sketch_free_;
    const int flashPct = total ? static_cast<int>(static_cast<uint64_t>(sketch_used_) * 100 / total) : 0;
    char flbuf[32];
    snprintf(flbuf, sizeof(flbuf), "FLASH %uK / %uK",
             static_cast<unsigned>(sketch_used_ / 1024), static_cast<unsigned>(total / 1024));
    ReplaceText(shown_flash_txt_, flbuf, 10, SY_FLASH_Y, 2, COL_TIME, ML_DATUM);
    shown_flash_txt_ = flbuf;
    char flp[8];
    snprintf(flp, sizeof(flp), "%d%%", flashPct);
    ReplaceText(shown_flash_pct_, flp, 230, SY_FLASH_Y, 2, COL_LABEL, MR_DATUM);
    shown_flash_pct_ = flp;
    DrawBar(10, SY_FLASH_BAR, 220, 7, flashPct, COL_DATE, shown_flash_, shown_flash_col_, force);
    shown_flash_ = flashPct;
    shown_flash_col_ = COL_DATE;

    // ---- 运行时间 / CPU / 复位原因 ----
    const uint32_t s = millis() / 1000;
    const uint32_t d = s / 86400;
    const uint32_t h = (s % 86400) / 3600;
    const uint32_t sec = s % 60;
    const uint32_t m = (s % 3600) / 60;
    char ubuf[32];
    if (d > 0) {
        snprintf(ubuf, sizeof(ubuf), "UP %lud %02lu:%02lu:%02lu",
                 static_cast<unsigned long>(d), static_cast<unsigned long>(h),
                 static_cast<unsigned long>(m), static_cast<unsigned long>(sec));
    } else {
        snprintf(ubuf, sizeof(ubuf), "UP %02lu:%02lu:%02lu",
                 static_cast<unsigned long>(h),
                 static_cast<unsigned long>(m),
                 static_cast<unsigned long>(sec));
    }
    ReplaceText(shown_up_, ubuf, 10, SY_UP_Y, 2, COL_TIME, ML_DATUM);
    shown_up_ = ubuf;

    // ---- 电池电压（显示在运行时间行右侧的空白处，不改动既有布局）----
    // 只显示 "x.xxV"（不加前缀）：运行时间在多天后会变长，紧凑格式可保证右对齐不与其重叠。
    char btbuf[16];
    snprintf(btbuf, sizeof(btbuf), "%.2fV", battery_v_);
    ReplaceText(shown_bat_, btbuf, 230, SY_UP_Y, 2, COL_DATE, MR_DATUM);
    shown_bat_ = btbuf;

    char cbuf[40];
    snprintf(cbuf, sizeof(cbuf), "CPU %dMHz   Rst %s", cpu_, reset_.c_str());
    ReplaceText(shown_cpu_, cbuf, 10, SY_CPU_Y, 2, COL_LABEL, ML_DATUM);
    shown_cpu_ = cbuf;

    // ---- 空闲堆趋势 ----
    DrawSparkline(force);
}

void SystemPage::DrawSparkline(const bool force) {
    constexpr int x = SY_SPARK_X;
    constexpr int w = SY_SPARK_W;
    constexpr int h = SY_SPARK_H;
    constexpr int y = SY_SPARK_TOP;

    TFT_eSPI &tft = app_ctx_.tft_;
    if (force) {
        // 进页面时整屏刚被清空，只需补画底部的基线
        tft.drawFastHLine(x, y + h, w, COL_DIV);
        prev_count_ = 0;
    }

    // 先按背景色精确擦掉上一条折线（不清整块区域），再画新线
    if (prev_count_ >= 2) {
        for (int i = 1; i < prev_count_; i++) {
            const int px0 = x + (w - 1) * (i - 1) / (prev_count_ - 1);
            const int px1 = x + (w - 1) * i / (prev_count_ - 1);
            tft.drawLine(px0, prev_y_[i - 1], px1, prev_y_[i], COL_BG);
        }
    }

    if (hist_count_ < 2) {
        prev_count_ = 0;
        return;
    }

    uint16_t mn = 65535, mx = 0;
    for (int i = 0; i < hist_count_; i++) {
        const uint16_t v = heap_hist_[i];
        if (v < mn) {
            mn = v;
        }

        if (v > mx) {
            mx = v;
        }
    }

    if (mx == mn) {
        mx = mn + 1;
    }

    const int start = (hist_count_ < HB) ? 0 : hist_head_; // 最旧样本
    int prevX = -1, prevY = -1;
    for (int i = 0; i < hist_count_; i++) {
        const int idx = (start + i) % HB;
        const uint16_t v = heap_hist_[idx];
        const int px = x + (w - 1) * i / (hist_count_ - 1);
        const int py = y + h - 1 - static_cast<int>(static_cast<long>(v - mn) * (h - 2) / (mx - mn));

        prev_y_[i] = py;
        if (prevX >= 0) {
            tft.drawLine(prevX, prevY, px, py, COL_DATE);
        }

        prevX = px;
        prevY = py;
    }
    prev_count_ = hist_count_;
}
