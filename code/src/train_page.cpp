//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "train_page.h"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ctime>
#include <cmath>

// ---- 布局坐标（240x240）----
#define TR_DIV1      28
#define TR_NAME_Y    42     // font2 行中心：任务名(左) / 状态(右)
#define TR_EPOCH_Y   62     // font2 行中心：EPOCH x/y(左) / 百分比(右)
#define TR_BAR_X     12
#define TR_BAR_Y     74
#define TR_BAR_W     216
#define TR_BAR_H     14
#define TR_LOSS_Y    104    // font4 行中心：LOSS 值(左) / ACC(右)
#define TR_FOOT_Y    130    // font4 行中心：ETA(左) / step(右)
#define TR_DIV2      146
#define TR_TREND_Y   158    // font2 行中心：曲线标题(左) / 值域(右)
#define TR_SPARK_X   10
#define TR_SPARK_TOP 170
#define TR_SPARK_W   220
#define TR_SPARK_H   64     // 基线画在 TR_SPARK_TOP + TR_SPARK_H = 234

static constexpr float CACHE_NONE = -1e30f; // 缓存哨兵：与任何真实值都不相等，用于强制首绘

static bool SameF(const float a, const float b) {
    return (isnan(a) && isnan(b)) || a == b;
}

// loss 量级跨度大（2.5 → 0.0001 都常见），按大小自动选小数位
static void FmtLoss(char *b, const size_t n, const float v) {
    if (isnan(v)) {
        snprintf(b, n, "--");
    } else if (fabsf(v) >= 100) {
        snprintf(b, n, "%.1f", v);
    } else if (fabsf(v) >= 1) {
        snprintf(b, n, "%.3f", v);
    } else {
        snprintf(b, n, "%.4f", v);
    }
}

// 超宽文本截断加省略号，避免左右两栏互相踩到对方的擦除区
static String FitText(TFT_eSPI &tft, String s, const int max_w, uint8_t font) {
    if (tft.textWidth(s, font) <= max_w) {
        return s;
    }

    while (s.length() > 1 && tft.textWidth(s + "..", font) > max_w) {
        s.remove(s.length() - 1);
    }

    return s + "..";
}

// ================================ 生命周期 ================================
void TrainPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    shown_online_ = -1;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    DrawClock();
    shown_valid_ = -1; // 强制 drawBody 整区重绘一次（进页时屏已清空，重绘不闪）
    DrawBody();
}

void TrainPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }

    DrawClock();

    const bool on = valid_ && (millis() - last_recv_ms_ < STALE_MS);
    if (shown_online_ != static_cast<int>(on)) {
        dirty_ = true;
    }

    if (dirty_) {
        dirty_ = false;
        shown_online_ = on;
        online_ = on;
        DrawBody();
    }
}

// ================================ 接收 ================================
// 由 WebPortal 的 /train 路由调用，传入共用的 80 端口 server（Task 5）
void TrainPage::HandlePost(ESP8266WebServer &s) {
    String body = s.arg("plain");
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(768);
#endif
    if (deserializeJson(doc, body)) {
        s.send(400, "text/plain", "bad json");
        return;
    }

    const int prev_epoch = epoch_; // 采样点是「epoch 翻页」，需要翻页前的旧值
    const float prev_loss = loss_;

    if (!doc["name"].isNull()) name_ = doc["name"].as<String>();
    if (!doc["status"].isNull()) status_ = doc["status"].as<String>();
    if (!doc["eta"].isNull()) eta_ = doc["eta"].as<String>();
    if (!doc["epoch"].isNull()) epoch_ = doc["epoch"].as<int>();
    if (!doc["epochs"].isNull()) epochs_ = doc["epochs"].as<int>();
    if (!doc["step"].isNull()) step_ = doc["step"].as<long>();
    if (!doc["steps"].isNull()) steps_ = doc["steps"].as<long>();
    if (!doc["loss"].isNull()) loss_ = doc["loss"].as<float>();
    if (!doc["acc"].isNull()) acc_ = doc["acc"].as<float>();

    // 换任务名或 epoch 回退 → 视为新一轮训练，历史清空重来
    if (name_ != hist_name_ || (prev_epoch >= 0 && epoch_ < prev_epoch)) {
        HistReset();
        hist_name_ = name_;
    }
    // epoch 翻页时记下「上个 epoch 最后一次上报的 loss」
    if (epoch_ != prev_epoch) {
        HistSample(prev_epoch, prev_loss);
    }

    // 训练结束不会再翻页，补记最后一个 epoch
    if (status_ == "done") {
        HistSample(epoch_, loss_);
    }

    valid_ = true;
    dirty_ = true;
    last_recv_ms_ = millis();
    s.send(200, "text/plain", "ok");
}

