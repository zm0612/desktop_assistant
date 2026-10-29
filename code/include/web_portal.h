//
// Created by Zhang Zhimeng on 2026/8/27.
//
#pragma once
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <functional>

// ============================ 网页配置门户 ============================
// 单个 80 端口的 ESP8266WebServer，集中提供配置读写 API：
//   GET  /api/status  运行状态（模式/IP/SSID/空闲堆/固件版本/页数）
//   GET  /api/config  当前配置（敏感字段 net.pass / ha.token 已抹空）
//   POST /api/config  提交配置 -> 合并保存 -> 重启生效
//   GET  /            占位首页（真正的表单 UI 在 Task 6 补上）
// 另开放 onData() 供 Pc/Gpu/Train 页在 Task 5 把数据端点并到本 80 端口。
// 本模块不挂载文件系统、不连 WiFi，只负责 HTTP；接线在 main.cpp。
// ====================================================================
class WebPortal {
public:
    void Begin(bool apMode, const String &apSsid); // 注册路由 + 启动 80 端口（AP 模式附带强制门户）

    void Handle(); // 主循环调用：处理客户端请求

    // 供 Pc/Gpu/Train 页注册数据接收端点（Task 5 用）；现在先实现好接口
    using PostFn = std::function<void(ESP8266WebServer &)>;

    void OnData(const char *path, const PostFn &fn);

    // OTA 期间的清场/进度回调（由 main.cpp 注入，桥到 PageManager/MessagePage）：
    //   onOtaStart 在收到首块固件时触发一次（断 MQTT、暂停轮播、清屏建进度条）
    //   onOtaProgress 每次进度更新触发（画进度条）。回调运行在被阻塞的上传过程中。
    using OtaStartFn = std::function<void()>;
    using OtaProgFn = std::function<void(int)>; // 参数：完成百分比 0..100
    void OnOtaStart(const OtaStartFn &fn) { ota_start_ = fn; }
    void OnOtaProgress(const OtaProgFn &fn) { ota_prog_ = fn; }

private:
    void HandleGetStatus();

    void HandleGetConfig();

    void HandlePostConfig();

    void HandleOtaProgress(size_t cur, size_t total); // Update.onProgress 适配器

    ESP8266WebServer srv_{80};
    ESP8266HTTPUpdateServer ota_{true}; // 网页 OTA(/update,复用 80 端口)；true=串口打印进度/错误，便于诊断卡顿
    DNSServer dns_; // AP 模式下的通配 DNS（强制门户）
    bool ap_mode_ = false; // 是否 AP 配网模式
    String ap_ssid_; // AP 模式热点名（/api/status 回报）

    OtaStartFn ota_start_; // 上传开始（首个进度回调）时触发一次
    OtaProgFn ota_prog_; // 每次进度更新触发
    bool ota_begun_ = false; // 本次上传是否已触发过 onOtaStart（结束后在 handle() 复位）
};
