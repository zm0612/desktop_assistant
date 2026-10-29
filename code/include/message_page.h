//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once
#include "page.h"
#include <PubSubClient.h>
#include <WiFiClient.h>

// MQTT 配置：ESP 订阅 topic，手机（任何网络）向同一 topic 发布即可跨局域网送达
struct MessageConfig {
    const char *broker_; // 如 "broker.emqx.io"（公共免费）
    uint16_t port_; // 1883（明文）
    const char *topic_; // 订阅主题——务必改成你独一无二的字符串
};

// 消息屏：显示远程 MQTT 推送来的消息（支持纯文本或 JSON {title,body,level}）
class MessagePage : public Page {
public:
    MessagePage(AppContext &ctx, const MessageConfig &cfg)
        : Page(ctx), message_cfg_(cfg), mqtt_(net_) {
    }

    void Begin() override;

    void OnEnter() override;

    void BackgroundTick() override; // 维持 MQTT 连接 + 收消息（所有页都跑）
    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 10000; }

    void StopMqtt(); // OTA 期间断开 MQTT，释放 socket/堆给固件上传

private:
    static void OnMqtt(const char *topic, const uint8_t *payload, unsigned int len); // PubSubClient 回调

    void HandleMessage(const uint8_t *payload, unsigned int len);

    static String NowHHMM();

    int WrapText(const String &text, int max_w, uint8_t font, String *out, int max_lines) const;

    void DrawHeader() const;

    void DrawClock();

    void DrawStatusDot();

    void DrawEnvelope(int x, int y, int w, int h, uint16_t c) const;

    void DrawBody() const;

    static uint16_t LevelColor(const String &lv);

    MessageConfig message_cfg_;
    WiFiClient net_;
    PubSubClient mqtt_;
    String client_id_;

    String title_, body_, level_ = "info";
    String recv_time_;
    int count_ = 0;

    bool mqtt_up_ = false;
    bool dirty_ = false;
    uint32_t last_mqtt_try_ms_ = 0;
    uint32_t last_clock_ms_ = 0;

    String shown_time_;
    int shown_dot_ = -1; // -1 未画 / 0 断 / 1 连
    int shown_mqtt_ = -1;

    static MessagePage *self_; // 供静态回调路由到实例
};
