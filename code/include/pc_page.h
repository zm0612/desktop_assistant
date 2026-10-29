//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once
#include "page.h"
#include <ESP8266WebServer.h>

// 局域网直连:PC 脚本每隔几秒 POST 一个 JSON 到 ESP，本页显示。无需 HA/broker/数据线。
// POST http://<esp-ip>:<port>/pc   body: {"cpu":42,"ram":68,"vol":60,"down":12.3,"up":1.1}
struct PcConfig {
    const char *net_unit_; // 网速单位后缀显示，如 "M/s"
};

class PcPage : public Page {
public:
    PcPage(AppContext &ctx, const PcConfig &cfg)
        : Page(ctx), pc_cfg_(cfg) {
    }

    void OnEnter() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override {
        return 10000u;
    }

    // 由 WebPortal 的 /pc 路由调用（Task 5：数据端点并入 80 端口共用 server）
    void HandlePost(ESP8266WebServer &s);

private:
    void DrawHeader() const;

    void DrawClock();

    void DrawBody();

    void DrawGauge(int cx, int cy, int pct, const char *label, const String &valTxt) const;

    void DrawVolume() const;

    static uint16_t LoadColor(int pct);

    PcConfig pc_cfg_{};

    bool valid_ = false;
    bool valid_last_ = false;
    bool dirty_ = false;
    bool online_ = false;
    int cpu_ = -1;
    int cpu_last_ = -1;
    int ram_ = -1;
    int ram_last_ = -1;
    int vol_ = -1;
    int vol_last_ = -1;
    int shown_dot_ = -1, shown_online_ = -1;
    uint32_t last_recv_ms_ = 0, last_clock_ms_ = 0;
    String down_{}, up_{};
    String down_last_{}, up_last_{};
    String shown_time_{};

    static constexpr uint32_t STALE_MS = 8000; // 超过这么久没收到 -> 判离线
};
