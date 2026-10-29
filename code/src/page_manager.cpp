//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "page_manager.h"
#include "config.h"

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Updater.h>   // Update.isRunning()：OTA 结束(失败未重启)后恢复正常显示
#include <ctime>
#include <cstring>   // strcmp

#define BL_PIN 16   // 背光，低电平点亮

PageManager::PageManager(TFT_eSPI &tft) : app_ctx_{tft} {
}

void PageManager::AddPage(Page *p, const char *id) {
    if (count_ < MAX_PAGES) {
        pages_[count_] = p;
        ids_[count_] = id;
        count_++;
    }
}

// 切到轮播列表第 orderIdx 页：整屏清空 + onEnter + 重置停留计时
void PageManager::EnterOrder(const int orderIdx) {
    cur_order_ = orderIdx;
    last_switch_ = millis();
    app_ctx_.tft_.fillScreen(COL_BG);
    pages_[order_[cur_order_]]->OnEnter();
}

// 统一背光开关：低电平点亮（BL_PIN），并把引脚状态镜像到 backlight_on_，杜绝 desync。
void PageManager::SetBacklight(const bool on) {
    digitalWrite(BL_PIN, on ? LOW : HIGH);
    backlight_on_ = on;
}

int PageManager::GetScreenRotation() const {
    if (!motion_detector_) {
        return -1;
    }

    if (motion_detector_->Orientation().face == FaceOrientation::Unknown) {
        return -1;
    }

    if (motion_detector_->Orientation().face == FaceOrientation::Left) {
        return 1;
    }

    if (motion_detector_->Orientation().face == FaceOrientation::FaceDown) {
        return 2;
    }

    if (motion_detector_->Orientation().face == FaceOrientation::Right) {
        return 3;
    }

    if (motion_detector_->Orientation().face == FaceOrientation::FaceUp ||
        motion_detector_->Orientation().face == FaceOrientation::Bottom ||
        motion_detector_->Orientation().face == FaceOrientation::Top) {
        return 0;
    }

    return -1;
}

// 体感活动上报：未启用自动灭屏时直接返回（关背光/无传感器场景绝不接管引脚）；
// 否则刷新活动计时，若当前已灭屏则立即点亮（唤醒即为当前页，无需重绘）。
void PageManager::NotifyActivity() {
    if (!auto_dim_enabled_) {
        return;
    }

    last_activity_ms_ = millis();

    if (!backlight_on_) {
        SetBacklight(true);
    }
}

// 关闭自动灭屏并把背光恢复到 Config 设定：供 main 在检测不到 MPU 时调用，
// 避免灭屏后失去唯一唤醒源而永久黑屏。
void PageManager::DisableAutoDim() {
    auto_dim_enabled_ = false;
    SetBacklight(Config::Get().sys_.backlight_);
}

void PageManager::Begin() {
    const Config &C = Config::Get();

    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH); // 初始化期间关背光，避免闪屏

    // 联网决策：能连上路由器就走 STA + mDNS；空 SSID 或 15s 超时则退回 AP 配网
    const bool sta = TryConnectSTA();
    if (sta) {
        ap_mode_ = false;
        MDNS.begin(C.net_.host_.c_str()); // http://<host>.local/
        MDNS.addService("http", "tcp", 80);
    } else {
        StartAP(); // 置 _apMode=true / _apSsid，开放热点
    }

    // 各页上电初始化（所有注册页都要 begin，AP 模式下同样无害）
    for (int i = 0; i < count_; i++) {
        pages_[i]->Begin();
    }

    // AP 配网模式：只显示引导屏，点亮背光，跳过轮播构建
    if (ap_mode_) {
        DrawSetupScreen();
        digitalWrite(BL_PIN, LOW); // 低电平点亮，让引导屏可见
        return;
    }

    // 依据 Config.pages[] 构建“启用 + 顺序”后的轮播列表：
    // 按数组顺序取 en==true 的页，用 id 在注册表里 strcmp 匹配，记下注册下标与停留毫秒。
    order_count_ = 0;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (!C.pages_[i].en_) continue;
        for (int j = 0; j < count_; j++) {
            if (ids_[j] && strcmp(ids_[j], C.pages_[i].id_) == 0) {
                if (order_count_ < MAX_PAGES) {
                    order_[order_count_] = j;
                    dwell_ms_[order_count_] = static_cast<uint32_t>(C.pages_[i].dwell_s_) * 1000UL;
                    order_count_++;
                }
                break;
            }
        }
    }

    // 安全兜底：全禁用/都不匹配时，按注册顺序显示所有页（用各页自带 dwell），避免黑屏
    if (order_count_ == 0) {
        for (int j = 0; j < count_ && j < MAX_PAGES; j++) {
            order_[order_count_] = j;
            dwell_ms_[order_count_] = pages_[j]->DwellMs();
            order_count_++;
        }
    }

    // 翻页方式
    if (C.sw.mode_ == "orient") mode_ = SwitchMode::Orient;
    else mode_ = SwitchMode::Auto;

    // 进首个启用页
    if (order_count_ > 0) EnterOrder(0);

    // 背光：首屏画好后按 Config.sys.backlight 决定是否点亮（低电平点亮）
    SetBacklight(C.sys_.backlight_);
    // arm 自动灭屏：仅在配置开背光时启用。此处早于 MPU 初始化，若最终无传感器，
    // 由 main 的 DisableAutoDim() 收回（否则灭屏后无从唤醒）。
    auto_dim_enabled_ = C.sys_.backlight_;
    last_activity_ms_ = millis();
}

