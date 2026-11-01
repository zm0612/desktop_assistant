//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "overview_page.h"
#include "weather_page.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ctime>
#include <cmath>
#include <cstring>

// ================================ 生命周期 ================================
void OverviewPage::OnEnter() {
    app_ctx_.tft_.drawFastVLine(120, 8, 224, COL_DIV);
    app_ctx_.tft_.drawFastHLine(8, 120, 224, COL_DIV);
    last_clock_ms_ = 0;
    DrawTime(true);
    DrawWeatherCell(true);
    DrawStockCell(true);
    DrawGpuCell(true);
}

void OverviewPage::Tick() {
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_w_ms_ >= W_INT)) {
        last_w_ms_ = millis();
        if (FetchWeather()) DrawWeatherCell();
    }
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_s_ms_ >= S_INT)) {
        last_s_ms_ = millis();
        if (FetchStock()) DrawStockCell();
    }
    need_fetch_ = false;
}

void OverviewPage::Render() {
    DrawTime(false); // 分钟变了才重画
    DrawGpuCell(false); // GPU 占用变了才重画
}

// ================================ 取数 ================================
bool OverviewPage::FetchWeather() {
    if (!app_ctx_.wifi_up_) return false;
    const String url = String("http://api.seniverse.com/v3/weather/now.json?key=")
                       + overview_cfg_.api_key_ + "&location=" + overview_cfg_.location_ + "&language=en&unit=c";
    WiFiClient c;
    HTTPClient h;
    h.setTimeout(3000);
    if (!h.begin(c, url)) {
        return false;
    }

    const int st = h.GET();
    bool ok = false;
    if (st == 200) {
        String p = h.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(2048);
#endif
        if (!deserializeJson(doc, p)) {
            const JsonVariant now = doc["results"][0]["now"];
            if (!now.isNull()) {
                w_code_ = now["code"].as<int>();
                temp_ = static_cast<int>(now["temperature"].as<float>() + 0.5f);
                w_valid_ = true;
                ok = true;
            }
        }
    }
    h.end();
    return ok;
}

bool OverviewPage::FetchStock() {
    if (!app_ctx_.wifi_up_) return false;
    String url = String("http://qt.gtimg.cn/q=") + overview_cfg_.stock_;
    WiFiClient c;
    HTTPClient h;
    h.setTimeout(3000);
    if (!h.begin(c, url)) {
        return false;
    }

    const int st = h.GET();
    bool ok = false;
    if (st == 200) {
        const String p = h.getString();
        const int q1 = p.indexOf("=\"");
        if (q1 >= 0) {
            const int q2 = p.indexOf('"', q1 + 2);
            if (q2 >= 0) {
                String seg = p.substring(q1 + 2, q2);
                int f = 0, start = 0;
                const int L = seg.length();
                String price, prev;
                for (int i = 0; i <= L; i++) {
                    if (i == L || seg[i] == '~') {
                        const String tok = seg.substring(start, i);
                        if (f == 3) price = tok;
                        else if (f == 4) {
                            prev = tok;
                            break;
                        }
                        f++;
                        start = i + 1;
                    }
                }
                const float pr = price.toFloat();
                const float pv = prev.toFloat();
                stk_price_ = pr;
                stk_pct_ = (pv > 0) ? (pr - pv) / pv * 100.0f : 0;
                s_valid = (pv > 0);
                ok = s_valid;
            }
        }
    }
    h.end();
    return ok;
}