// ================================ loss 历史（固定容量 + 抽稀） ================================
void TrainPage::HistReset() {
    hist_count_ = 0;
    stride_ = 1;
    since_keep_ = 0;
    last_hist_epoch_ = -1;
    hist_rev_++;
}

void TrainPage::HistSample(const int epoch, const float v) {
    if (epoch < 0 || isnan(v) || epoch == last_hist_epoch_) return;
    last_hist_epoch_ = epoch;
    if (++since_keep_ < stride_) return; // 抽稀：每 _stride 个 epoch 只留一点
    since_keep_ = 0;
    if (hist_count_ >= LB) {
        // 存满：原地保留偶数下标，步长翻倍
        for (int i = 0; i < LB / 2; i++) loss_hist_[i] = loss_hist_[i * 2];
        hist_count_ = LB / 2;
        stride_ *= 2;
    }
    loss_hist_[hist_count_++] = v;
    hist_rev_++;
}

// ================================ 渲染 ================================
void TrainPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("TRAIN", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, TR_DIV1, 224, COL_DIV);
}

void TrainPage::DrawClock() {
    char b[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        tm t;
        localtime_r(&n, &t);
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(b, "--:--");
    }
    const String s(b);
    if (s == shown_time_) return;
    shown_time_ = s;
    app_ctx_.tft_.fillRect(150, 2, 74, 18, COL_BG);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, 212, 11, 2);
}

int TrainPage::ProgressPct() const {
    float base = 0;
    if (epochs_ > 0) {
        base = static_cast<float>(epoch_) / static_cast<float>(epochs_);
        if (steps_ > 0) {
            base += static_cast<float>(step_) / static_cast<float>(steps_) / static_cast<float>(epochs_);
        }
    }

    int pct = static_cast<int>(base * 100 + 0.5f);
    if (pct < 0) {
        pct = 0;
    }

    if (pct > 100) {
        pct = 100;
    }

    return pct;
}

// 整区 fillRect 清屏只在「进页 / 等待态<->数据态切换」发生（每次仅一次，不构成持续闪烁）；
// 平时的数据刷新由 render()->drawBody() 分发到各元素做局部原位重画。
void TrainPage::DrawBody() {
    if (shown_valid_ != static_cast<int>(valid_)) {
        shown_valid_ = valid_;
        app_ctx_.tft_.fillRect(0, TR_DIV1 + 2, 240, 240 - (TR_DIV1 + 2), COL_BG);
        if (!valid_) {
            app_ctx_.tft_.setTextDatum(MC_DATUM);
            app_ctx_.tft_.setTextColor(COL_LABEL);
            app_ctx_.tft_.drawString("Waiting for job...", 120, 100, 4);
            app_ctx_.tft_.setTextColor(COL_DIV);
            const String url = "http://" + WiFi.localIP().toString() + "/train";
            app_ctx_.tft_.drawString("POST to", 120, 134, 2);
            app_ctx_.tft_.setTextColor(COL_DATE);
            app_ctx_.tft_.drawString(url, 120, 152, 2);
            return;
        }

        // 静态元素（进度条边框 / 分隔线 / LOSS、ETA 标签）只画一次
        app_ctx_.tft_.drawRoundRect(TR_BAR_X, TR_BAR_Y, TR_BAR_W, TR_BAR_H, 3, COL_DIV);
        app_ctx_.tft_.drawFastHLine(8, TR_DIV2, 224, COL_DIV);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString("LOSS", 12, TR_LOSS_Y, 4);
        app_ctx_.tft_.drawString("ETA", 12, TR_FOOT_Y, 4);
        // 重置缓存，让下面的逐项补画把数据区画全
        shown_name_ = shown_status_ = shown_eta_ = "";
        shown_epoch_ = shown_epochs_ = shown_pct_ = -2;
        shown_step_ = shown_steps_ = -2;
        _shownLoss = _shownAcc = CACHE_NONE;
        _shownFillW = -1;
        shown_name_w_ = shown_status_w_ = shown_epoch_w_ = shown_pct_w_ = 0;
        shown_loss_w_ = shown_acc_w_ = shown_eta_w_ = shown_step_w_ = 0;
        shown_hist_rev_ = static_cast<uint16_t>(hist_rev_ - 1);
    }

    if (!valid_) {
        return;
    }

    DrawNameRow();
    DrawEpochRow();
    DrawBar();
    DrawLossRow();
    DrawFootRow();
    DrawTrend();
}

