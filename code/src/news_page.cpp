//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "news_page.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ctime>

// ---- 布局坐标（240x240）----
#define N_DIV1       28
#define N_ACCENT_X   8
#define N_FEAT_X     18
#define N_FEAT_Y0    46
#define N_FEAT_LH    28
#define N_FEAT_MAX   4
#define N_IDX_Y      162
#define N_DIV2       180
#define N_CRAWL_Y    196        // TL 基准，Font2 16px -> 196..211

// ================================ 生命周期 ================================
void NewsPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    last_clock_ms_ = 0;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    DrawClock();
    scroll_x_ = 0;
    last_cycle_ms_ = millis();
    DrawFeatured();
    DrawCrawl();
}

void NewsPage::Tick() {
    if (app_ctx_.wifi_up_ && (need_fetch_ || millis() - last_fetch_ms_ >= NEWS_INTERVAL)) {
        need_fetch_ = false;
        last_fetch_ms_ = millis();
        if (FetchNews()) {
            cur_ = 0;
            scroll_x_ = 0;
            last_cycle_ms_ = millis();
            DrawFeatured();
            DrawCrawl();
        }
    }
}

void NewsPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }
    DrawClock();

    const uint32_t now = millis();
    if (have_data_ && n_ > 1 && now - last_cycle_ms_ >= 7000) {
        // 大标题轮播
        last_cycle_ms_ = now;
        cur_ = (cur_ + 1) % n_;
        DrawFeatured();
    }
    if (crawl_w_ > 0 && now - last_scroll_ms_ >= 40) {
        // 平滑滚动
        last_scroll_ms_ = now;
        scroll_x_ -= 3;
        if (scroll_x_ <= -crawl_w_) scroll_x_ += crawl_w_;
        DrawCrawl();
    }
}

// ================================ 抓取 RSS ================================
bool NewsPage::FetchNews() {
    if (!app_ctx_.wifi_up_) {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(4000);

    if (!http.begin(client, news_cfg_.feed_url_)) {
        return false;
    }

    http.addHeader("User-Agent", "Mozilla/5.0 (ESP8266)");
    const int code = http.GET();
    bool ok = false;

    if (code == 200) {
        WiFiClient *s = http.getStreamPtr();
        constexpr int CAP = 9000; // 只读前 ~9KB，够拿到前若干条，省内存
        String buf;
        buf.reserve(CAP + 16);
        const uint32_t t0 = millis();
        while (http.connected() && static_cast<int>(buf.length()) < CAP && millis() - t0 < 4000) {
            while (s->available() && static_cast<int>(buf.length()) < CAP) {
                buf += static_cast<char>(s->read());
            }

            if (!s->available()) {
                delay(2);
            }
        }

        // 顺序提取 <title>...</title>
        n_ = 0;
        int from = 0;
        while (n_ < MAXH) {
            const int a = buf.indexOf("<title", from);
            if (a < 0) {
                break;
            }

            const int gt = buf.indexOf('>', a);
            if (gt < 0) {
                break;
            }

            const int b = buf.indexOf("</title>", gt);
            if (b < 0) {
                break;
            }

            String tt = CleanTitle(buf.substring(gt + 1, b));
            from = b + 8;
            if (tt.length() > 0) {
                titles_[n_++] = tt;
            }
        }

        // 第一条通常是频道名，丢掉
        if (n_ > 1) {
            for (int i = 1; i < n_; i++) titles_[i - 1] = titles_[i];
            n_--;
        }

        if (n_ > 0) {
            have_data_ = true;
            BuildCrawl();
            ok = true;
        }
    }
    http.end();
    return ok;
}

String NewsPage::CleanTitle(const String &raw) {
    String s = raw;
    s.replace("<![CDATA[", "");
    s.replace("]]>", "");
    s.replace("&amp;", "&");
    s.replace("&quot;", "\"");
    s.replace("&#39;", "'");
    s.replace("&apos;", "'");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&#8217;", "'");
    s.replace("&#8216;", "'");
    s.replace("&#8220;", "\"");
    s.replace("&#8221;", "\"");
    // 去掉残留标签 + 只保留可打印 ASCII（过滤中文等非 ASCII，避免乱码方框）
    String out;
    bool intag = false;
    for (size_t i = 0; i < s.length(); i++) {
        const char c = s[i];
        if (c == '<') {
            intag = true;
        } else if (c == '>') {
            intag = false;
        } else if (!intag && static_cast<uint8_t>(c) >= 32 && static_cast<uint8_t>(c) <= 126) {
            out += c;
        }
    }
    out.trim();
    return out;
}

void NewsPage::BuildCrawl() {
    crawl_ = "";
    for (int i = 0; i < n_; i++) {
        crawl_ += titles_[i];
        crawl_ += "    |    ";
    }

    crawl_w_ = app_ctx_.tft_.textWidth(crawl_, 2);
    while (crawl_w_ > 0 && crawl_w_ < 300) {
        // 太短则重复，保证覆盖整条
        crawl_ += crawl_;
        crawl_w_ = app_ctx_.tft_.textWidth(crawl_, 2);
    }
}

int NewsPage::WrapText(const String &t, const int maxW, uint8_t font, String *out, const int max_lines) const {
    int n = 0;
    String line;
    int i = 0;
    const int L = static_cast<int>(t.length());
    while (i < L && n < max_lines) {
        while (i < L && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r')) i++;
        if (i >= L) break;
        int j = i;
        while (j < L && t[j] != ' ' && t[j] != '\n' && t[j] != '\r') j++;
        String word = t.substring(i, j);
        i = j;
        while (app_ctx_.tft_.textWidth(word, font) > maxW && word.length() > 1) {
            int cut = static_cast<int>(word.length());
            while (cut > 1 && app_ctx_.tft_.textWidth(word.substring(0, cut), font) > maxW) cut--;
            if (line.length()) {
                out[n++] = line;
                line = "";
                if (n >= max_lines) return n;
            }
            out[n++] = word.substring(0, cut);
            if (n >= max_lines) return n;
            word = word.substring(cut);
        }
        String trial = line.length() ? line + " " + word : word;
        if (app_ctx_.tft_.textWidth(trial, font) <= maxW) line = trial;
        else {
            out[n++] = line;
            line = word;
        }
    }
    if (n < max_lines && line.length()) out[n++] = line;
    return n;
}

// ================================ 渲染 ================================
void NewsPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("News", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, N_DIV1, 224, COL_DIV);
    app_ctx_.tft_.drawFastHLine(8, N_DIV2, 224, COL_DIV);
}

void NewsPage::DrawClock() {
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
    if (s == shown_time_) return;
    shown_time_ = s;
    app_ctx_.tft_.fillRect(150, 2, 74, 18, COL_BG);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, 212, 11, 2);
}

