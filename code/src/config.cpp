//
// Created by Zhang Zhimeng on 2026/8/27.
//
#include "config.h"
#include <LittleFS.h>

static auto CONFIG_PATH = "/config.json";

// 默认页面 id：顺序即默认显示顺序（与 main.cpp 里 addPage 的顺序一致）
const char *const Config::DEFAULT_PAGE_IDS[PAGE_COUNT] = {
    "weather", "stock", "home", "message", "system", "pet",
    "news", "pc", "clock", "gpu", "train", "overview"
};

// 体感映射的 JSON 键名（sw.faces / sw.gestures 下的子键）。
// 顺序必须与 FaceOrientation / GestureType 的枚举值-1 一一对应（见 config.h 注释），
// 同时要与前端 config_html.h 里的键名逐一对应。
static const char *const FACE_KEYS[FACE_COUNT] = {
    "up", "down", "left", "right", "top", "bottom" // FaceUp,FaceDown,Left,Right,Top,Bottom
};
static const char *const GESTURE_KEYS[GESTURE_COUNT] = {
    "swipeLeft", "swipeRight", "doubleTap", "shake" // SwipeLeft,SwipeRight,DoubleTap,Shake
};

Config &Config::Get() {
    static Config inst; // 首次调用时构造并填默认值（构造函数里调 loadDefaults）
    return inst;
}

// ================================ 默认值 ================================
void Config::LoadDefaults() {
    net_.ssid_ = "your-wifi-ssid";
    net_.pass_ = "your-wifi-password";
    net_.host_ = "infodisplay";

    weather_.key_ = "your-seniverse-api-key";
    weather_.loc_ = "beijing";

    stock_.symbols_ = "sh000001,sz399001,sz399006,sh600519,sz000858,sh601318";
    stock_.overview_ = "sh600519";
    stock_.index_count_ = 3;

    ha_.url_ = "http://192.168.1.20:8123";
    ha_.token_ = "your-long-lived-access-token";
    ha_.temp_ = "sensor.wenshiduji_temperature";
    ha_.humi_ = "sensor.wenshiduji_humidity";
    ha_.pm25_ = "sensor.airpurifier_pm2_5";
    ha_.fan_ = "fan.airpurifier";
    ha_.light_ = "light.keting";
    ha_.tv_ = "media_player.xiaomi_tv";
    ha_.other_ = "other";

    mqtt_.broker_ = "broker.emqx.io";
    mqtt_.topic_ = "st7789-inbox/CHANGE-ME-2f9k3x/msg";
    mqtt_.port_ = 1883;

    news_.feed_ = "https://openai.com/news/rss.xml";
    news_.source_ = "OpenAI";

    sys_.tz_offset_ = 28800; // 东八区 UTC+8
    sys_.backlight_ = true;

    for (int i = 0; i < PAGE_COUNT; i++) {
        pages_[i].id_ = DEFAULT_PAGE_IDS[i];
        pages_[i].en_ = true;
        pages_[i].dwell_s_ = 8;
    }

    sw.mode_ = "auto";
    sw.gesture_hold_s_ = 5; // 手势触发后默认停留 5s 再退回按朝向显示

    for (auto &i: sw.face_map_) {
        i = "";
    }
    for (auto &i: sw.gesture_map_) {
        i = "";
    }
}

// ================================ 读盘 ================================
// 成功返回 true；文件缺失/损坏/解析失败一律 loadDefaults() 并返回 false。
bool Config::Load() {
    LittleFS.begin(); // 幂等，防御性挂载（正常由调用方 begin 过）

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        LoadDefaults();
        return false;
    }

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LoadDefaults();
        return false;
    }

    LoadDefaults(); // 先铺默认值，再用 JSON 里出现的键覆盖（缺键即保持默认）
    FromJson(doc);
    return true;
}

// ================================ 写盘 ================================
bool Config::Save() const {
    LittleFS.begin(); // 幂等

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif
    ToJson(doc);

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        return false;
    }
    const size_t n = serializeJson(doc, f);
    f.close();
    return n > 0;
}

