//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "home_page.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ctime>
#include <cmath>

// ---- 布局坐标（240x240）----
#define HM_DIV1_Y   28
#define HM_T_X      26         // 温度数字 ML 起点
#define HM_H_X      150        // 湿度数字 ML 起点
#define HM_VAL_CY   60
#define HM_T_LBL_CX 60
#define HM_H_LBL_CX 180
#define HM_LBL_Y    92
#define HM_DIV2_Y   104
#define HM_PM_Y     125
#define HM_DIV3_Y   146
#define HM_CHIP_W   112
#define HM_CHIP_H   30
#define HM_CHIP_X0  6
#define HM_CHIP_X1  122
#define HM_CHIP_Y0  154
#define HM_CHIP_Y1  190
#define HM_CHIP_COL 0x18E3     // 胶囊底色

static uint16_t pmColor(const int v) {
    if (v < 0) return COL_LABEL;
    if (v <= 35) return COL_ONLINE; // 绿
    if (v <= 75) return 0xFE60; // 黄
    return COL_OFFLINE; // 红
}

static const char *pmWord(const int v) {
    if (v < 0) return "--";
    if (v <= 35) return "Good";
    if (v <= 75) return "Moderate";
    return "Poor";
}

// ================================ 生命周期 ================================
void HomePage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    last_clock_ms_ = 0;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    shown_dot_ = app_ctx_.wifi_up_;
    DrawClock();
    DrawBody();
}

void HomePage::Tick() {
    // 退避：连续失败时按 fail_streak_ 拉长重试间隔（最多 4×），HA 长期离线时几乎不再探测。
    const uint32_t interval = HOME_INTERVAL * (fail_streak_ < 4 ? (fail_streak_ + 1u) : 4u);
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_fetch_ms_ >= interval)) {
        need_fetch_ = false;
        last_fetch_ms_ = millis();
        FetchAll();
    }
}

void HomePage::Render() {
    DrawStatusDot();
    if (AppContext::TimeReady() && millis() - last_clock_ms_ >= 1000u) {
        last_clock_ms_ = millis();
        DrawClock();
    }
    if (dirty_) {
        dirty_ = false;
        DrawBody();
    }
}

// ================================ 数据（读 HA）================================
void HomePage::FetchAll() {
    // 先做一次短超时 TCP 探测：HA 不可达时立刻返回，避免下面 6 个 GET 各自阻塞一个超时
    // （最坏 6×2s）。保留上次显示，并累加退避计数交给 Tick 拉长下次重试。
    if (!HaReachable()) {
        if (fail_streak_ < 255) fail_streak_++;
        return;
    }
    fail_streak_ = 0;

    String s;
    if (HaState(home_cfg_.temp_eid_, s)) {
        temp_ = s.toFloat();
    }

    if (HaState(home_cfg_.humid_eid_, s)) {
        humid_ = s.toFloat();
    }

    if (HaState(home_cfg_.pm25_eid_, s)) {
        pm25_ = s.toInt();
    }

    if (HaState(home_cfg_.purifier_eid_, s)) {
        purifier_on_ = IsOn(s);
    }

    if (HaState(home_cfg_.light_eid_, s)) {
        light_on_ = IsOn(s);
    }

    if (HaState(home_cfg_.tv_eid_, s)) {
        tv_on_ = IsOn(s);
    }

    if (HaState(home_cfg_.other_eid_, s)) {
        other_on_ = IsOn(s);
    }

    valid_ = true;
    dirty_ = true;
}

bool HomePage::HaState(const char *eid, String &out) const {
    if (!eid || !eid[0] || !app_ctx_.wifi_up_) return false;
    const String url = String(home_cfg_.ha_url_) + "/api/states/" + eid;
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(2000);
    if (!http.begin(client, url)) return false;
    http.addHeader("Authorization", String("Bearer ") + home_cfg_.token_);
    const int status = http.GET();
    bool ok = false;
    if (status == 200) {
        // 只取 "state"，用过滤器避免 media_player 等大 attributes 撑爆内存
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument filter;
        filter["state"] = true;
        JsonDocument doc;
#else
        StaticJsonDocument<32> filter; filter["state"] = true;
        DynamicJsonDocument doc(192);
#endif
        const DeserializationError err = deserializeJson(doc, http.getStream(),
                                                         DeserializationOption::Filter(filter));
        if (!err) {
            out = doc["state"].as<String>();
            ok = true;
        }
    }
    http.end();
    return ok;
}

