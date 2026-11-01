//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once

#include "page.h"
#include <ESP8266WebServer.h>

// 训练进度页（局域网直连）：训练脚本把进度 POST 到 /train
// body: {"name":"resnet50","status":"training","epoch":3,"epochs":100,
//        "step":1200,"steps":5000,"loss":0.0234,"acc":0.91,"eta":"1h23m"}
struct TrainConfig {
};

class TrainPage : public Page {
public:
    TrainPage(AppContext &ctx, const TrainConfig &cfg)
        : Page(ctx), train_cfg_(cfg) {
    }

    void OnEnter() override;

    void Render() override;

    [[nodiscard]] uint16_t DwellMs() const override { return 11000; }

    // 由 WebPortal 的 /train 路由调用（Task 5：数据端点并入 80 端口共用 server）
    void HandlePost(ESP8266WebServer &s);

private:
    void DrawHeader() const;

    void DrawClock();

    void DrawBody(); // 分发器：整区清屏只在等待态<->数据态切换时发生
    void DrawNameRow();

    void DrawEpochRow();

    void DrawBar();

    void DrawLossRow();

    void DrawFootRow();

    void DrawTrend();

    [[nodiscard]] int ProgressPct() const;

    // loss 历史：容量固定，满了原地抽稀（保留偶数下标 + 采样步长翻倍），
    // 因此无论训练多少个 epoch，占用恒为 sizeof(_lossHist)，且曲线始终覆盖全程
    void HistReset();

    void HistSample(int epoch, float v);

    TrainConfig train_cfg_;

    String name_, status_, eta_;
    int epoch_ = -1, epochs_ = -1;
    long step_ = -1, steps_ = -1;
    float loss_ = NAN, acc_ = NAN;

    bool valid_ = false, dirty_ = false, online_ = false;
    uint32_t last_recv_ms_ = 0;
    String shown_time_;
    int shown_dot_ = -1, shown_online_ = -1, shown_valid_ = -1;

    // 各元素「已绘制值」缓存：值没变就不重画（宽度缓存单调取大，用于决定擦多宽）
    String shown_name_, shown_status_, shown_eta_;
    int shown_epoch_ = -2, shown_epochs_ = -2, shown_pct_ = -2;
    long shown_step_ = -2, shown_steps_ = -2;
    float _shownLoss = 0, _shownAcc = 0;
    int _shownFillW = -1; // 进度条已填充宽度（-1=还没画过）
    int shown_name_w_ = 0, shown_status_w_ = 0, shown_epoch_w_ = 0, shown_pct_w_ = 0;
    int shown_loss_w_ = 0, shown_acc_w_ = 0, shown_eta_w_ = 0, shown_step_w_ = 0;

    static constexpr int LB = 110; // 曲线宽 220px → 2px/点；440 字节，编译期分配
    float loss_hist_[LB]{};
    int hist_count_ = 0;
    uint32_t stride_ = 1; // 每 _stride 个 epoch 留一点
    uint32_t since_keep_ = 0;
    int last_hist_epoch_ = -1; // 已记入历史的 epoch（同 epoch 多次上报只记一次）
    String hist_name_; // 任务名变了就当新一轮训练，清空历史
    uint16_t hist_rev_ = 0, shown_hist_rev_ = 0xFFFF;

    static constexpr uint32_t STALE_MS = 20000; // 训练一步可能久，放宽
};