// 尝试连 WiFi：空 SSID 直接失败；否则阻塞轮询至多 ~15s，期间显示过场屏。
bool PageManager::TryConnectSTA() {
    Config &C = Config::Get();
    if (C.net_.ssid_.length() == 0) return false;
    WiFi.mode(WIFI_STA);
    // 常态开 modem-sleep：射频在 DTIM beacon 间隙休眠，平均电流大幅下降（需 loop 里 delay 让出 CPU）。
    // OTA 期间由 OtaBegin() 临时切回 WIFI_NONE_SLEEP，保证大流量上传全速不卡。
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
    WiFi.begin(C.net_.ssid_.c_str(), C.net_.pass_.c_str());
    last_wifi_try_ms_ = millis();
    DrawConnecting(); // 画一屏“连接中”
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

// 启动开放热点：SSID = InfoDisplay-XXXX（芯片 ID 低 16 位），无密码。
void PageManager::StartAP() {
    ap_mode_ = true;
    const uint32_t id = EspClass::getChipId();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(id & 0xFFFF));
    ap_ssid_ = String("InfoDisplay-") + suffix;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid_.c_str()); // 开放热点，无密码
}

// 连接中过场屏：居中提示 + 目标 SSID。点亮背光以便可见。
void PageManager::DrawConnecting() const {
    TFT_eSPI &tft = app_ctx_.tft_;
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TIME, COL_BG);
    tft.drawString("Connecting...", 120, 100, 4);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString(Config::Get().net_.ssid_, 120, 140, 2);
    digitalWrite(BL_PIN, LOW); // 低电平点亮，让过场屏可见
}

// AP 配网引导屏：热点名 + 192.168.4.1，竖向居中排布。
void PageManager::DrawSetupScreen() const {
    TFT_eSPI &tft = app_ctx_.tft_;
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);

    tft.setTextColor(COL_TIME, COL_BG);
    tft.drawString("WiFi Setup", 120, 40, 4);

    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("connect to hotspot", 120, 82, 2);

    tft.setTextColor(COL_DATE, COL_BG);
    tft.drawString(ap_ssid_, 120, 116, 4);

    tft.setTextColor(COL_LABEL, COL_BG);
    tft.drawString("then open in browser", 120, 158, 2);

    tft.setTextColor(COL_ONLINE, COL_BG);
    tft.drawString("192.168.4.1", 120, 194, 4);
}

// ================================ OTA 显示 ================================
// 由 WebPortal 的 Update 进度回调驱动（回调运行在被阻塞的上传过程中，此时主 loop
// 不会执行，所以这里直接画屏是安全且唯一能显示进度的途径）。
void PageManager::OtaBegin() {
    ota_mode_ = true;
    ota_shown_pct_ = -1;
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // OTA 全速：临时禁 modem-sleep，避免上传周期性卡顿
    SetBacklight(true); // 确保背光点亮（低电平点亮）
    last_activity_ms_ = millis(); // 重置活动计时：OTA 阻塞期不喂活动，退出后才有完整超时窗口
    TFT_eSPI &t = app_ctx_.tft_;
    t.fillScreen(COL_BG);
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_TIME, COL_BG);
    t.drawString("Firmware Update", 120, 74, 4);
    t.setTextColor(COL_LABEL, COL_BG);
    t.drawString("uploading... do NOT power off", 120, 104, 2);
    t.drawRoundRect(20, 132, 200, 22, 4, COL_DIV); // 进度条外框（内部随进度填充）
}

void PageManager::OtaProgress(int pct) {
    if (!ota_mode_) OtaBegin(); // 兜底：万一未初始化，先建屏
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == ota_shown_pct_) return; // 节流：整数百分比变化才重绘
    ota_shown_pct_ = pct;
    TFT_eSPI &t = app_ctx_.tft_;
    int x = 23, y = 135, w = 194, h = 16; // 落在外框内侧；进度单调递增，只需填充已完成段
    t.fillRect(x, y, w * pct / 100, h, COL_DATE);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", pct);
    t.fillRect(96, 168, 48, 26, COL_BG); // 清旧百分比再画
    t.setTextDatum(MC_DATUM);
    t.setTextColor(COL_TIME, COL_BG);
    t.drawString(b, 120, 180, 4);
}