// 发正式请求前的可达性探测：解析 ha_url_ 的 host:port，用短超时 TCP connect 试连。
// HA 离线时 ~1.2s 内失败返回，从而避免 FetchAll 里 6 个 GET 各自吃满一个超时。
bool HomePage::HaReachable() const {
    if (!home_cfg_.ha_url_ || !home_cfg_.ha_url_[0] || !app_ctx_.wifi_up_) return false;
    String u(home_cfg_.ha_url_); // 例 "http://192.168.1.20:8123"
    const int p = u.indexOf("://");
    if (p >= 0) u = u.substring(p + 3); // 去 scheme
    const int slash = u.indexOf('/');
    if (slash >= 0) u = u.substring(0, slash); // 去路径，只留 host[:port]
    uint16_t port = 80; // 与 HTTPClient 解析 http:// URL 的默认端口一致
    String host = u;
    const int colon = u.indexOf(':');
    if (colon >= 0) {
        host = u.substring(0, colon);
        port = static_cast<uint16_t>(u.substring(colon + 1).toInt());
    }
    if (host.length() == 0) return false;
    WiFiClient probe;
    probe.setTimeout(1200); // 短探测超时（与 GET 的 connect 超时同一套机制）
    const bool ok = probe.connect(host.c_str(), port);
    probe.stop();
    return ok;
}

int HomePage::IsOn(const String &st) {
    if (st == "off" || st == "unavailable" || st == "unknown" ||
        st == "standby" || st == "idle" || st.length() == 0)
        return 0;
    return 1;
}

// ================================ 渲染 ================================
void HomePage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("Home", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, HM_DIV1_Y, 224, COL_DIV);
}

void HomePage::DrawClock() {
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

void HomePage::DrawStatusDot() {
    if (shown_dot_ == app_ctx_.wifi_up_) return;
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void HomePage::DrawBody() const {
    app_ctx_.tft_.fillRect(0, HM_DIV1_Y + 2, 240, 240 - (HM_DIV1_Y + 2), COL_BG);
    if (!valid_) {
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("loading...", 120, 120, 4);
        return;
    }

    // 温度（大字 + 度数圈）
    const String tstr = isnan(temp_) ? "--" : String(static_cast<int>(lroundf(temp_)));
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_TEMP);
    app_ctx_.tft_.drawString(tstr, HM_T_X, HM_VAL_CY, 6);
    if (!isnan(temp_)) {
        int tw = app_ctx_.tft_.textWidth(tstr, 6);
        app_ctx_.tft_.drawCircle(HM_T_X + tw + 8, HM_VAL_CY - 14, 5, COL_TEMP);
        app_ctx_.tft_.drawCircle(HM_T_X + tw + 8, HM_VAL_CY - 14, 4, COL_TEMP);
    }

    // 湿度（大字 + Font4 的 %）
    const String hstr = isnan(humid_) ? "--" : String(static_cast<int>(lroundf(humid_)));
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_DATE);
    app_ctx_.tft_.drawString(hstr, HM_H_X, HM_VAL_CY, 6);
    if (!isnan(humid_)) {
        const int hw = app_ctx_.tft_.textWidth(hstr, 6);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_DATE);
        app_ctx_.tft_.drawString("%", HM_H_X + hw + 4, HM_VAL_CY - 6, 4);
    }

    // 标签
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("TEMP", HM_T_LBL_CX, HM_LBL_Y, 2);
    app_ctx_.tft_.drawString("HUMID", HM_H_LBL_CX, HM_LBL_Y, 2);

    // PM2.5
    app_ctx_.tft_.drawFastHLine(8, HM_DIV2_Y, 224, COL_DIV);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("PM2.5", 12, HM_PM_Y, 2);
    char pbuf[12];
    snprintf(pbuf, sizeof(pbuf), "%d", pm25_ < 0 ? 0 : pm25_);
    app_ctx_.tft_.setTextColor(pmColor(pm25_));
    app_ctx_.tft_.drawString(pm25_ < 0 ? "--" : pbuf, 92, HM_PM_Y, 4);
    app_ctx_.tft_.setTextColor(pmColor(pm25_));
    app_ctx_.tft_.drawString(pmWord(pm25_), 150, HM_PM_Y, 2);

    // 设备胶囊
    app_ctx_.tft_.drawFastHLine(8, HM_DIV3_Y, 224, COL_DIV);
    DrawChip(HM_CHIP_X0, HM_CHIP_Y0, HM_CHIP_W, HM_CHIP_H, "Purifier", purifier_on_);
    DrawChip(HM_CHIP_X1, HM_CHIP_Y0, HM_CHIP_W, HM_CHIP_H, "Light", light_on_);
    DrawChip(HM_CHIP_X0, HM_CHIP_Y1, HM_CHIP_W, HM_CHIP_H, "TV", tv_on_);
    DrawChip(HM_CHIP_X1, HM_CHIP_Y1, HM_CHIP_W, HM_CHIP_H, "Other", other_on_);
}

void HomePage::DrawChip(const int x, const int y, const int w, const int h, const char *label, const int on) const {
    app_ctx_.tft_.fillRoundRect(x, y, w, h, 6, HM_CHIP_COL);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(label, x + 8, y + h / 2, 2);

    const char *s = (on == 1) ? "ON" : (on == 0) ? "OFF" : "--";
    const uint16_t c = (on == 1) ? COL_ONLINE : COL_LABEL;
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(c);
    app_ctx_.tft_.drawString(s, x + w - 10, y + h / 2, 2);
}