// ================================ 渲染 ================================
// 原位文本替换：不整块清屏，只擦除/重画字符串占用的像素，避免闪烁。
// 新旧串中心位置相同时，仅重绘两者不同的后缀部分（如分钟跳动只重画末位），
// 其余字符原位不动；位置或长度变化时，整串原位擦除后重画。
void OverviewPage::ReplaceText(const String &old_s, const String &new_s,
                               const int32_t ox, const int32_t oy,
                               const int32_t nx, const int32_t ny,
                               uint8_t font, const uint16_t fg) const {
    if (old_s == new_s) return;
    TFT_eSPI &tft = app_ctx_.tft_;
    int n = 0;
    if (ox == nx && oy == ny && old_s.length() == new_s.length()) {
        while (n < static_cast<int>(old_s.length()) && n < static_cast<int>(new_s.length())
               && old_s[n] == new_s[n])
            n++;
    }
    if (n > 0) {
        // 只重绘变化的后缀：先按背景色精确擦掉旧后缀，再画新后缀
        const int32_t x = ox - tft.textWidth(old_s, font) / 2
                          + tft.textWidth(old_s.substring(0, n), font);
        const String ot = old_s.substring(n);
        const String nt = new_s.substring(n);
        tft.setTextDatum(ML_DATUM);
        if (ot.length()) {
            tft.setTextColor(COL_BG);
            tft.drawString(ot, x, oy, font);
        }
        if (nt.length()) {
            tft.setTextColor(fg);
            tft.drawString(nt, x, oy, font);
        }
        tft.setTextDatum(MC_DATUM);
    } else {
        if (old_s.length()) {
            tft.setTextColor(COL_BG);
            tft.drawString(old_s, ox, oy, font);
        }
        if (new_s.length()) {
            tft.setTextColor(fg);
            tft.drawString(new_s, nx, ny, font);
        }
    }
}

void OverviewPage::DrawDegree(int32_t cx, uint16_t color) const {
    app_ctx_.tft_.drawCircle(cx, 44, 4, color);
    app_ctx_.tft_.drawCircle(cx, 44, 3, color);
}

