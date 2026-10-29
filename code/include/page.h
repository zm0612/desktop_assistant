//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ctime>

// ============================================================================
//  多页框架的共享定义
//  - AppContext：屏幕句柄 + 联网/时间状态，由 PageManager 维护并注入各页
//  - Page：页面基类，PageManager 负责驱动其生命周期
// ============================================================================

// ---- 全局深色主题配色（各页共用，保证风格一致）----
#define COL_BG      TFT_BLACK
#define COL_TIME    0xFFFF      // 白
#define COL_DATE    0x07FF      // 青
#define COL_TEMP    0xFD20      // 琥珀/橙
#define COL_LABEL   0x8410      // 灰
#define COL_DIV     0x39C7      // 深灰分割线
#define COL_ONLINE  0x07E0      // 绿（联网）
#define COL_OFFLINE 0xF800      // 红（断网）
#define COL_UP      0xF800      // 涨=红（A股习惯；想改西方绿涨，与 COL_DOWN 对调即可）
#define COL_DOWN    0x07E0      // 跌=绿

struct AppContext {
    TFT_eSPI &tft_;
    bool wifi_up_; // 由 PageManager 每轮更新
    float battery_v_ = 0.0f; // 电池电压(V)，由 main.cpp 每秒采样写入；页面按需读取显示
    explicit AppContext(TFT_eSPI &t) : tft_(t), wifi_up_(false) {
    }

    static bool TimeReady() { return time(nullptr) > 1609459200; } // >2021-01-01 视为已校时
};

class Page {
public:
    explicit Page(AppContext &ctx) : app_ctx_(ctx) {
    }

    virtual ~Page() = default;

    virtual void Begin() {
    } // 上电初始化（一次）

    virtual void OnEnter() {
    } // 切入本页：整屏已被清空，这里做全绘

    virtual void Tick() {
    } // 每轮 loop：取数 / 滚动等逻辑

    virtual void Render() {
    } // 每轮 loop：增量重绘

    virtual void BackgroundTick() {
    } // 每轮 loop：所有页都跑（如维持 MQTT 连接/收消息）

    [[nodiscard]] virtual uint16_t DwellMs() const {
        return 8000u;
    } // 本页停留多久后轮播

protected:
    AppContext &app_ctx_;
};
