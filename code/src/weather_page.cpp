//
// Created by Zhang Zhimeng on 2026/8/23.
//
#include "weather_page.h"
#include <ArduinoJson.h>
#include <ctime>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

// ---- 布局坐标（240x240）----
#define CLOCK_CX 120
#define CLOCK_CY 58
#define DATE_CX  120
#define DATE_CY  102
#define DIV1_Y   122
#define CUR_ICON_CX 34
#define CUR_ICON_CY 150
#define CUR_ICON_R  17
#define CUR_TEMP_X  80
#define CUR_TEMP_CY 155
#define CUR_COND_X  155
#define DIV2_Y   176
#define FC_TOP     180
#define FC_H       58
#define FC_LABEL_Y 190
#define FC_ICON_Y  208
#define FC_ICON_R  12
#define FC_TEMP_Y  231
#define COL_HILITE 0x2945       // 今天列高亮底色

// ================================ 生命周期 ================================
void WeatherPage::OnEnter() {
    app_ctx_.tft_.drawFastHLine(16, DIV1_Y, 208, COL_DIV);
    shown_time_ = "";
    shown_date_ = "";
    last_clock_ms_ = 0;
    app_ctx_.tft_.fillCircle(228, 12, 5, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    shown_dot_ = app_ctx_.wifi_up_;
    DrawClock(true);
    DrawDate(true);
    DrawWeather();
}

void WeatherPage::Tick() {
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_fetch_ms_ >= WEATHER_INTERVAL)) {
        need_fetch_ = false;
        last_fetch_ms_ = millis();
        FetchWeather();
        FetchForecast();
    }
}

void WeatherPage::Render() {
    DrawStatusDot();
    if (AppContext::TimeReady() && millis() - last_clock_ms_ >= 1000) {
        last_clock_ms_ = millis();
        DrawClock();
        DrawDate();
    }
    if (weather_dirty_ || fc_dirty_) {
        weather_dirty_ = false;
        fc_dirty_ = false;
        DrawWeather();
    }
}

// ================================ 数据 ================================
bool WeatherPage::FetchWeather() {
    if (!app_ctx_.wifi_up_) {
        return false;
    }

    const String url = String("http://api.seniverse.com/v3/weather/now.json?key=")
                       + api_key_ + "&location=" + loc_ + "&language=en&unit=c";
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    bool ok = false;
    if (status == 200) {
        String payload = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(2048);
#endif
        if (!deserializeJson(doc, payload)) {
            JsonVariant now = doc["results"][0]["now"];
            if (!now.isNull()) {
                data_.city = doc["results"][0]["location"]["name"].as<String>();
                data_.code = now["code"].as<int>();
                data_.temp = now["temperature"].as<float>();
                data_.valid = true;
                weather_dirty_ = true;
                ok = true;
            }
        }
    }
    http.end();
    return ok;
}

bool WeatherPage::FetchForecast() {
    if (!app_ctx_.wifi_up_) {
        return false;
    }

    const String url = String("http://api.seniverse.com/v3/weather/daily.json?key=")
                       + api_key_ + "&location=" + loc_ + "&language=en&unit=c&start=0&days=3";
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    bool ok = false;
    if (status == 200) {
        String payload = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(4096);
#endif
        if (!deserializeJson(doc, payload)) {
            const auto daily = doc["results"][0]["daily"].as<JsonArray>();
            if (!daily.isNull() && daily.size() >= 1) {
                int n = static_cast<int>(daily.size());
                if (n > 3) {
                    n = 3;
                }

                for (int i = 0; i < n; i++) {
                    days_[i].code_ = daily[i]["code_day"].as<int>();
                    days_[i].high_ = daily[i]["high"].as<int>();
                    days_[i].low_ = daily[i]["low"].as<int>();
                }

                SetDayLabels();
                fc_valid_ = true;
                fc_dirty_ = true;
                ok = true;
            }
        }
    }
    http.end();
    return ok;
}

void WeatherPage::SetDayLabels() {
    static const char *WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    int wday = 0;
    bool have = false;

    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        wday = t.tm_wday;
        have = true;
    }

    strcpy(days_[0].label_, "Today");
    for (int i = 1; i < 3; i++) {
        if (have) strcpy(days_[i].label_, WD[(wday + i) % 7]);
        else snprintf(days_[i].label_, sizeof(days_[i].label_), "D+%d", i);
    }
}