void OverviewPage::DrawTime(bool force) {
    static const char *WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *MO[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    String s = "--:--";
    String date;
    if (AppContext::TimeReady()) {
        time_t n = time(nullptr);
        struct tm t;
        localtime_r(&n, &t);
        char b[8];
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
        char db[24];
        sprintf(db, "%s %d %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
        s = b;
        date = db;
    }
    app_ctx_.tft_.setTextDatum(MC_DATUM);
    if (force) {
        app_ctx_.tft_.fillRect(6, 30, 110, 86, COL_BG);
        shown_hm_ = "";
        shown_date_ = "";
    }
    ReplaceText(shown_hm_, s, 62, 52, 62, 52, 4, COL_TIME);
    shown_hm_ = s;
    ReplaceText(shown_date_, date, 62, 82, 62, 82, 2, COL_DATE);
    shown_date_ = date;
}

void OverviewPage::DrawWeatherCell(bool force) {
    TFT_eSPI &tft = app_ctx_.tft_;
    tft.setTextDatum(MC_DATUM);
    if (force) {
        tft.fillRect(124, 30, 110, 86, COL_BG);
        shown_temp_ = "";
        shown_w_name_ = "";
        temp_cx_ = 0;
    }
    String ts = w_valid_ ? String(temp_) : String("--");
    String wn = w_valid_
                    ? String(WeatherPage::KindName(WeatherPage::Classify(w_code_)))
                    : String("");
    // 温度："--" 与数值的中心位置不同，位置变化时整串原位替换
    if (ts != shown_temp_) {
        ReplaceText(shown_temp_, ts,
                    (shown_temp_ == "--") ? 180 : 172, (shown_temp_ == "--") ? 60 : 50,
                    (ts == "--") ? 180 : 172, (ts == "--") ? 60 : 50,
                    4, (ts == "--") ? COL_LABEL : COL_TEMP);
        if (temp_cx_) DrawDegree(temp_cx_, COL_BG); // 擦掉旧的度数圈
        if (ts != "--") {
            temp_cx_ = 172 + tft.textWidth(ts, 4) / 2 + 7;
            DrawDegree(temp_cx_, COL_TEMP);
        } else {
            temp_cx_ = 0;
        }
        shown_temp_ = ts;
    }
    // 天气名（变化少，独立原位替换）
    ReplaceText(shown_w_name_, wn, 180, 82, 180, 82, 2, COL_TIME);
    shown_w_name_ = wn;
}

void OverviewPage::DrawStockCell(bool force) {
    TFT_eSPI &tft = app_ctx_.tft_;
    tft.setTextDatum(MC_DATUM);
    if (force) {
        tft.fillRect(6, 126, 110, 104, COL_BG);
        const char *code = overview_cfg_.stock_;
        const char *shortc = (strlen(code) > 2) ? code + 2 : code;
        tft.setTextColor(COL_LABEL);
        tft.drawString(shortc, 62, 138, 2);
        shown_price_ = "";
        shown_pct_ = "";
    }
    // 现价："--" 与数值的行高不同
    const String price = s_valid ? String(stk_price_, 2) : String("--");
    ReplaceText(shown_price_, price,
                62, (shown_price_ == "--") ? 172 : 168,
                62, (price == "--") ? 172 : 168,
                4, (price == "--") ? COL_LABEL : COL_TIME);
    shown_price_ = price;
    // 涨跌幅（无数据时整串擦除）
    String pct;
    if (s_valid) {
        char pb[12];
        snprintf(pb, sizeof(pb), "%+.2f%%", stk_pct_);
        pct = pb;
    }
    ReplaceText(shown_pct_, pct, 62, 198, 62, 198, 2, stk_pct_ >= 0 ? COL_UP : COL_DOWN);
    shown_pct_ = pct;
}

void OverviewPage::DrawGpuCell(const bool force) {
    const int u = gpu_.Online() ? gpu_.Util() : -1;
    if (!force && u == shown_gpu_) return;
    TFT_eSPI &tft = app_ctx_.tft_;
    tft.setTextDatum(MC_DATUM);
    if (force) {
        tft.fillRect(124, 126, 110, 104, COL_BG);
        tft.setTextColor(COL_LABEL);
        tft.drawString("GPU", 180, 138, 2);
        tft.drawRect(140, 198, 80, 8, COL_DIV);
        shown_gpu_ = -2;
        shown_bar_col_ = 0;
    }
    const uint16_t c = (u < 60) ? COL_ONLINE : ((u < 85) ? 0xFE60 : COL_OFFLINE);
    // 百分比文本："--" 与数值的行高不同
    const String old_s = (shown_gpu_ < 0) ? String("--") : String(shown_gpu_) + "%";
    const String new_s = (u < 0) ? String("--") : String(u) + "%";
    ReplaceText(old_s, new_s,
                180, (shown_gpu_ < 0) ? 172 : 170,
                180, (u < 0) ? 172 : 170,
                4, (u < 0) ? COL_LABEL : c);
    // 进度条：只增删变化的部分，不清整条
    constexpr int x = 140, y = 198, w = 80;
    if (u < 0) {
        tft.fillRect(x + 1, y + 1, w - 2, 6, COL_BG); // 离线：清空填充
    } else {
        const int old_w = (shown_gpu_ >= 0) ? (w - 2) * shown_gpu_ / 100 : 0;
        const int new_w = (w - 2) * u / 100;
        if (c == shown_bar_col_) {
            // 颜色没变：只重画增减的部分
            if (new_w > old_w) tft.fillRect(x + 1 + old_w, y + 1, new_w - old_w, 6, c);
            else if (new_w < old_w) tft.fillRect(x + 1 + new_w, y + 1, old_w - new_w, 6, COL_BG);
        } else {
            // 颜色变化：整条重画
            tft.fillRect(x + 1, y + 1, new_w, 6, c);
            if (new_w < old_w) tft.fillRect(x + 1 + new_w, y + 1, old_w - new_w, 6, COL_BG);
        }
        shown_bar_col_ = c;
    }
    shown_gpu_ = u;
}
