//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "message_page.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ctime>

// ---- 布局坐标（240x240）----
#define MSG_DIV1_Y   28
#define MSG_ACCENT_X 8
#define MSG_BODY_X   20
#define MSG_BODY_W   208        // 自动换行宽度（20..228）
#define MSG_BODY_Y0  48         // 正文顶部(无标题)，TL 基准
#define MSG_BODY_Y0T 78         // 正文顶部(有标题)
#define MSG_LINE_H   20
#define MSG_FOOT_DIV 210
#define MSG_FOOT_Y   224

MessagePage *MessagePage::self_ = nullptr;

// ================================ 生命周期 ================================
void MessagePage::Begin() {
    self_ = this;
    client_id_ = "esp12f-" + String(EspClass::getChipId(), HEX);
    mqtt_.setServer(message_cfg_.broker_, message_cfg_.port_);
    mqtt_.setCallback(&MessagePage::OnMqtt);
    mqtt_.setBufferSize(1024); // 允许较长消息
}

void MessagePage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    last_clock_ms_ = 0;
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    shown_mqtt_ = mqtt_up_;
    DrawEnvelope(12, 6, 22, 13, mqtt_up_ ? COL_ONLINE : COL_LABEL);
    DrawClock();
    DrawBody();
}

// 所有页每轮都会调用：维持 MQTT 连接并收消息（即使当前不在消息页）
void MessagePage::BackgroundTick() {
    if (!app_ctx_.wifi_up_) {
        mqtt_up_ = false;
        return;
    }
    if (mqtt_.connected()) {
        mqtt_.loop();
        if (!mqtt_up_) {
            mqtt_up_ = true;
            dirty_ = true;
        }
    } else {
        mqtt_up_ = false;
        if (millis() - last_mqtt_try_ms_ >= 5000) {
            last_mqtt_try_ms_ = millis();
            Serial.printf("[MQTT] connecting %s:%u as %s ...\n", message_cfg_.broker_, message_cfg_.port_,
                          client_id_.c_str());
            if (mqtt_.connect(client_id_.c_str())) {
                mqtt_.subscribe(message_cfg_.topic_);
                mqtt_up_ = true;
                dirty_ = true;
                Serial.printf("[MQTT] connected, subscribed: %s\n", message_cfg_.topic_);
            } else {
                Serial.printf("[MQTT] connect FAILED, state=%d\n", mqtt_.state());
            }
        }
    }
}

// OTA 升级期间调用：主动断开 MQTT，释放它常驻的 TCP socket 与缓冲，
// 让 lwIP 的 pbuf/PCB 腾给固件上传的连接（否则 loop 被上传阻塞时，broker 仍会
// 往这条空转的连接推数据，缓冲池被占满会拖垮甚至卡死 OTA 下载）。
void MessagePage::StopMqtt() {
    if (mqtt_.connected()) mqtt_.disconnect();
    mqtt_up_ = false;
}

void MessagePage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        // WiFi 点
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }
    if (shown_mqtt_ != static_cast<int>(mqtt_up_)) {
        // MQTT 信封
        shown_mqtt_ = mqtt_up_;
        app_ctx_.tft_.fillRect(10, 4, 26, 18, COL_BG);
        DrawEnvelope(12, 6, 22, 13, mqtt_up_ ? COL_ONLINE : COL_LABEL);
    }
    if (AppContext::TimeReady() && millis() - last_clock_ms_ >= 1000) {
        last_clock_ms_ = millis();
        DrawClock();
    }
    if (dirty_) {
        dirty_ = false;
        DrawBody();
    }
}

// ================================ MQTT ================================
void MessagePage::OnMqtt(const char *topic, const uint8_t *payload, const unsigned int len) {
    (void) topic;
    if (self_) self_->HandleMessage(payload, len);
}

void MessagePage::HandleMessage(const uint8_t *payload, const unsigned int len) {
    String msg;
    msg.reserve(len + 1);
    for (unsigned int i = 0; i < len; i++) msg += static_cast<char>(payload[i]);
    Serial.print("[MQTT] recv: ");
    Serial.println(msg);

    title_ = "";
    level_ = "info";
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif
    DeserializationError err = deserializeJson(doc, msg);
    if (!err && doc.is<JsonObject>()) {
        // JSON: {title, body/text, level}
        const char *t = doc["title"];
        const char *b = doc["body"];
        if (!b) b = doc["text"];
        const char *lv = doc["level"];
        title_ = t ? String(t) : "";
        body_ = b ? String(b) : msg;
        level_ = lv ? String(lv) : "info";
    } else {
        // 纯文本
        body_ = msg;
    }
    recv_time_ = NowHHMM();
    count_++;
    dirty_ = true;
}

