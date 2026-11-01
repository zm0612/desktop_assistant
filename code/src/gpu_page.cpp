//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "gpu_page.h"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ctime>

// ---- 布局坐标（240x240）----
#define GP_DIV1     28
#define GP_NAME_Y   42
#define GP_GAUGE_CX 120
#define GP_GAUGE_CY 108
#define GP_GAUGE_R  50
#define GP_DIV2     162
#define GP_VRAM_Y   174
#define GP_VRAM_BAR 188
#define GP_DIV3     202
#define GP_TP_Y     225

static uint16_t Lerp565(const uint16_t a, const uint16_t b, const int t) {
    const int ar = (a >> 11) & 0x1F;
    const int ag = (a >> 5) & 0x3F;
    const int ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F;
    const int bg = (b >> 5) & 0x3F;
    const int bb = b & 0x1F;
    const int r = ar + (br - ar) * t / 100;
    const int g = ag + (bg - ag) * t / 100;
    const int bl = ab + (bb - ab) * t / 100;

    return (r << 11) | (g << 5) | bl;
}

uint16_t GpuPage::HeatColor(const int pct) {
    if (pct < 0) {
        return COL_LABEL;
    }

    if (pct <= 60) {
        return Lerp565(0x07E0, 0xFE60, pct * 100 / 60);
    }

    return Lerp565(0xFE60, 0xF800, (pct - 60) * 100 / 40);
}

// ================================ 生命周期 ================================
void GpuPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    shown_online_ = -1;
    last_clock_ms_ = 0;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);

    DrawClock();

    shown_valid_ = -1; // 强制 drawBody 整区重绘一次（进页时屏已清空，重绘不闪）
    DrawBody();
}

void GpuPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }

    DrawClock();
    const bool on = valid_ && millis() - last_recv_ms_ < STALE_MS;
    if (shown_online_ != static_cast<int>(on)) {
        dirty_ = true;
    }

    if (dirty_) {
        dirty_ = false;
        shown_online_ = on;
        online_ = on;
        DrawBody();
    }
}

// ================================ 接收 ================================
// 由 WebPortal 的 /gpu 路由调用，传入共用的 80 端口 server（Task 5）
void GpuPage::HandlePost(ESP8266WebServer &s) {
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
    if (!doc["util"].isNull()) {
        util_ = doc["util"].as<int>();
    }

    if (!doc["memUsed"].isNull()) {
        mem_used_ = doc["memUsed"].as<long>();
    }

    if (!doc["memTotal"].isNull()) {
        mem_total_ = doc["memTotal"].as<long>();
    }

    if (!doc["temp"].isNull()) {
        temp_ = doc["temp"].as<int>();
    }

    if (!doc["power"].isNull()) {
        power_ = doc["power"].as<int>();
    }

    if (!doc["name"].isNull()) {
        name_ = doc["name"].as<String>();
    }

    valid_ = true;
    dirty_ = true;
    last_recv_ms_ = millis();
    s.send(200, "text/plain", "ok");
}

// ================================ 渲染 ================================
void GpuPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("GPU", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, GP_DIV1, 224, COL_DIV);
}

