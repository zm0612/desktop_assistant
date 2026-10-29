//
// Created by Zhang Zhimeng on 2026/8/28.
//
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ============================ 运行时配置（单例） ============================
// 把所有“可通过网页修改”的设置集中在这里，作为唯一数据源；持久化到 LittleFS
// 的 /config.json。默认值与 main.cpp 原来的 #define 一致（见 loadDefaults）。
// 本模块不负责挂载文件系统，也暂时没人调用——接线交给后续任务。
// ==========================================================================

// 页面数量（固定 12 页）
static constexpr int PAGE_COUNT = 12;

// 体感映射的动作数量：
//   6 个静止朝向面（对应 FaceOrientation 值 1..6）
//   4 个手势（对应 GestureType 值 1..4）
static constexpr int FACE_COUNT = 6;
static constexpr int GESTURE_COUNT = 4;

// 单页配置。id 指向字符串字面量（常驻内存，无需释放/拷贝）。
struct PageCfg {
    const char *id_; // 页面标识，如 "weather"
    bool en_; // 是否启用（关掉则轮播时跳过）
    int dwell_s_; // 停留秒数
};

class Config {
public:
    Config(const Config &) = delete;

    Config &operator=(const Config &) = delete;

    // ---- 分组字段（用嵌套结构体归类，读写更清晰）----
    struct {
        String ssid_, pass_, host_;
    } net_; // WiFi + mDNS 主机名

    struct {
        String key_, loc_;
    } weather_; // 心知天气

    struct {
        String symbols_, overview_;
        int index_count_{};
    } stock_; // 股票

    struct {
        String url_, token_, temp_, humi_, pm25_, fan_, light_, tv_, other_;
    } ha_; // Home Assistant

    struct {
        String broker_, topic_;
        int port_{};
    } mqtt_; // 消息屏（MQTT）

    struct {
        String feed_, source_;
    } news_; // 新闻 RSS

    struct {
        int tz_offset_;
        bool backlight_;
    } sys_{}; // 时区偏移(秒) + 背光

    // 页面数组：显示顺序 = 数组顺序（将来重排 = 重写此数组）
    PageCfg pages_[PAGE_COUNT]{};

    // 翻页方式：
    //   "auto"   定时轮播（忽略体感）
    //   "orient" 体感：静止朝向 + 手势决定当前页；当前朝向未映射 / 手势停留结束 / 无传感器时，
    //            按“页面”停留时间轮播兜底（永不卡死单页）
    //   （旧配置里的 "both" 在加载时被归一为 "orient"）
    //
    // 体感映射一律按“页面 id”存储（空串 "" = 不映射，该动作不切页；未启用的页不生效）：
    //   face_map_[i]    静止 6 面 -> 页 id，i = FaceOrientation 值-1
    //                   顺序: FaceUp, FaceDown, Left, Right, Top, Bottom
    //   gesture_map_[i] 手势   -> 页 id，i = GestureType 值-1
    //                   顺序: SwipeLeft, SwipeRight, DoubleTap, Shake
    //   gesture_hold_s_ 手势触发后强制停留秒数；到期退回“按朝向显示”，
    //                   以此化解“手势 vs 朝向”冲突（朝向是持续态，手势是瞬时态）。
    struct {
        String mode_;
        int gesture_hold_s_;
        String face_map_[FACE_COUNT];
        String gesture_map_[GESTURE_COUNT];
    } sw;

    // ---- 公开接口 ----
    static Config &Get(); // 单例访问

    void LoadDefaults(); // 全部字段恢复默认

    bool Load(); // 读 /config.json；任何失败 -> loadDefaults() 且返回 false

    [[nodiscard]] bool Save() const; // 序列化写入 /config.json；返回是否成功

    void ToJson(JsonDocument &doc) const; // 字段 -> JSON

    void FromJson(JsonDocument &doc); // JSON -> 字段（缺键保持原值）

    // 默认页面 id 列表（顺序即默认显示顺序）
    static const char *const DEFAULT_PAGE_IDS[PAGE_COUNT];

private:
    Config() { LoadDefaults(); } // 构造即填默认值
};