// ================================ 渲染 ================================
void WeatherPage::DrawClock(const bool force) {
    char buf[10];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        sprintf(buf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        strcpy(buf, "--:--:--");
    }

    const String s(buf);
    if (!force && s == shown_time_) {
        return;
    }

    shown_time_ = s;
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME, COL_BG);
    app_ctx_.tft_.drawString(s, CLOCK_CX, CLOCK_CY, 6);
}

void WeatherPage::DrawDate(const bool force) {
    static const char *WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    String s;

    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        char b[24];
        sprintf(b, "%s  %d %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
        s = b;
    }

    if (!force && s == shown_date_) {
        return;
    }

    shown_date_ = s;
    if (s.length()) {
        DrawCentered(s, DATE_CX, DATE_CY, 4, COL_DATE);
    }
}

void WeatherPage::DrawWeather() const {
    app_ctx_.tft_.fillRect(0, 0, 180, 22, COL_BG);
    if (data_.city.length()) {
        app_ctx_.tft_.setTextDatum(TL_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString(data_.city, 8, 4, 2);
    }

    app_ctx_.tft_.fillRect(0, DIV1_Y + 2, 240, 240 - (DIV1_Y + 2), COL_BG);
    if (!data_.valid) {
        DrawCentered("-- Waiting --", 120, 150, 4, COL_LABEL);
        return;
    }

    DrawCurrentRow();

    if (fc_valid_) {
        DrawForecastStrip();
    }
}

void WeatherPage::DrawCurrentRow() const {
    const WeatherKind kind = Classify(data_.code);
    DrawIcon(kind, CUR_ICON_CX, CUR_ICON_CY, CUR_ICON_R);

    const String ts(static_cast<int>(lroundf(data_.temp)));
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_TEMP, COL_BG);
    app_ctx_.tft_.drawString(ts, CUR_TEMP_X, CUR_TEMP_CY, 6);
    const int tw = app_ctx_.tft_.textWidth(ts, 6);
    app_ctx_.tft_.drawCircle(CUR_TEMP_X + tw + 8, CUR_TEMP_CY - 14, 5, COL_TEMP);
    app_ctx_.tft_.drawCircle(CUR_TEMP_X + tw + 8, CUR_TEMP_CY - 14, 4, COL_TEMP);

    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(kindColor(kind));
    app_ctx_.tft_.drawString(KindName(kind), CUR_COND_X, CUR_TEMP_CY, 2);
}

void WeatherPage::DrawForecastStrip() const {
    app_ctx_.tft_.drawFastHLine(10, DIV2_Y, 220, COL_DIV);
    constexpr int cx[3] = {40, 120, 200};
    for (int i = 0; i < 3; i++) {
        if (i == 0) app_ctx_.tft_.fillRoundRect(cx[0] - 38, FC_TOP, 76, FC_H, 5, COL_HILITE);

        const uint16_t fg = (i == 0) ? COL_TIME : COL_LABEL;
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(fg);
        app_ctx_.tft_.drawString(days_[i].label_, cx[i], FC_LABEL_Y, 2);

        DrawIcon(Classify(days_[i].code_), cx[i], FC_ICON_Y, FC_ICON_R);

        char buf[12];
        snprintf(buf, sizeof(buf), "%d/%d", days_[i].high_, days_[i].low_);
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(fg);
        app_ctx_.tft_.drawString(buf, cx[i], FC_TEMP_Y, 2);
    }
}