void GpuPage::DrawClock() {
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

// 整区 fillRect 清屏只在「进页 / 等待态<->数据态切换」发生（每次仅一次，不构成持续闪烁）；
// 平时的数据刷新由 render()->drawBody() 分发到各元素做局部原位重画。
void GpuPage::DrawBody() {
    if (shown_valid_ != static_cast<int>(valid_)) {
        shown_valid_ = valid_;
        app_ctx_.tft_.fillRect(0, GP_DIV1 + 2, 240, 240 - (GP_DIV1 + 2), COL_BG);

        if (!valid_) {
            app_ctx_.tft_.setTextDatum(MC_DATUM);
            app_ctx_.tft_.setTextColor(COL_LABEL);
            app_ctx_.tft_.drawString("Waiting for GPU...", 120, 100, 4);
            app_ctx_.tft_.setTextColor(COL_DIV);
            const String url = "http://" + WiFi.localIP().toString() + "/gpu";
            app_ctx_.tft_.drawString("POST to", 120, 134, 2);
            app_ctx_.tft_.setTextColor(COL_DATE);
            app_ctx_.tft_.drawString(url, 120, 152, 2);
            return;
        }

        // 静态元素（分隔线 / VRAM 标签）只画一次
        app_ctx_.tft_.drawFastHLine(8, GP_DIV2, 224, COL_DIV);
        app_ctx_.tft_.drawFastHLine(8, GP_DIV3, 224, COL_DIV);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("VRAM", 12, GP_VRAM_Y, 2);

        // 重置缓存，让下面的逐项补画把数据区画全
        shown_util_ = shown_temp_ = shown_power_ = -2;
        shown_mem_used_ = shown_mem_total_ = -2;
        shown_name_ = "";
        shown_util_w_ = shown_vram_w_ = shown_temp_w_ = shown_power_w_ = 0;
    }

    if (!valid_) {
        return;
    }

    DrawNameRow();
    DrawGauge();
    DrawVram();
    DrawTempPower();
}

void GpuPage::DrawNameRow() {
    const String nm = online_ ? (name_.length() ? name_ : String("GPU")) : String("offline");

    if (nm == shown_name_) {
        return;
    }

    shown_name_ = nm;
    app_ctx_.tft_.fillRect(0, GP_NAME_Y - 8, 240, 16, COL_BG); // 名字独占一行，整行清底再写
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(online_ ? COL_LABEL : COL_OFFLINE);
    app_ctx_.tft_.drawString(nm, 120, GP_NAME_Y, 2);
}

void GpuPage::DrawGauge() {
    if (util_ == shown_util_) {
        return;
    }

    shown_util_ = util_;
    const int u = util_ < 0 ? 0 : (util_ > 100 ? 100 : util_);
    char ub[8];
    snprintf(ub, sizeof(ub), "%d%%", u);
    const char *txt = util_ >= 0 ? ub : "--";
    const int nw = app_ctx_.tft_.textWidth(txt, 6);
    const int w = nw > shown_util_w_ ? nw : shown_util_w_;
    shown_util_w_ = w;
    // 数字与圆环局部重叠：先擦旧数字（连带擦掉环的一段），再重铺环恢复，最后写新数字，
    // 层次与整绘时一致（环在下、字在上），且无需整区清屏
    app_ctx_.tft_.fillRect(GP_GAUGE_CX - w / 2, GP_GAUGE_CY - 30, w, 48, COL_BG);
    app_ctx_.tft_.drawSmoothArc(GP_GAUGE_CX, GP_GAUGE_CY, GP_GAUGE_R, GP_GAUGE_R - 10, 30, 330, COL_DIV, COL_BG);
    if (util_ >= 0) {
        const int end = 30 + (330 - 30) * u / 100;
        if (end > 31) {
            app_ctx_.tft_.drawSmoothArc(GP_GAUGE_CX, GP_GAUGE_CY, GP_GAUGE_R,
                                        GP_GAUGE_R - 10, 30, end, HeatColor(util_), COL_BG);
        }
    }

    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(util_ >= 0 ? COL_TIME : COL_LABEL);
    app_ctx_.tft_.drawString(txt, GP_GAUGE_CX + 6, GP_GAUGE_CY, 6);
    // 标签两角压在环上，随环一起重画，保持「标签在环上」的原层次
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("GPU LOAD", GP_GAUGE_CX, GP_GAUGE_CY + 30, 2);
}

void GpuPage::DrawVram() {
    if (mem_used_ == shown_mem_used_ && mem_total_ == shown_mem_total_) {
        return;
    }

    shown_mem_used_ = mem_used_;
    shown_mem_total_ = mem_total_;

    if (mem_total_ > 0) {
        char mb[24];
        snprintf(mb, sizeof(mb), "%.1f/%.1f G", mem_used_ / 1024.0, mem_total_ / 1024.0);
        const int nw = app_ctx_.tft_.textWidth(mb, 2);
        const int w = nw > shown_vram_w_ ? nw : shown_vram_w_;
        shown_vram_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 1, GP_VRAM_Y - 8, w + 2, 16, COL_BG); // 右对齐：按新旧最大宽度清
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(COL_TIME);
        app_ctx_.tft_.drawString(mb, 230, GP_VRAM_Y, 2);
        // 进度条：清内部再按比例填（边框重画幂等）
        const int pct = static_cast<int>(static_cast<long long>(mem_used_) * 100 / mem_total_);
        app_ctx_.tft_.drawRoundRect(12, GP_VRAM_BAR, 216, 8, 2, COL_DIV);
        app_ctx_.tft_.fillRect(13, GP_VRAM_BAR + 1, 214, 6, COL_BG);
        const int fw = 214 * (pct > 100 ? 100 : pct) / 100;
        app_ctx_.tft_.fillRect(13, GP_VRAM_BAR + 1, fw, 6, COL_DATE);
    } else {
        // 显存总量丢失（异常）：清掉已画的文本与进度条
        app_ctx_.tft_.fillRect(230 - shown_vram_w_ - 1, GP_VRAM_Y - 8, shown_vram_w_ + 2, 16, COL_BG);
        app_ctx_.tft_.fillRect(12, GP_VRAM_BAR - 1, 216, 10, COL_BG);
        shown_vram_w_ = 0;
    }
}

void GpuPage::DrawTempPower() {
    if (temp_ != shown_temp_) {
        shown_temp_ = temp_;
        char tb[16];
        snprintf(tb, sizeof(tb), "TEMP %d", temp_ < 0 ? 0 : temp_);
        const char *txt = temp_ >= 0 ? tb : "TEMP --";
        const int nw = app_ctx_.tft_.textWidth(txt, 4);
        const int w = nw > shown_temp_w_ ? nw : shown_temp_w_;
        shown_temp_w_ = w;
        app_ctx_.tft_.fillRect(10, GP_TP_Y - 13, w + 4, 26, COL_BG); // 左对齐：只清新旧最大宽度
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(temp_ >= 0 ? HeatColor(temp_ <= 0 ? 0 : (temp_ - 30) * 100 / 60) : COL_LABEL);
        app_ctx_.tft_.drawString(txt, 12, GP_TP_Y, 4);
    }

    if (power_ != shown_power_) {
        shown_power_ = power_;
        char pb[16];
        snprintf(pb, sizeof(pb), "%dW", power_ < 0 ? 0 : power_);
        const char *txt = power_ >= 0 ? pb : "--W";
        const int nw = app_ctx_.tft_.textWidth(txt, 4);
        const int w = nw > shown_power_w_ ? nw : shown_power_w_;
        shown_power_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 2, GP_TP_Y - 13, w + 4, 26, COL_BG);
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(0xFD20);
        app_ctx_.tft_.drawString(txt, 230, GP_TP_Y, 4);
    }
}
