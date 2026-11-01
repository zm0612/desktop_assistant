//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once

#include "page.h"
#include <ESP8266WebServer.h>

// GPU 监控页（局域网直连）：PC 脚本用 nvidia-smi 采集后 POST 到 /gpu
// body: {"util":42,"memUsed":6200,"memTotal":16376,"temp":62,"power":85,"name":"RTX 4000 Ada"}
struct GpuConfig {
};

class GpuPage : public Page {
public:
    GpuPage(AppContext &ctx, const GpuConfig &cfg)
        : Page(ctx), cpu_cfg_(cfg) {
    }

    void OnEnter() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 10000; }

    // 供概览页读取
    [[nodiscard]] int Util() const {
        return util_;
    }

    [[nodiscard]] bool Online() const {
        return valid_ && (millis() - last_recv_ms_ < STALE_MS);
    }

    // 由 WebPortal 的 /gpu 路由调用（Task 5：数据端点并入 80 端口共用 server）
    void HandlePost(ESP8266WebServer &s);

private:
    void DrawHeader() const;

    void DrawClock();

    void DrawBody();

    void DrawNameRow(); // 原位重绘：GPU 名/在线状态变化时才局部重画

    void DrawGauge(); // 利用率圆环 + 大数字（先擦数字、重铺环、再写字）

    void DrawVram(); // 显存文本 + 进度条

    void DrawTempPower(); // 温度 + 功耗

    static uint16_t HeatColor(int pct); // 0..100 绿->黄->红

    GpuConfig cpu_cfg_;

    int util_ = -1, temp_ = -1, power_ = -1;
    long mem_used_ = -1, mem_total_ = -1;
    String name_;
    bool valid_ = false, dirty_ = false, online_ = false;
    uint32_t last_recv_ms_ = 0, last_clock_ms_ = 0;
    String shown_time_;
    int shown_dot_ = -1, shown_online_ = -1;

    // 原位重绘缓存：与「已绘制」值比较，仅变化项局部重画，避免整区 fillRect 闪烁
    int shown_util_ = -2, shown_temp_ = -2, shown_power_ = -2;
    long shown_mem_used_ = -2, shown_mem_total_ = -2;
    int shown_valid_ = -1;
    String shown_name_;
    int shown_util_w_ = 0, shown_vram_w_ = 0, shown_temp_w_ = 0, shown_power_w_ = 0;

    static constexpr uint32_t STALE_MS = 8000;
};
