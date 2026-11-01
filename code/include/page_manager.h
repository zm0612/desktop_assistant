//
// Created by Zhang Zhimeng on 2026/8/24.
//
#pragma once
#include "motion_detector.h"
#include "page.h"

// 翻页方式：Auto=定时轮播；Orient=陀螺仪定向
enum class SwitchMode { Auto, Orient };

// 定向翻页钩子接口（将来 MPU6500 实现并注入）。
// orientationPage() 返回“启用页顺序”里的下标；返回 -1（或越界）表示“维持当前页”。
struct IOrient {
    virtual int OrientationPage() = 0;

    virtual ~IOrient() = default;
};

// 页面管理器：统一管理 WiFi/NTP、背光；配置全部来自 Config 单例。
// 注册时携带页面 id，begin() 时按 Config.pages[] 的启用与顺序构建轮播列表。
class PageManager {
public:
    explicit PageManager(TFT_eSPI &tft);

    void AddPage(Page *p, const char *id); // 注册页面（附带 id，供 Config 匹配启用/顺序）
    void Begin(); // 连 WiFi、校时、开背光、进首页（配置读 Config）
    void Loop(); // 主循环里持续调用（非阻塞）

    // 翻页 API
    void NextPage(); // 立即切到下一启用页（重置停留计时）
    void PrevPage(); // 立即切到上一启用页（重置停留计时）
    void GotoPage(int orderIdx); // 跳到“启用顺序”里的第 orderIdx 页
    void SetOrientationSource(IOrient *s) { orient_ = s; } // 注入定向翻页源（默认无）

    void SetMotionDetector(MotionDetector *md) { motion_detector_ = md; }

    // 体感活动上报：有运动/手势时调用，刷新活动计时；若已灭屏则立即点亮。
    void NotifyActivity();

    // 关闭自动灭屏并把背光恢复到 Config 设定（无 MPU 传感器时用，避免灭屏后无从唤醒）。
    void DisableAutoDim();

    // 由页面 id 查它在“启用轮播顺序”里的下标；未启用/不存在返回 -1。
    // 供体感路由器把 Config 里的页 id 映射解析成 orientationPage() 需要的下标。
    [[nodiscard]] int OrderIndexOfId(const char *id) const;

    // OTA 期间由 WebPortal 的进度回调驱动：暂停轮播/渲染，独占屏幕显示升级进度
    void OtaBegin(); // 进入 OTA 模式：停轮播、清屏、画“固件升级”外框
    void OtaProgress(int pct); // 刷新进度条（0..100，内部按整数百分比节流）

    AppContext &Ctx() { return app_ctx_; } // 供页面构造时取共享上下文

    // 联网模式对外只读：main.cpp 据此把模式/热点名传给 WebPortal
    [[nodiscard]] bool ApMode() const { return ap_mode_; } // true=AP 配网模式

    [[nodiscard]] const String &ApSSID() const { return ap_ssid_; } // AP 模式下的热点名

private:
    void HandleWifi();

    void EnterOrder(int orderIdx); // 内部：切到某启用页并整屏重绘 + 重置计时

    bool TryConnectSTA(); // 尝试连 WiFi（阻塞至多 ~15s），成功返回 true

    void StartAP(); // 启动开放热点 InfoDisplay-XXXX，置 _apMode/_apSsid

    void DrawConnecting() const; // “连接中…”过场屏

    void DrawSetupScreen() const; // AP 模式配网引导屏

    void SetBacklight(bool on); // 统一控背光引脚（低电平点亮）并同步 backlight_on_ 标志

    int GetScreenRotation() const;

    AppContext app_ctx_;

    // 注册表（与 id 平行数组）
    static constexpr int MAX_PAGES = 16;
    Page *pages_[MAX_PAGES]{};
    const char *ids_[MAX_PAGES]{};
    int count_ = 0; // 已注册页数

    // 启用 + 排序后的轮播列表：_order[k]=注册下标，_dwellMs[k]=该页停留毫秒
    int order_[MAX_PAGES]{};
    uint32_t dwell_ms_[MAX_PAGES]{};
    int order_count_ = 0; // 轮播列表长度
    int cur_order_ = 0; // 当前在轮播列表中的位置
    uint32_t last_switch_ = 0;

    SwitchMode mode_ = SwitchMode::Auto;
    IOrient *orient_ = nullptr;
    MotionDetector *motion_detector_ = nullptr;

    bool wifi_up_ = false;
    uint32_t last_wifi_try_ms_ = 0;
    static constexpr uint32_t WIFI_RETRY_INTERVAL = 15000UL;

    bool ap_mode_ = false; // 是否处于 AP 配网模式
    String ap_ssid_; // AP 模式热点名 InfoDisplay-XXXX

    bool ota_mode_ = false; // OTA 升级中：loop 让位，屏幕独占显示进度
    int ota_shown_pct_ = -1; // 已绘制的进度百分比（节流，避免每块都重绘）

    // ---- 背光自动熄灭（省电）：静止无活动超时灭背光，MPU 检测到活动即唤醒 ----
    // 仅在 Config.sys_.backlight_==true 时启用（auto_dim_enabled_）；只控背光引脚，
    // 不发 ST7789 sleep-in，显存内容保留，唤醒即时且就是当前页。
    static constexpr uint32_t BACKLIGHT_TIMEOUT_MS = 300000UL; // 空闲超时 1 分钟
    uint32_t last_activity_ms_ = 0; // 最近一次活动时间戳
    bool backlight_on_ = false; // 背光引脚状态镜像（与引脚始终同步）
    bool auto_dim_enabled_ = false; // 是否启用自动灭屏（默认 false：AP/无传感器不误灭）
};