// ============================ 字段 -> JSON ============================
void Config::ToJson(JsonDocument &doc) const {
    const JsonObject n = doc["net"].to<JsonObject>();
    n["ssid"] = net_.ssid_;
    n["pass"] = net_.pass_;
    n["host"] = net_.host_;

    const auto w = doc["weather"].to<JsonObject>();
    w["key"] = weather_.key_;
    w["loc"] = weather_.loc_;

    const auto s = doc["stock"].to<JsonObject>();
    s["symbols"] = stock_.symbols_;
    s["overview"] = stock_.overview_;
    s["indexCount"] = stock_.index_count_;

    const JsonObject h = doc["ha"].to<JsonObject>();
    h["url"] = ha_.url_;
    h["token"] = ha_.token_;
    h["temp"] = ha_.temp_;
    h["humi"] = ha_.humi_;
    h["pm25"] = ha_.pm25_;
    h["fan"] = ha_.fan_;
    h["light"] = ha_.light_;
    h["tv"] = ha_.tv_;
    h["other"] = ha_.other_;

    const JsonObject m = doc["mqtt"].to<JsonObject>();
    m["broker"] = mqtt_.broker_;
    m["topic"] = mqtt_.topic_;
    m["port"] = mqtt_.port_;

    const JsonObject ne = doc["news"].to<JsonObject>();
    ne["feed"] = news_.feed_;
    ne["source"] = news_.source_;

    const JsonObject sy = doc["sys"].to<JsonObject>();
    sy["tzOffset"] = sys_.tz_offset_;
    sy["backlight"] = sys_.backlight_;

    // pages：有序对象数组 {"id","en","dwell"}
    const JsonArray parr = doc["pages"].to<JsonArray>();
    for (const auto &[id, en, dwellS]: pages_) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
        auto e = parr.add<JsonObject>();
#else
        JsonObject e = parr.createNestedObject();
#endif
        e["id"] = id;
        e["en"] = en;
        e["dwell"] = dwellS;
    }

    const auto swj = doc["sw"].to<JsonObject>();
    swj["mode"] = sw.mode_;
    swj["holdS"] = sw.gesture_hold_s_;

    // 体感映射用具名对象（比位置数组更自解释，也方便前端按键名绑定）
    const auto faces = swj["faces"].to<JsonObject>();
    for (int i = 0; i < FACE_COUNT; i++) {
        faces[FACE_KEYS[i]] = sw.face_map_[i];
    }
    const auto gestures = swj["gestures"].to<JsonObject>();
    for (int i = 0; i < GESTURE_COUNT; i++) {
        gestures[GESTURE_KEYS[i]] = sw.gesture_map_[i];
    }
}

