#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#include <ctime>

#include "mpu6500.h"
#include "motion_detector.h"
#include "motion_router.h"
#include "battery_monitor.h"
#include "config.h"
#include "weather_page.h"
#include "page_manager.h"
#include "home_page.h"
#include "stock_page.h"
#include "message_page.h"
#include "system_page.h"
#include "pet_page.h"
#include "news_page.h"
#include "pc_page.h"
#include "gpu_page.h"
#include "clock_page.h"
#include "overview_page.h"
#include "train_page.h"
#include "web_portal.h"


// ============================ 配置来源 ============================
// 所有可调设置现在集中在 Config 单例（持久化到 LittleFS 的 /config.json），
// 不再用 #define。默认值见 Config::loadDefaults()，网页表单改完 save→重启生效。
// ================================================================
static auto tft = TFT_eSPI();
static PageManager manager(tft);
static WebPortal portal; // 80 端口配置门户（读写 /config.json，save→重启）

// 页面对象用“指针 + setup 内 new”：Config 在 setup() 里从 flash 载入，
// 若页面是全局对象（静态初始化早于 setup），会捕获默认配置，且 load() 重赋 String 后
// 之前 .c_str() 的指针会悬空。因此必须在 Config::load() 之后再构造页面。
static WeatherPage *weather_page;
static StockPage *stock_page;
static HomePage *home_page;
static MessagePage *message_page;
static SystemPage *system_page;
static PetPage *pet_page;
static NewsPage *news_page;
static PcPage *pc_page;
static ClockPage *clock_page;
static GpuPage *gpu_page;
static TrainPage *train_page;
static OverviewPage *overview_page;


// MPU6500 动作检测：I2C 走默认 Wire（ESP8266 = GPIO4/SDA、GPIO5/SCL，CLAUDE.md 预留引脚）。
// 检测结果经 MotionRouter 接入翻页：静止朝向 + 手势 -> 页（映射见 Config.sw，网页可配）。
static MPU6500 *mpu; // 默认地址 0x68
static MotionDetector motion;
static MotionRouter router; // 体感 -> 翻页路由（实现 IOrient，注入 PageManager）
static bool mpu_ok = false; // 传感器在线标志：失败则 loop 跳过读取

static BatteryMonitor *battery_monitor_ptr;

// 手势回调：串口打印 + 转发给体感路由（由其按 Config 映射决定是否翻页、并起停留计时）
static void OnGesture(const GestureEvent &e) {
    auto n = "?";
    switch (e.type) {
        case GestureType::SwipeLeft: {
            n = "向左甩";
            break;
        }
        case GestureType::SwipeRight: {
            n = "向右甩";
            break;
        }
        case GestureType::DoubleTap: {
            n = "双击";
            break;
        }
        case GestureType::Shake: {
            n = "摇一摇";
            break;
        }
        default: break;
    }
    Serial.printf("[手势] %s (axis=%d strength=%.1f)\n", n, e.axis, e.strength);
    manager.NotifyActivity(); // 手势=用户交互：唤醒背光并刷新活动计时
    router.HandleGesture(e); // 交给路由：命中映射则切页并停留
}