String MessagePage::NowHHMM() {
    if (!AppContext::TimeReady()) return "--:--";
    const time_t n = time(nullptr);
    tm t;
    localtime_r(&n, &t);
    char b[8];
    sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    return {b};
}

// ================================ 渲染 ================================
void MessagePage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("Message", 42, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, MSG_DIV1_Y, 224, COL_DIV);
}

void MessagePage::DrawClock() {
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

void MessagePage::DrawStatusDot() {
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
}

void MessagePage::DrawEnvelope(const int x, const int y, const int w, const int h, const uint16_t c) const {
    app_ctx_.tft_.drawRoundRect(x, y, w, h, 2, c);
    app_ctx_.tft_.drawLine(x + 1, y + 1, x + w / 2, y + h / 2, c);
    app_ctx_.tft_.drawLine(x + w - 1, y + 1, x + w / 2, y + h / 2, c);
}

uint16_t MessagePage::LevelColor(const String &lv) {
    if (lv == "alert" || lv == "error") return COL_OFFLINE; // 红
    if (lv == "warn" || lv == "warning") return 0xFE60; // 黄
    return COL_DATE; // 青（info）
}

void MessagePage::DrawBody() const {
    app_ctx_.tft_.fillRect(0, MSG_DIV1_Y + 2, 240, 240 - (MSG_DIV1_Y + 2), COL_BG);

    if (body_.length() == 0) {
        // 空状态
        DrawEnvelope(96, 78, 48, 30, COL_LABEL);
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("Waiting for messages", 120, 134, 2);
        app_ctx_.tft_.setTextColor(COL_DIV);
        app_ctx_.tft_.drawString("topic:", 120, 160, 2);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString(message_cfg_.topic_, 120, 178, 2);
        return;
    }

    uint16_t lc = LevelColor(level_);
    app_ctx_.tft_.fillRect(MSG_ACCENT_X, 40, 4, 162, lc); // 左侧强调条

    int y = MSG_BODY_Y0;
    if (title_.length()) {
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(lc);
        app_ctx_.tft_.drawString(title_, MSG_BODY_X, 52, 4);
        y = MSG_BODY_Y0T;
    }

    int maxLines = (MSG_FOOT_DIV - y - 2) / MSG_LINE_H;
    if (maxLines > 9) maxLines = 9;
    String lines[9];
    int n = WrapText(body_, MSG_BODY_W, 2, lines, maxLines);

    app_ctx_.tft_.setTextDatum(TL_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    for (int i = 0; i < n; i++) {
        app_ctx_.tft_.drawString(lines[i], MSG_BODY_X, y, 2);
        y += MSG_LINE_H;
    }

    app_ctx_.tft_.drawFastHLine(8, MSG_FOOT_DIV, 224, COL_DIV);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("recv " + recv_time_, 12, MSG_FOOT_Y, 2);
    char cbuf[12];
    snprintf(cbuf, sizeof(cbuf), "#%d", count_);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.drawString(cbuf, 232, MSG_FOOT_Y, 2);
}

// 贪心自动换行；超长单词硬断；返回行数
int MessagePage::WrapText(const String &text, const int max_w, uint8_t font, String *out, const int max_lines) const {
    int n = 0;
    String line;
    int i = 0;
    const int L = static_cast<int>(text.length());
    while (i < L && n < max_lines) {
        while (i < L && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r')) {
            i++;
        } // 跳过空白

        if (i >= L) {
            break;
        }

        int j = i;
        while (j < L && text[j] != ' ' && text[j] != '\n' && text[j] != '\r') {
            j++;
        }

        String word = text.substring(i, j);
        i = j;

        while (app_ctx_.tft_.textWidth(word, font) > max_w && word.length() > 1) {
            // 硬断超长词
            int cut = static_cast<int>(word.length());
            while (cut > 1 && app_ctx_.tft_.textWidth(word.substring(0, cut), font) > max_w) {
                cut--;
            }

            if (line.length()) {
                out[n++] = line;
                line = "";
                if (n >= max_lines) {
                    return n;
                }
            }

            out[n++] = word.substring(0, cut);
            if (n >= max_lines) {
                return n;
            }

            word = word.substring(cut);
        }

        String trial = line.length() ? line + " " + word : word;
        if (app_ctx_.tft_.textWidth(trial, font) <= max_w) {
            line = trial;
        } else {
            out[n++] = line;
            line = word;
        }
    }
    if (n < max_lines && line.length()) out[n++] = line;
    return n;
}