void WeatherPage::DrawStatusDot() {
    if (shown_dot_ == app_ctx_.wifi_up_) {
        return;
    }

    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(228, 12, 5, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void WeatherPage::DrawCentered(const String &s, const int cx, const int y, uint8_t font, const uint16_t fg) const {
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    app_ctx_.tft_.setTextColor(fg, COL_BG);
    app_ctx_.tft_.drawString(s, cx, y, font);
}

// ================================ 天气图标 ================================
void WeatherPage::DrawIcon(const WeatherKind kind, const int cx, const int cy, const int r) const {
    constexpr uint16_t sun = 0xFD20;
    constexpr uint16_t cloud = 0xC618;
    constexpr uint16_t gray = 0x9CD3;
    constexpr uint16_t blue = 0x5D9F;
    constexpr uint16_t bolt = 0xFFE0;
    constexpr uint16_t snow = 0xFFFF;

    switch (kind) {
        case WeatherKind::Sun:
            DrawSun(cx, cy, r, sun);
            break;
        case WeatherKind::Hot:
            DrawSun(cx, cy, r, 0xF980);
            break;
        case WeatherKind::Partly:
            DrawSun(cx - r / 3, cy - r / 3, r * 3 / 5, sun);
            DrawCloud(cx + r / 4, cy + r / 4, r * 4 / 5, cloud);
            break;
        case WeatherKind::Cloud:
            DrawCloud(cx, cy, r, cloud);
            break;
        case WeatherKind::Overcast:
            DrawCloud(cx - 4, cy - 3, r, gray);
            DrawCloud(cx + 6, cy + 4, r * 4 / 5, cloud);
            break;
        case WeatherKind::Rain:
            DrawCloud(cx, cy - (r >= 18 ? 4 : 0), r, r >= 18 ? cloud : blue);
            if (r >= 18) DrawRainLines(cx, cy, r, blue);
            break;
        case WeatherKind::Thunder:
            DrawCloud(cx, cy - (r >= 18 ? 4 : 0), r, gray);
            if (r >= 18) DrawBolt(cx, cy, r, bolt);
            break;
        case WeatherKind::Snow:
            DrawCloud(cx, cy - (r >= 18 ? 4 : 0), r, r >= 18 ? cloud : snow);
            if (r >= 18) DrawFlakes(cx, cy, r, snow);
            break;
        case WeatherKind::Cold:
            DrawFlakes(cx, cy, r + 6, 0x9DFF);
            break;
        case WeatherKind::Fog:
            DrawLines(cx, cy, r, gray, 4);
            break;
        case WeatherKind::Wind:
            DrawLines(cx, cy, r, cloud, 3);
            break;
        default:
            DrawCloud(cx, cy, r, gray);
            DrawCentered("?", cx, cy, 4, COL_BG);
            break;
    }
}

void WeatherPage::DrawSun(const int cx, const int cy, const int r, const uint16_t color) const {
    const int rd = r * 3 / 5;
    app_ctx_.tft_.fillCircle(cx, cy, rd, color);
    for (int i = 0; i < 8; i++) {
        const float a = static_cast<float>(i) * static_cast<float>(PI) / 4.0f;
        const int x0 = cx + static_cast<int>(cosf(a) * static_cast<float>(rd + 3));
        const int y0 = cy + static_cast<int>(sinf(a) * static_cast<float>(rd + 3));
        const int x1 = cx + static_cast<int>(cosf(a) * static_cast<float>(r + 3));
        const int y1 = cy + static_cast<int>(sinf(a) * static_cast<float>(r + 3));
        app_ctx_.tft_.drawLine(x0, y0, x1, y1, color);
        app_ctx_.tft_.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    }
}

void WeatherPage::DrawCloud(const int cx, const int cy, const int r, const uint16_t body) const {
    app_ctx_.tft_.fillRoundRect(cx - r + 2, cy, 2 * r - 4, r * 55 / 100, 5, body);
    app_ctx_.tft_.fillCircle(cx - r / 2, cy + 2, r / 2, body);
    app_ctx_.tft_.fillCircle(cx + r / 2, cy + 2, r * 55 / 100, body);
    app_ctx_.tft_.fillCircle(cx, cy - r * 35 / 100, r * 3 / 5, body);
}

void WeatherPage::DrawRainLines(const int cx, const int cy, const int r, const uint16_t color) const {
    const int y0 = cy + r * 55 / 100 + 6;
    for (int i = -1; i <= 1; i++) {
        const int x = cx + i * 12;
        app_ctx_.tft_.drawLine(x, y0, x - 4, y0 + 12, color);
        app_ctx_.tft_.drawLine(x + 1, y0, x - 3, y0 + 12, color);
    }
}

void WeatherPage::DrawBolt(const int cx, const int cy, const int r, const uint16_t color) const {
    const int y0 = cy + r * 55 / 100 + 2;
    app_ctx_.tft_.fillTriangle(cx + 3, y0, cx - 7, y0 + 13, cx + 3, y0 + 13, color);
    app_ctx_.tft_.fillTriangle(cx - 3, y0 + 9, cx + 7, y0 + 9, cx - 3, y0 + 24, color);
}

void WeatherPage::DrawFlakes(const int cx, const int cy, const int r, const uint16_t color) const {
    const int y0 = cy + r * 55 / 100 + 8;
    const int xs[3] = {cx - 12, cx, cx + 12};
    for (int i = 0; i < 3; i++) {
        const int px = xs[i];
        const int py = y0 + (i == 1 ? 6 : 0);
        app_ctx_.tft_.drawLine(px - 4, py, px + 4, py, color);
        app_ctx_.tft_.drawLine(px, py - 4, px, py + 4, color);
        app_ctx_.tft_.drawLine(px - 3, py - 3, px + 3, py + 3, color);
        app_ctx_.tft_.drawLine(px - 3, py + 3, px + 3, py - 3, color);
    }
}

void WeatherPage::DrawLines(const int cx, const int cy, const int r, const uint16_t color, const int n) const {
    for (int i = 0; i < n; i++) {
        const int y = cy - n / 2 * 9 + i * 9;
        const int len = r * 8 / 5 - i % 2 * 10;
        app_ctx_.tft_.drawFastHLine(cx - len / 2, y, len, color);
        app_ctx_.tft_.drawFastHLine(cx - len / 2, y + 1, len, color);
    }
}

// ================================ code 归类 ================================
WeatherKind WeatherPage::Classify(int c) {
    switch (c) {
        case 0:
        case 1:
        case 2:
        case 3: return WeatherKind::Sun;
        case 4: return WeatherKind::Cloud;
        case 5:
        case 6: return WeatherKind::Partly;
        case 7:
        case 8: return WeatherKind::Cloud;
        case 9: return WeatherKind::Overcast;
        case 11:
        case 12: return WeatherKind::Thunder;
        case 10:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19: return WeatherKind::Rain;
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25: return WeatherKind::Snow;
        case 26:
        case 27:
        case 28:
        case 29: return WeatherKind::Wind;
        case 30:
        case 31: return WeatherKind::Fog;
        case 32:
        case 33:
        case 34:
        case 35:
        case 36: return WeatherKind::Wind;
        case 37: return WeatherKind::Cold;
        case 38: return WeatherKind::Hot;
        default: return WeatherKind::Unknown;
    }
}

const char *WeatherPage::KindName(WeatherKind k) {
    switch (k) {
        case WeatherKind::Sun: return "Sunny";
        case WeatherKind::Partly: return "Partly Cloudy";
        case WeatherKind::Cloud: return "Cloudy";
        case WeatherKind::Overcast: return "Overcast";
        case WeatherKind::Rain: return "Rain";
        case WeatherKind::Thunder: return "Thunderstorm";
        case WeatherKind::Snow: return "Snow";
        case WeatherKind::Fog: return "Fog";
        case WeatherKind::Wind: return "Windy";
        case WeatherKind::Hot: return "Hot";
        case WeatherKind::Cold: return "Cold";
        default: return "--";
    }
}

uint16_t WeatherPage::kindColor(WeatherKind k) {
    switch (k) {
        case WeatherKind::Sun: return 0xFD20;
        case WeatherKind::Hot: return 0xF980;
        case WeatherKind::Partly: return 0xFE60;
        case WeatherKind::Cloud: return 0xC618;
        case WeatherKind::Overcast: return 0x9CD3;
        case WeatherKind::Rain: return 0x5D9F;
        case WeatherKind::Thunder: return 0xFEA0;
        case WeatherKind::Snow: return 0xDFFF;
        case WeatherKind::Cold: return 0x9DFF;
        case WeatherKind::Fog: return 0xAD55;
        case WeatherKind::Wind: return 0xAEBC;
        default: return 0x8410;
    }
}