void NewsPage::DrawFeatured() const {
    app_ctx_.tft_.fillRect(0, N_DIV1 + 2, 240, (N_DIV2 - 1) - (N_DIV1 + 2), COL_BG);
    if (!have_data_) {
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("Loading news...", 120, 100, 4);
        return;
    }

    app_ctx_.tft_.fillRect(N_ACCENT_X, N_FEAT_Y0 - 4, 4, N_FEAT_LH * N_FEAT_MAX, COL_DATE); // 左强调条

    String lines[N_FEAT_MAX];
    const int ln = WrapText(titles_[cur_], 218, 4, lines, N_FEAT_MAX);
    app_ctx_.tft_.setTextDatum(TL_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    int y = N_FEAT_Y0;
    for (int i = 0; i < ln; i++) {
        app_ctx_.tft_.drawString(lines[i], N_FEAT_X, y, 4);
        y += N_FEAT_LH;
    }

    char idx[16];
    snprintf(idx, sizeof(idx), "%d / %d", cur_ + 1, n_);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString(idx, N_FEAT_X, N_IDX_Y, 2);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_DATE);
    app_ctx_.tft_.drawString(news_cfg_.source_, 230, N_IDX_Y, 2);
}

// 关键：不透明文字 + 首尾相接双份，每帧每像素直接覆盖，无需清屏 -> 无黑影/几乎不闪
void NewsPage::DrawCrawl() const {
    if (crawl_w_ <= 0) {
        return;
    }

    app_ctx_.tft_.setTextDatum(TL_DATUM);
    app_ctx_.tft_.setTextColor(COL_DATE, COL_BG); // 前景+背景=不透明
    app_ctx_.tft_.drawString(crawl_, scroll_x_, N_CRAWL_Y, 2);
    app_ctx_.tft_.drawString(crawl_, scroll_x_ + crawl_w_, N_CRAWL_Y, 2);
}
