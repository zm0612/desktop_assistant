//
// Created by Zhang Zhimeng on 2026/8/27.
//
#include "web_portal.h"
#include "config.h"
#include "config_html.h"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Updater.h>           // 全局 Update：onProgress 进度回调 / isRunning 判定 OTA 结束

// 固件版本（/api/status 回报）。放在 .cpp 文件作用域：只有状态处理器用得到，
// 避免写进头文件后在每个包含它的翻译单元里生成一份未使用的 static 常量。
static auto FW_VERSION = "2026.08.27";

// ================================ 生命周期 ================================
void WebPortal::Begin(const bool apMode, const String &apSsid) {
    ap_mode_ = apMode;
    ap_ssid_ = apSsid;

    srv_.on("/api/status", HTTP_GET, [this] {
        HandleGetStatus();
    });

    srv_.on("/api/config", HTTP_GET, [this] {
        HandleGetConfig();
    });

    srv_.on("/api/config", HTTP_POST, [this] {
        HandlePostConfig();
    });

    // 首页：配置表单 UI（单文件、零外部依赖，存于 PROGMEM，用 send_P 直接吐出）
    srv_.on("/", HTTP_GET, [this] {
        srv_.send_P(200, "text/html; charset=utf-8", CONFIG_HTML);
    });

    if (ap_mode_) {
        dns_.start(53, "*", WiFi.softAPIP()); // 通配 DNS -> 本机（强制门户）
        srv_.onNotFound([this] {
            // 未知路径 302 到配置页
            srv_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
            srv_.send(302, "text/plain", "");
        });
    } else {
        srv_.onNotFound([this] { srv_.send(404, "text/plain", "not found"); });
    }
    ota_.setup(&srv_, "/update"); // 网页 OTA:GET /update 显示上传表单,POST /update 接收固件并刷写
    // OTA 进度桥接:Update 每写完一块就回调 -> 首块触发 onOtaStart(断 MQTT/暂停轮播/建屏),之后回报百分比
    Update.onProgress([this](const size_t cur, const size_t total) { HandleOtaProgress(cur, total); });
    srv_.begin();
}

void WebPortal::Handle() {
    if (ota_begun_ && !Update.isRunning()) {
        ota_begun_ = false;
    } // 上次 OTA 已结束(失败未重启)，复位以便下次重新清场

    if (ap_mode_) {
        dns_.processNextRequest();
    }

    srv_.handleClient();
}

// Update 每写完一块固件就回调这里（运行在被阻塞的上传过程中，主 loop 此刻不执行）。
// 首块时触发一次 onOtaStart 做清场；随后把百分比交给 onOtaProgress 画进度条。
void WebPortal::HandleOtaProgress(size_t cur, size_t total) {
    if (!ota_begun_) {
        ota_begun_ = true;
        if (ota_start_) {
            ota_start_();
        }
    }

    const int pct = total ? static_cast<int>(static_cast<uint64_t>(cur) * 100 / total) : 0;
    if (ota_prog_) {
        ota_prog_(pct);
    }
}

// 供 Pc/Gpu/Train 页注册数据接收端点（Task 5）。begin() 前后注册都有效；
// Task 5 会在 main.cpp 里于 begin() 之前调用本方法。
void WebPortal::OnData(const char *path, const PostFn &fn) {
    srv_.on(path, HTTP_POST, [this, fn] { fn(srv_); });
}

// ================================ GET /api/status ================================
// 运行状态快照：按真实联网模式回报 mode/ip/ssid。
void WebPortal::HandleGetStatus() {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif
    doc["mode"] = ap_mode_ ? "ap" : "sta";
    doc["ip"] = ap_mode_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["ssid"] = ap_mode_ ? ap_ssid_ : WiFi.SSID();
    doc["heap"] = EspClass::getFreeHeap();
    doc["version"] = FW_VERSION;
    doc["pages"] = PAGE_COUNT;

    String out;
    serializeJson(doc, out);
    srv_.send(200, "application/json", out);
}

// ================================ GET /api/config ================================
// 回吐当前配置，但把敏感字段抹空，避免密码/令牌泄露到浏览器。
// 与 Config::fromJson 的“空串不覆盖”规则配合：表单原样回传也不会清空原有密码/令牌。
void WebPortal::HandleGetConfig() {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    Config::Get().ToJson(doc);
    doc["net"]["pass"] = ""; // 抹掉 WiFi 密码
    doc["ha"]["token"] = ""; // 抹掉 HA 长期令牌

    String out;
    serializeJson(doc, out);
    srv_.send(200, "application/json", out);
}

// ================================ POST /api/config ================================
// 合并保存（缺键保持原值，空密码/令牌不覆盖）-> 写 /config.json -> 成功即重启，
// 让新配置从 flash 干净重载，同时避开已构造页面持有的悬空 .c_str() 指针。
void WebPortal::HandlePostConfig() {
    String body = srv_.arg("plain");
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    if (deserializeJson(doc, body)) {
        srv_.send(400, "application/json", R"({"ok":false,"err":"bad json"})");
        return;
    }

    Config::Get().FromJson(doc); // 合并到单例
    const bool ok = Config::Get().Save(); // 写 /config.json
    srv_.send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true}" : R"({"ok":false,"err":"save failed"})");

    if (ok) {
        delay(300);
        EspClass::restart();
    } // 等响应刷出再重启，新值生效
}