void TrainPage::DrawNameRow() {
    const String nm = FitText(app_ctx_.tft_, name_.length() ? name_ : String("job"), 130, 2);
    if (nm != shown_name_) {
        shown_name_ = nm;
        const int nw = app_ctx_.tft_.textWidth(nm, 2);
        const int w = nw > shown_name_w_ ? nw : shown_name_w_;
        shown_name_w_ = w;
        app_ctx_.tft_.fillRect(10, TR_NAME_Y - 8, w + 2, 16, COL_BG);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_TIME);
        app_ctx_.tft_.drawString(nm, 12, TR_NAME_Y, 2);
    }

    String st = online_ ? (status_.length() ? status_ : String("training")) : String("offline");
    if (st != shown_status_) {
        shown_status_ = st;
        const uint16_t sc = !online_
                                ? COL_OFFLINE
                                : status_ == "done"
                                      ? COL_ONLINE
                                      : (status_ == "training" || status_.length() == 0)
                                            ? COL_DATE
                                            : COL_LABEL;
        const int nw = app_ctx_.tft_.textWidth(st, 2);
        int w = nw > shown_status_w_ ? nw : shown_status_w_;
        shown_status_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 1, TR_NAME_Y - 8, w + 2, 16, COL_BG);
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(sc);
        app_ctx_.tft_.drawString(st, 230, TR_NAME_Y, 2);
    }
}

void TrainPage::DrawEpochRow() {
    if (epoch_ != shown_epoch_ || epochs_ != shown_epochs_) {
        shown_epoch_ = epoch_;
        shown_epochs_ = epochs_;
        char eb[24];
        if (epochs_ > 0) {
            snprintf(eb, sizeof(eb), "EPOCH %d / %d", epoch_, epochs_);
        } else if (epoch_ >= 0) {
            snprintf(eb, sizeof(eb), "EPOCH %d", epoch_);
        } else {
            snprintf(eb, sizeof(eb), "EPOCH --");
        }

        const int nw = app_ctx_.tft_.textWidth(eb, 2);
        const int w = nw > shown_epoch_w_ ? nw : shown_epoch_w_;
        shown_epoch_w_ = w;
        app_ctx_.tft_.fillRect(10, TR_EPOCH_Y - 8, w + 2, 16, COL_BG);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(COL_LABEL);
        app_ctx_.tft_.drawString(eb, 12, TR_EPOCH_Y, 2);
    }

    const int pct = ProgressPct();
    if (pct != shown_pct_) {
        shown_pct_ = pct;
        char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        const int nw = app_ctx_.tft_.textWidth(pb, 2);
        const int w = nw > shown_pct_w_ ? nw : shown_pct_w_;
        shown_pct_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 1, TR_EPOCH_Y - 8, w + 2, 16, COL_BG);
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(COL_TIME);
        app_ctx_.tft_.drawString(pb, 230, TR_EPOCH_Y, 2);
    }
}