void setup() {
    Serial.begin(115200);

    // GPIO15 上电默认下拉，作屏幕 RST 需主动拉高；背光先关，避免初始化闪屏
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    pinMode(16, OUTPUT);
    digitalWrite(16, HIGH);
    delay(10);

    tft.init();
    tft.setRotation(0);

    LittleFS.begin();
    Config::Get().Load(); // 读 /config.json；失败自动用默认值
    const Config &C = Config::Get();

    // 各页配置结构体：字段是指向 Config 内 String 的 .c_str() 指针。
    // Config 载入后不再改动（save→重启模式），故这些指针在整个运行期有效。
    static HomeConfig home_cfg = {
        .ha_url_ = C.ha_.url_.c_str(),
        .token_ = C.ha_.token_.c_str(),
        .temp_eid_ = C.ha_.temp_.c_str(),
        .humid_eid_ = C.ha_.humi_.c_str(),
        .pm25_eid_ = C.ha_.pm25_.c_str(),
        .purifier_eid_ = C.ha_.fan_.c_str(),
        .light_eid_ = C.ha_.light_.c_str(),
        .tv_eid_ = C.ha_.tv_.c_str(),
        .other_eid_ = C.ha_.other_.c_str()
    };
    static MessageConfig msg_cfg = {
        .broker_ = C.mqtt_.broker_.c_str(),
        .port_ = static_cast<uint16_t>(C.mqtt_.port_),
        .topic_ = C.mqtt_.topic_.c_str()
    };
    static NewsConfig news_cfg = {
        .feed_url_ = C.news_.feed_.c_str(),
        .source_ = C.news_.source_.c_str()
    };
    static PcConfig pc_cfg = {
        .net_unit_ = "M/s"
    }; // 端口暂留；Task 5 合并到 80 端口再改

    static GpuConfig gpu_cfg;
    static TrainConfig train_cfg;
    static OverviewConfig ov_cfg = {
        .api_key_ = C.weather_.key_.c_str(),
        .location_ = C.weather_.loc_.c_str(),
        .stock_ = C.stock_.overview_.c_str()
    };

    weather_page = new WeatherPage(manager.Ctx(), C.weather_.key_.c_str(), C.weather_.loc_.c_str());
    stock_page = new StockPage(manager.Ctx(), C.stock_.symbols_.c_str(), C.stock_.index_count_);
    home_page = new HomePage(manager.Ctx(), home_cfg);
    message_page = new MessagePage(manager.Ctx(), msg_cfg);
    system_page = new SystemPage(manager.Ctx());
    pet_page = new PetPage(manager.Ctx());
    news_page = new NewsPage(manager.Ctx(), news_cfg);
    pc_page = new PcPage(manager.Ctx(), pc_cfg);
    clock_page = new ClockPage(manager.Ctx());
    gpu_page = new GpuPage(manager.Ctx(), gpu_cfg);
    train_page = new TrainPage(manager.Ctx(), train_cfg);
    overview_page = new OverviewPage(manager.Ctx(), ov_cfg, *gpu_page);

    // 注册全部 12 页（id 必须与 Config 的 DEFAULT_PAGE_IDS 一致）；启用/顺序由 Config 决定
    manager.AddPage(weather_page, "weather");
    manager.AddPage(stock_page, "stock");
    manager.AddPage(home_page, "home");
    manager.AddPage(message_page, "message");
    manager.AddPage(system_page, "system");
    manager.AddPage(pet_page, "pet");
    manager.AddPage(news_page, "news");
    manager.AddPage(pc_page, "pc");
    manager.AddPage(clock_page, "clock");
    manager.AddPage(gpu_page, "gpu");
    manager.AddPage(train_page, "train");
    manager.AddPage(overview_page, "overview");

    manager.Begin(); // 连 WiFi/校时、按 Config 建轮播表、开背光、进首页

    manager.SetMotionDetector(&motion); // 注入motion detector，用于后续屏幕的方向

    // Task 5：Pc/Gpu/Train 数据端点并入 80 端口共用 server（各页原独立 server 已删）
    portal.OnData("/pc", [](ESP8266WebServer &s) { pc_page->HandlePost(s); });
    portal.OnData("/gpu", [](ESP8266WebServer &s) { gpu_page->HandlePost(s); });
    portal.OnData("/train", [](ESP8266WebServer &s) { train_page->HandlePost(s); });

    // OTA 期间腾资源 + 屏上显示进度：一收到固件就断 MQTT（释放 socket/堆给上传），
    // 暂停轮播/渲染并独占屏幕画进度条，避免上传被后台网络挤占而卡死。
    portal.OnOtaStart([]() {
        message_page->StopMqtt();
        manager.OtaBegin();
    });
    portal.OnOtaProgress([](const int pct) { manager.OtaProgress(pct); });

    portal.Begin(manager.ApMode(), manager.ApSSID()); // 80 端口门户：STA=普通；AP=强制门户+/api/status 回报真实模式

    // ---- MPU6500 动作检测初始化 ----
    // AFS_4G：给甩动/摇动留加速度余量（±2g 容易被强动作打满）；陀螺仪未用，量程随意。
    mpu = new MPU6500;
    mpu_ok = mpu->Begin(AccelRange::AFS_4G, GyroRange::GFS_500DPS, DLPFBandwidth::BW_41Hz, 200);
    if (mpu_ok) {
        motion.SetSerialDebug(true); // 上机校准期打开：串口打印峰值/朝向；确认无误后可关
        motion.OnGesture(OnGesture);
        router.Begin(&manager, &motion); // 载入 Config.sw 体感映射快照
        manager.SetOrientationSource(&router); // 注入体感翻页源（orient 模式下生效）
        Serial.println("[MPU6500] 动作检测已启动");
    } else {
        // 无传感器：不注入体感源。若用户选了 orient 模式，PageManager 会兜底走定时轮播，不至于卡死单页。
        Serial.println("[MPU6500] 未检测到传感器，动作检测关闭");
        // 唯一唤醒源（MPU）不存在：关闭自动灭屏并恢复背光到 Config 设定，避免灭屏后永久黑屏。
        manager.DisableAutoDim();
    }

    battery_monitor_ptr = new BatteryMonitor;
}

void loop() {
    manager.Loop(); // 非阻塞：轮播/定向翻页 + 各页刷新 + 后台维持 MQTT
    portal.Handle(); // 处理配置门户的 HTTP 请求

    // ~5ms(≈200Hz)节流喂一帧给动作检测器；传感器缺失则跳过。
    // 注意：manager.loop() 内若有 HTTP 抓取/整屏重绘卡顿，会拉长采样间隔（已知局限）。
    if (mpu_ok) {
        static uint32_t last_read = 0;
        const uint32_t now = millis();
        if (now - last_read >= 5) {
            last_read = now;
            IMUData d{};
            if (mpu->Read(d)) {
                motion.Update(d);
                // 加速度模长偏离重力(≈9.81) => 设备被拿起/触碰/转动，视为活动，唤醒背光。
                const float mag = sqrtf(d.ax_ * d.ax_ + d.ay_ * d.ay_ + d.az_ * d.az_);
                if (fabsf(mag - 9.80665f) > 1.2f) manager.NotifyActivity();
            }
        }
    }

    static std::uint32_t last_battery_time = 0u;
    const std::uint32_t now = millis();
    if (now - last_battery_time >= 1000) {
        const float volt = battery_monitor_ptr->ReadVoltage();
        last_battery_time = now;
        manager.Ctx().battery_v_ = volt; // 写入共享上下文，供 SystemPage 显示
    }

    // 让出 ~1ms 空闲窗口给 SDK，使 WIFI_MODEM_SLEEP 能在 DTIM 间隙关断射频省电。
    // （yield()/delay(0) 不产生真正空闲，省不了电；delay(1) 是有效下限，对 200Hz 采样/渲染无感。）
    delay(1);
}