void PageManager::HandleWifi() {
    Config &C = Config::Get();
    bool up = (WiFi.status() == WL_CONNECTED);
    if (up && !wifi_up_) {
        // 刚连上：校时（时区偏移来自 Config）
        wifi_up_ = true;
        configTime(C.sys_.tz_offset_, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
    } else if (!up && wifi_up_) {
        wifi_up_ = false;
    }
    app_ctx_.wifi_up_ = wifi_up_;

    if (!up && millis() - last_wifi_try_ms_ >= WIFI_RETRY_INTERVAL) {
        last_wifi_try_ms_ = millis();
        WiFi.begin(C.net_.ssid_.c_str(), C.net_.pass_.c_str());
    }
}

// 立即切到下一启用页
void PageManager::NextPage() {
    if (order_count_ == 0) {
        return;
    }

    EnterOrder((cur_order_ + 1) % order_count_);
}

// 立即切到上一启用页
void PageManager::PrevPage() {
    if (order_count_ == 0) {
        return;
    }

    EnterOrder((cur_order_ + order_count_ - 1) % order_count_);
}

// 跳到启用顺序中的第 orderIdx 页（越界则夹取），与当前相同则不动
void PageManager::GotoPage(int orderIdx) {
    if (order_count_ == 0) {
        return;
    }

    if (orderIdx < 0) {
        orderIdx = 0;
    }

    if (orderIdx >= order_count_) {
        orderIdx = order_count_ - 1;
    }

    if (orderIdx == cur_order_) {
        return;
    }

    EnterOrder(orderIdx);
}

// 页面 id -> 启用轮播顺序下标；未启用/不存在返回 -1。
int PageManager::OrderIndexOfId(const char *id) const {
    if (!id || !id[0]) return -1;
    for (int k = 0; k < order_count_; k++) {
        const char *pid = ids_[order_[k]];
        if (pid && strcmp(pid, id) == 0) return k;
    }
    return -1;
}

void PageManager::Loop() {
    if (ota_mode_) {
        // OTA 升级中/刚结束
        if (Update.isRunning()) return; // 仍在写 flash（防御性；上传时 loop 本就被阻塞）
        // 走到这说明升级已结束却没重启 => 失败：退出 OTA 模式，恢复正常显示
        ota_mode_ = false;
        ota_shown_pct_ = -1;
        WiFi.setSleepMode(WIFI_MODEM_SLEEP); // 恢复常态 modem-sleep（OTA 失败未重启时不至于停在 NONE_SLEEP）
        last_activity_ms_ = millis(); // 退出 OTA 重置计时：无论上传耗时多久，恢复显示后都有完整超时窗口
        if (ap_mode_) {
            DrawSetupScreen();
        } else if (order_count_ > 0) {
            EnterOrder(cur_order_);
        }
    }
    if (ap_mode_) {
        return;
    } // AP 配网模式：交给门户处理，无轮播/校时

    HandleWifi();
    MDNS.update(); // 维持 <host>.local 解析

    // 背光自动熄灭：启用且当前亮屏、且静止无活动已超时 -> 灭背光省电。
    // 位于 ap_mode_/ota_mode_ 提前 return 之后，故只在正常轮播态生效；活动由 main 经 NotifyActivity() 上报。
    if (auto_dim_enabled_ && backlight_on_ &&
        millis() - last_activity_ms_ >= BACKLIGHT_TIMEOUT_MS) {
        SetBacklight(false); // 只关背光，不发 sleep-in，显存内容保留
    }

    if (order_count_ == 0) {
        return;
    }

    // 后台：所有注册页都跑（维持 MQTT/HTTP 等），与是否当前页无关
    for (int i = 0; i < count_; i++) {
        pages_[i]->BackgroundTick();
    }

    // 体感翻页（Orient）：查询路由器。
    //   返回有效下标(>=0)=“体感正在指定某页”（朝向锁定 / 手势停留中）：暂停轮播，并按需切换；
    //   返回 -1 = 体感无意见 -> 交给下面的定时轮播兜底。
    bool motion_active = false;
    if (mode_ == SwitchMode::Orient && orient_) {
        const int idx = orient_->OrientationPage();
        if (idx >= 0 && idx < order_count_) {
            motion_active = true;
            if (idx != cur_order_) {
                const int rotation = GetScreenRotation();
                if (rotation != -1) {
                    app_ctx_.tft_.setRotation(static_cast<std::uint8_t>(rotation));
                    GotoPage(idx);
                } else {
                    Serial.println(F("invalid screen rotation"));
                }
            }
        }
    }

    // 定时轮播：体感未在指定页面时，按停留时间轮播下一启用页。
    //   - auto 模式：恒不 motion_active，等价于纯轮播（与原行为一致）。
    //   - orient 模式：填补“朝向未映射 / 手势停留结束 / 传感器缺失”的空档，从而永不卡死单页；
    //     一旦当前朝向命中映射，即被 motion_active 锁定并停止轮播。
    if (!motion_active && order_count_ > 1 && millis() - last_switch_ >= dwell_ms_[cur_order_]) {
        app_ctx_.tft_.setRotation(0);
        EnterOrder((cur_order_ + 1) % order_count_);
    }

    Page *cur = pages_[order_[cur_order_]];
    cur->Tick();
    cur->Render();
}