// ============================ JSON -> 字段 ============================
// 只覆盖 JSON 里“出现且类型匹配”的键，缺键保持原值（防止部分 JSON 抹掉未指定字段）。
// 敏感字段 net.pass / ha.token：传入空串则保留原值（网页表单可省略密码而不清空）。
void Config::FromJson(JsonDocument &doc) {
    const auto n = doc["net"].as<JsonObjectConst>();
    if (n["ssid"].is<const char *>()) {
        net_.ssid_ = n["ssid"].as<String>();
    }

    if (n["pass"].is<const char *>()) {
        const auto v = n["pass"].as<String>();
        if (v.length()) {
            net_.pass_ = v;
        }
    }

    if (n["host"].is<const char *>()) {
        net_.host_ = n["host"].as<String>();
    }

    const auto w = doc["weather"].as<JsonObjectConst>();
    if (w["key"].is<const char *>()) {
        weather_.key_ = w["key"].as<String>();
    }

    if (w["loc"].is<const char *>()) {
        weather_.loc_ = w["loc"].as<String>();
    }

    const auto s = doc["stock"].as<JsonObjectConst>();
    if (s["symbols"].is<const char *>()) {
        stock_.symbols_ = s["symbols"].as<String>();
    }

    if (s["overview"].is<const char *>()) {
        stock_.overview_ = s["overview"].as<String>();
    }

    if (s["indexCount"].is<int>()) {
        stock_.index_count_ = s["indexCount"].as<int>();
    }

    const auto h = doc["ha"].as<JsonObjectConst>();

    if (h["url"].is<const char *>()) {
        ha_.url_ = h["url"].as<String>();
    }

    if (h["token"].is<const char *>()) {
        const auto v = h["token"].as<String>();
        if (v.length()) {
            ha_.token_ = v;
        }
    }

    if (h["temp"].is<const char *>()) {
        ha_.temp_ = h["temp"].as<String>();
    }

    if (h["humi"].is<const char *>()) {
        ha_.humi_ = h["humi"].as<String>();
    }

    if (h["pm25"].is<const char *>()) {
        ha_.pm25_ = h["pm25"].as<String>();
    }

    if (h["fan"].is<const char *>()) {
        ha_.fan_ = h["fan"].as<String>();
    }

    if (h["light"].is<const char *>()) {
        ha_.light_ = h["light"].as<String>();
    }

    if (h["tv"].is<const char *>()) {
        ha_.tv_ = h["tv"].as<String>();
    }

    if (h["other"].is<const char *>()) {
        ha_.other_ = h["other"].as<String>();
    }

    const auto m = doc["mqtt"].as<JsonObjectConst>();
    if (m["broker"].is<const char *>()) {
        mqtt_.broker_ = m["broker"].as<String>();
    }

    if (m["topic"].is<const char *>()) {
        mqtt_.topic_ = m["topic"].as<String>();
    }

    if (m["port"].is<int>()) {
        mqtt_.port_ = m["port"].as<int>();
    }

    const auto ne = doc["news"].as<JsonObjectConst>();
    if (ne["feed"].is<const char *>()) {
        news_.feed_ = ne["feed"].as<String>();
    }

    if (ne["source"].is<const char *>()) {
        news_.source_ = ne["source"].as<String>();
    }

    const auto sy = doc["sys"].as<JsonObjectConst>();
    if (sy["tzOffset"].is<int>()) {
        sys_.tz_offset_ = sy["tzOffset"].as<int>();
    }

    if (sy["backlight"].is<bool>()) {
        sys_.backlight_ = sy["backlight"].as<bool>();
    }

    // pages：按“收到的顺序”重建数组（支持网页拖动排序）。仅当 JSON 里带 pages 才重建，
    // 缺该键则保持原样（部分提交不会清空页面设置）。未知 id 跳过；未出现的已知页补到末尾
    // 并保留其当前 en/dwell，保证 12 页始终齐全。
    if (!doc["pages"].isNull()) {
        const auto parr = doc["pages"].as<JsonArrayConst>();
        PageCfg tmp[PAGE_COUNT];
        bool used[PAGE_COUNT] = {false};
        int num = 0;
        for (JsonObjectConst e: parr) {
            const char *id = e["id"];
            if (!id) continue;
            for (int k = 0; k < PAGE_COUNT; k++) {
                if (!used[k] && strcmp(id, DEFAULT_PAGE_IDS[k]) == 0) {
                    if (num < PAGE_COUNT) {
                        tmp[num].id_ = DEFAULT_PAGE_IDS[k];
                        tmp[num].en_ = e["en"].is<bool>() ? e["en"].as<bool>() : true;
                        tmp[num].dwell_s_ = e["dwell"].is<int>() ? e["dwell"].as<int>() : 8;
                        num++;
                    }
                    used[k] = true;
                    break;
                }
            }
        }
        // 补齐未出现的已知页（保留其当前 en/dwell），放到末尾
        for (int k = 0; k < PAGE_COUNT && num < PAGE_COUNT; k++) {
            if (used[k]) continue;
            bool en = true;
            int dw = 8;
            for (int i = 0; i < PAGE_COUNT; i++) {
                if (strcmp(pages_[i].id_, DEFAULT_PAGE_IDS[k]) == 0) {
                    en = pages_[i].en_;
                    dw = pages_[i].dwell_s_;
                    break;
                }
            }
            tmp[num].id_ = DEFAULT_PAGE_IDS[k];
            tmp[num].en_ = en;
            tmp[num].dwell_s_ = dw;
            num++;
        }
        for (int i = 0; i < PAGE_COUNT; i++) pages_[i] = tmp[i];
    }

    const auto swj = doc["sw"].as<JsonObjectConst>();
    if (swj["mode"].is<const char *>()) {
        sw.mode_ = swj["mode"].as<String>();
        if (sw.mode_ == "both") sw.mode_ = "orient"; // 历史兼容：旧配置 both≈orient，归一后各处只见 orient
    }

    if (swj["holdS"].is<int>()) {
        sw.gesture_hold_s_ = swj["holdS"].as<int>();
    }

    // 体感映射：具名对象，缺子键则保持原值（部分提交不会抹掉未列出的映射）。
    const auto faces = swj["faces"].as<JsonObjectConst>();
    for (int i = 0; i < FACE_COUNT; i++) {
        if (faces[FACE_KEYS[i]].is<const char *>()) {
            sw.face_map_[i] = faces[FACE_KEYS[i]].as<String>();
        }
    }
    const auto gestures = swj["gestures"].as<JsonObjectConst>();
    for (int i = 0; i < GESTURE_COUNT; i++) {
        if (gestures[GESTURE_KEYS[i]].is<const char *>()) {
            sw.gesture_map_[i] = gestures[GESTURE_KEYS[i]].as<String>();
        }
    }
}
