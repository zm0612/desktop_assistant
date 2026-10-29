//
// Created by Zhang Zhimeng on 2026/8/23.
//
#pragma once

#include "page.h"

// 天气类别（由心知 code 归类，图标与英文名据此决定）
enum class WeatherKind : uint8_t {
    Sun, Partly, Cloud, Overcast, Rain, Thunder, Snow, Fog, Wind, Hot, Cold, Unknown
};

// 天气实况
struct WeatherNow {
    bool valid = false;
    int code = 99;
    float temp = NAN;
    String city;
};

// 单日预报（底部三天条）
struct DayForecast {
    int code_ = 99;
    int high_ = -999;
    int low_ = -999;
    char label_[8] = ""; // "Today" / "Tue" ...
};

class WeatherPage : public Page {
public:
    WeatherPage(AppContext &ctx, const char *apiKey, const char *location)
        : Page(ctx), api_key_(apiKey), loc_(location) {
    }

    void OnEnter() override;

    void Tick() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 8000; }

    // 供其它页复用（纯函数）
    static WeatherKind Classify(int code);

    static const char *KindName(WeatherKind k);

private:
    void DrawClock(bool force = false);

    void DrawDate(bool force = false);

    void DrawWeather() const;

    void DrawCurrentRow() const;

    void DrawForecastStrip() const;

    void DrawStatusDot();

    void DrawCentered(const String &s, int cx, int y, uint8_t font, uint16_t fg) const;

    // 图标
    void DrawIcon(WeatherKind kind, int cx, int cy, int r) const;

    void DrawSun(int cx, int cy, int r, uint16_t color) const;

    void DrawCloud(int cx, int cy, int r, uint16_t body) const;

    void DrawRainLines(int cx, int cy, int r, uint16_t color) const;

    void DrawBolt(int cx, int cy, int r, uint16_t color) const;

    void DrawFlakes(int cx, int cy, int r, uint16_t color) const;

    void DrawLines(int cx, int cy, int r, uint16_t color, int n) const;

    // 数据
    bool FetchWeather();

    bool FetchForecast();

    void SetDayLabels();

    // 归类
    static uint16_t kindColor(WeatherKind k);

    const char *api_key_;
    const char *loc_;

    WeatherNow data_;
    DayForecast days_[3];
    bool fc_valid_ = false;
    bool weather_dirty_ = false;
    bool fc_dirty_ = false;
    bool need_fetch_ = true;
    uint32_t last_clock_ms_ = 0;
    uint32_t last_fetch_ms_ = 0;

    String shown_time_;
    String shown_date_;
    bool shown_dot_ = false;

    static constexpr uint32_t WEATHER_INTERVAL = 30UL * 60UL * 1000UL;
};
