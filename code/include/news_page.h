//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once
#include "page.h"

// 新闻页配置：RSS 源（建议 http 且英文）+ 显示用来源名
struct NewsConfig {
    const char *feed_url_; // 如 "http://feeds.bbci.co.uk/news/world/rss.xml"
    const char *source_; // 显示用，如 "BBC"
};

// 新闻头条页：上方大标题轮播 + 底部平滑滚动跑马灯（不透明双份绘制，抗闪烁）
class NewsPage : public Page {
public:
    NewsPage(AppContext &ctx, const NewsConfig &cfg) : Page(ctx), news_cfg_(cfg) {
    }

    void OnEnter() override;

    void Tick() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 13000; }

private:
    bool FetchNews();

    static String CleanTitle(const String &raw);

    int WrapText(const String &t, int maxW, uint8_t font, String *out, int max_lines) const;

    void BuildCrawl();

    void DrawHeader() const;

    void DrawClock();

    void DrawFeatured() const;

    void DrawCrawl() const;

    NewsConfig news_cfg_;
    static constexpr int MAXH = 8;
    int n_ = 0;
    int cur_ = 0;

    String titles_[MAXH];
    String crawl_;

    int crawl_w_ = 0;
    int scroll_x_ = 0;

    bool have_data_ = false;
    bool need_fetch_ = true;
    uint32_t last_fetch_ms_ = 0, last_cycle_ms_ = 0, last_scroll_ms_ = 0, last_clock_ms_ = 0;

    String shown_time_;
    int shown_dot_ = -1;

    static constexpr uint32_t NEWS_INTERVAL = 15UL * 60UL * 1000UL; // 15 分钟刷新
};