// 进度条不叠任何文字，于是可以纯增量：前进只补画增量段、回退只擦多出来的段（完全不闪）
void TrainPage::DrawBar() {
    const int fw = (TR_BAR_W - 2) * ProgressPct() / 100;
    if (fw == _shownFillW) {
        return;
    }

    constexpr int ix = TR_BAR_X + 1;
    constexpr int iy = TR_BAR_Y + 1;
    constexpr int ih = TR_BAR_H - 2;
    if (_shownFillW < 0) {
        app_ctx_.tft_.fillRect(ix, iy, TR_BAR_W - 2, ih, COL_BG);
        if (fw > 0) {
            app_ctx_.tft_.fillRect(ix, iy, fw, ih, COL_DATE);
        }
    } else if (fw > _shownFillW) {
        app_ctx_.tft_.fillRect(ix + _shownFillW, iy, fw - _shownFillW, ih, COL_DATE);
    } else {
        app_ctx_.tft_.fillRect(ix + fw, iy, _shownFillW - fw, ih, COL_BG);
    }
    _shownFillW = fw;
}

void TrainPage::DrawLossRow() {
    if (!SameF(loss_, _shownLoss)) {
        _shownLoss = loss_;
        char lb[16];
        FmtLoss(lb, sizeof(lb), loss_);
        const int vx = 12 + app_ctx_.tft_.textWidth("LOSS", 4) + 10; // 标签是静态的，值紧跟其后
        const int nw = app_ctx_.tft_.textWidth(lb, 4);
        const int w = nw > shown_loss_w_ ? nw : shown_loss_w_;
        shown_loss_w_ = w;
        app_ctx_.tft_.fillRect(vx, TR_LOSS_Y - 13, w + 2, 26, COL_BG);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(isnan(loss_) ? COL_LABEL : COL_TEMP);
        app_ctx_.tft_.drawString(lb, vx, TR_LOSS_Y, 4);
    }

    // ACC 用 font2：font4 的话「LOSS + 数值 + ACC 91%」合计约 247px，240 宽塞不下
    if (!SameF(acc_, _shownAcc)) {
        _shownAcc = acc_;
        char ab[16];
        if (isnan(acc_)) {
            ab[0] = 0;
        } else {
            snprintf(ab, sizeof(ab), "ACC %d%%", static_cast<int>(acc_ * 100 + 0.5f));
        }

        const int nw = ab[0] ? app_ctx_.tft_.textWidth(ab, 2) : 0;
        const int w = nw > shown_acc_w_ ? nw : shown_acc_w_;
        shown_acc_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 1, TR_LOSS_Y - 8, w + 2, 16, COL_BG);
        if (ab[0]) {
            app_ctx_.tft_.setTextDatum(MR_DATUM);
            app_ctx_.tft_.setTextColor(COL_ONLINE);
            app_ctx_.tft_.drawString(ab, 230, TR_LOSS_Y, 2);
        }
    }
}

void TrainPage::DrawFootRow() {
    const String eta = FitText(app_ctx_.tft_, eta_.length() ? eta_ : String("--"), 90, 4);
    if (eta != shown_eta_) {
        shown_eta_ = eta;
        const int vx = 12 + app_ctx_.tft_.textWidth("ETA", 4) + 10;
        const int nw = app_ctx_.tft_.textWidth(eta, 4);
        const int w = nw > shown_eta_w_ ? nw : shown_eta_w_;
        shown_eta_w_ = w;
        app_ctx_.tft_.fillRect(vx, TR_FOOT_Y - 13, w + 2, 26, COL_BG);
        app_ctx_.tft_.setTextDatum(ML_DATUM);
        app_ctx_.tft_.setTextColor(eta_.length() ? COL_TIME : COL_LABEL);
        app_ctx_.tft_.drawString(eta, vx, TR_FOOT_Y, 4);
    }

    if (step_ != shown_step_ || steps_ != shown_steps_) {
        shown_step_ = step_;
        shown_steps_ = steps_;
        char sb[24];
        if (steps_ > 0) snprintf(sb, sizeof(sb), "%ld/%ld", step_, steps_);
        else sb[0] = 0;
        const int nw = sb[0] ? app_ctx_.tft_.textWidth(sb, 2) : 0;
        const int w = nw > shown_step_w_ ? nw : shown_step_w_;
        shown_step_w_ = w;
        app_ctx_.tft_.fillRect(230 - w - 1, TR_FOOT_Y - 8, w + 2, 16, COL_BG);
        if (sb[0]) {
            app_ctx_.tft_.setTextDatum(MR_DATUM);
            app_ctx_.tft_.setTextColor(COL_LABEL);
            app_ctx_.tft_.drawString(sb, 230, TR_FOOT_Y, 2);
        }
    }
}

