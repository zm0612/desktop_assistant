//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once
#include "page.h"

// Home Assistant 连接 + 要显示的 entity_id（任一为 "" 则跳过该项）
struct HomeConfig {
    const char *ha_url_; // 例 "http://192.168.1.20:8123"（无末尾斜杠）
    const char *token_; // 长期访问令牌
    const char *temp_eid_; // sensor.*_temperature
    const char *humid_eid_; // sensor.*_humidity
    const char *pm25_eid_; // sensor.*_pm2_5
    const char *purifier_eid_; // fan.* 或 switch.*
    const char *light_eid_; // light.*
    const char *tv_eid_; // media_player.*
    const char *other_eid_; // 任意实体（第 4 个设备胶囊，右下角）
};

// 米家/智能家居状态页：读 HA 的 /api/states/<eid>
class HomePage : public Page {
public:
    HomePage(AppContext &ctx, const HomeConfig &cfg) : Page(ctx), home_cfg_(cfg) {
    }

    void OnEnter() override;

    void Tick() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 10000; }

private:
    void FetchAll();

    bool HaState(const char *eid, String &out) const; // GET 单个实体的 state

    [[nodiscard]] bool HaReachable() const; // 发 6 个 GET 前先做一次短超时 TCP 探测：HA 离线即快速放弃

    static int IsOn(const String &st); // HA 状态字符串 -> 1/0

    void DrawHeader() const;

    void DrawClock();

    void DrawStatusDot();

    void DrawBody() const;

    void DrawChip(int x, int y, int w, int h, const char *label, int on) const;

    HomeConfig home_cfg_;

    float temp_ = NAN;
    float humid_ = NAN;
    int pm25_ = -1;
    int purifier_on_ = -1;
    int light_on_ = -1;
    int tv_on_ = -1; // -1 未知 / 0 关 / 1 开
    int other_on_ = -1;

    bool valid_ = false;
    bool dirty_ = false;
    bool need_fetch_ = true;
    uint32_t last_fetch_ms_ = 0;
    uint8_t fail_streak_ = 0; // 连续取数失败次数：Tick 据此按倍数拉长重试间隔（退避）
    uint32_t last_clock_ms_ = 0;

    String shown_time_;
    bool shown_dot_ = false;

    static constexpr uint32_t HOME_INTERVAL = 30UL * 1000UL; // 30s 刷新
};