// loss 趋势：每 epoch 最多重画一次（_histRev 变了才画），不参与高频刷新
void TrainPage::DrawTrend() {
    if (hist_rev_ == shown_hist_rev_) {
        return;
    }

    shown_hist_rev_ = hist_rev_;

    constexpr int x = TR_SPARK_X, y = TR_SPARK_TOP, w = TR_SPARK_W, h = TR_SPARK_H;
    app_ctx_.tft_.fillRect(x, y, w, h + 1, COL_BG);
    app_ctx_.tft_.drawFastHLine(x, y + h, w, COL_DIV);
    app_ctx_.tft_.fillRect(8, TR_TREND_Y - 8, 224, 16, COL_BG);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);

    if (hist_count_ < 2) {
        app_ctx_.tft_.drawString("LOSS TREND", 12, TR_TREND_Y, 2);
        app_ctx_.tft_.setTextDatum(MC_DATUM);
        app_ctx_.tft_.setTextColor(COL_DIV);
        app_ctx_.tft_.drawString("waiting for epochs", 120, y + h / 2, 2);
        return;
    }

    float mn = loss_hist_[0], mx = loss_hist_[0];
    for (int i = 1; i < hist_count_; i++) {
        if (loss_hist_[i] < mn) mn = loss_hist_[i];
        if (loss_hist_[i] > mx) mx = loss_hist_[i];
    }
    // loss 常跨数量级（2.5→0.02），线性纵轴会让后期整条曲线贴底；此时自动换对数轴
    const bool use_log = (mn > 0) && (mx / mn >= 10.0f);
    const float lo = use_log ? log10f(mn) : mn;
    float hi = use_log ? log10f(mx) : mx;
    if (hi - lo < 1e-9f) hi = lo + 1e-9f;

    char lbl[32];
    if (stride_ > 1) {
        snprintf(lbl, sizeof(lbl), "LOSS TREND /%u%s",
                 static_cast<unsigned>(stride_), use_log ? " LOG" : "");
    } else {
        snprintf(lbl, sizeof(lbl), "LOSS TREND%s", use_log ? " LOG" : "");
    }

    const int lw = app_ctx_.tft_.textWidth(lbl, 2);
    app_ctx_.tft_.drawString(lbl, 12, TR_TREND_Y, 2);

    char rb[32], nb[16], xb[16];
    FmtLoss(nb, sizeof(nb), mn);
    FmtLoss(xb, sizeof(xb), mx);
    snprintf(rb, sizeof(rb), "%s~%s", nb, xb);
    if (12 + lw + 6 < 230 - app_ctx_.tft_.textWidth(rb, 2)) {
        app_ctx_.tft_.setTextDatum(MR_DATUM);
        app_ctx_.tft_.setTextColor(COL_DIV);
        app_ctx_.tft_.drawString(rb, 230, TR_TREND_Y, 2);
    }

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < hist_count_; i++) {
        const float v = use_log ? log10f(loss_hist_[i]) : loss_hist_[i];
        const int px = x + (w - 1) * i / (hist_count_ - 1);
        const int py = y + h - 1 - static_cast<int>((v - lo) * (h - 2) / (hi - lo));
        if (prev_x >= 0) {
            app_ctx_.tft_.drawLine(prev_x, prev_y, px, py, COL_TEMP);
        }

        prev_x = px;
        prev_y = py;
    }
}
