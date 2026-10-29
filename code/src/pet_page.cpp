//
// Created by Zhang Zhimeng on 2026/8/24.
//
#include "pet_page.h"

#include <cmath>
#include <ctime>

// ---- 颜色 ----
#define CAT     0xFD20      // 橙(身体)
#define PINK    0xFDB8      // 腮红/耳内/舌
#define NOSE    0xFC30      // 鼻(珊瑚粉)
#define EYE     0x18E3      // 眼/嘴(近黑)
#define WHISK   0xE71C      // 胡须(浅灰)
#define HEARTC  0xF98A      // 爱心
#define SHADOW  0x2104      // 地面阴影
#define MOUTH   EYE

// ---- 关键坐标（幼态比例：大圆头 + 眼睛又大又低又分开 + 矮胖身）----
#define FX   120           // 脸中心 x
#define FY   124           // 脸中心 y
#define FR   55            // 脸半径(大头)
#define ELX  98            // 左眼中心 x
#define ERX  142           // 右眼中心 x
#define EYY  128           // 双眼 y(偏低=萌)
#define EYR  14            // 眼半径(大眼)

// ---- 单眼状态 ----
#define EO_OPEN  0
#define EO_CLOSE 1
#define EO_BIG   2

#define DIV1_Y 28

// ================================ 生命周期 ================================
void PetPage::OnEnter() {
    DrawHeader();
    shown_time_ = "";
    shown_dot_ = app_ctx_.wifi_up_;
    app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    DrawClock();

    randomSeed(micros());
    for (int i = 0; i < 3; i++) hearts_[i].on_ = false;
    app_ctx_.tft_.fillEllipse(120, 224, 50, 7, SHADOW); // 脚下阴影
    DrawStaticCat();
    bool night = IsNight();
    shown_night_ = night ? 1 : 0;
    DrawTail(0);
    DrawEyes(night ? 1 : 0);
    gesture_ = 0;
    next_gesture_ms_ = millis() + 1500;
    last_frame_ms_ = last_heart_ms_ = millis();
}

void PetPage::Render() {
    if (shown_dot_ != static_cast<int>(app_ctx_.wifi_up_)) {
        shown_dot_ = app_ctx_.wifi_up_;
        app_ctx_.tft_.fillCircle(230, 11, 4, app_ctx_.wifi_up_ ? COL_ONLINE : COL_OFFLINE);
    }
    DrawClock();

    bool night = IsNight();
    uint32_t now = millis();

    if (shown_night_ != (night ? 1 : 0)) {
        // 白天/夜晚切换
        shown_night_ = night ? 1 : 0;
        gesture_ = 0;
        DrawEyes(night ? 1 : 0);
        if (night) ClearHearts();
        else {
            app_ctx_.tft_.fillRect(172, 42, 58, 54, COL_BG);
            DrawNoseMouth();
            next_gesture_ms_ = now + 1500;
        }
    }

    if (now - last_frame_ms_ >= 70) {
        // ~14fps：尾巴 + 爱心/Zzz
        last_frame_ms_ = now;
        tail_phase_++;
        DrawTail(tail_phase_);
        if (night) {
            if (tail_phase_ % 7 == 0) {
                zzz_phase_++;
                DrawZzz(zzz_phase_);
            }
        } else UpdateHearts();
    }

    if (night) return;

    // ---- 白天：随机小动作状态机 ----
    if (gesture_ == 0 && now >= next_gesture_ms_) {
        // 触发一个随机动作
        gesture_ = 1 + static_cast<int>(random(0, 6)); // 1..6
        g_start_ = now;
        g_phase_ = 0;
    }
    const uint32_t el = now - g_start_;
    switch (gesture_) {
        case 1: // 眨眼
            if (g_phase_ == 0) {
                DrawEyes(1);
                g_phase_ = 1;
            }
            if (el >= 150) {
                DrawEyes(0);
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        case 2: // 双眨
            if (g_phase_ == 0) {
                DrawEyes(1);
                g_phase_ = 1;
            } else if (g_phase_ == 1 && el >= 120) {
                DrawEyes(0);
                g_phase_ = 2;
            } else if (g_phase_ == 2 && el >= 260) {
                DrawEyes(1);
                g_phase_ = 3;
            } else if (g_phase_ == 3 && el >= 380) {
                DrawEyes(0);
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        case 3: // 挤左眼
            if (g_phase_ == 0) {
                DrawEyes(2);
                g_phase_ = 1;
            }
            if (el >= 450) {
                DrawEyes(0);
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        case 4: // 挤右眼
            if (g_phase_ == 0) {
                DrawEyes(3);
                g_phase_ = 1;
            }
            if (el >= 450) {
                DrawEyes(0);
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        case 5: // 喵~(张嘴)
            if (g_phase_ == 0) {
                DrawMeowMouth();
                g_phase_ = 1;
            }
            if (el >= 600) {
                DrawNoseMouth();
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        case 6: // 兴奋：大眼 + 爱心 + 快甩尾
            if (g_phase_ == 0) {
                DrawEyes(4);
                tail_fast_until_ = now + 1400;
                for (int k = 0; k < 3; k++) {
                    hearts_[k].on_ = true;
                    hearts_[k].x_ = 182 + k * 12;
                    hearts_[k].y_ = 108 - k * 12;
                }
                g_phase_ = 1;
            }
            if (el >= 1400) {
                DrawEyes(0);
                gesture_ = 0;
                next_gesture_ms_ = now + 2000 + random(0, 2500);
            }
            break;
        default: ;
    }
}

// ================================ 绘制 ================================
void PetPage::DrawHeader() const {
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.drawString("Pet", 10, 12, 2);
    app_ctx_.tft_.drawFastHLine(8, DIV1_Y, 224, COL_DIV);
}

void PetPage::DrawClock() {
    char b[8];
    if (AppContext::TimeReady()) {
        const time_t n = time(nullptr);
        struct tm t{};
        localtime_r(&n, &t);
        sprintf(b, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        strcpy(b, "--:--");
    }
    String s(b);
    if (s == shown_time_) return;
    shown_time_ = s;
    app_ctx_.tft_.fillRect(150, 2, 74, 18, COL_BG);
    app_ctx_.tft_.setTextDatum(MR_DATUM);
    app_ctx_.tft_.setTextColor(COL_TIME);
    app_ctx_.tft_.drawString(s, 212, 11, 2);
}

void PetPage::DrawStaticCat() const {
    TFT_eSPI &g = app_ctx_.tft_;
    // 身体：矮胖圆角矩形(抗锯齿)，比头略窄 -> 幼态
    g.fillSmoothRoundRect(74, 158, 92, 64, 30, CAT, COL_BG);
    // 前爪
    g.fillSmoothRoundRect(88, 204, 26, 18, 9, CAT, COL_BG);
    g.fillSmoothRoundRect(126, 204, 26, 18, 9, CAT, COL_BG);
    // 外耳(三角：基座深扎进头顶轮廓内 -> 被后画的头盖住耳根，与头无缝相连；顶端用小圆抹圆 -> 萌)
    g.fillTriangle(92, 46, 80, 90, 114, 88, CAT);
    g.fillTriangle(148, 46, 160, 90, 126, 88, CAT);
    g.fillSmoothCircle(92, 50, 6, CAT, COL_BG); // 圆润左耳尖
    g.fillSmoothCircle(148, 50, 6, CAT, COL_BG); // 圆润右耳尖
    // 大圆头(抗锯齿)。画在耳后，盖住深扎的耳根，只露圆润耳尖 -> 耳朵贴头
    g.fillSmoothCircle(FX, FY, FR, CAT, COL_BG);
    // 内耳(粉，基座同样藏进头里，只在露出的耳面上显出一小片)
    g.fillTriangle(92, 56, 84, 84, 108, 82, PINK);
    g.fillTriangle(148, 56, 156, 84, 132, 82, PINK);
    // 腮红(抗锯齿，大而软 -> 更萌)
    g.fillSmoothCircle(86, 150, 10, PINK, CAT);
    g.fillSmoothCircle(154, 150, 10, PINK, CAT);
    // 鼻 + 嘴
    DrawNoseMouth();
    // 胡须(抗锯齿细线；根部避开眨眼擦除圈)
    g.drawWideLine(78, 140, 58, 134, 2, WHISK, COL_BG);
    g.drawWideLine(78, 146, 56, 146, 2, WHISK, COL_BG);
    g.drawWideLine(78, 152, 58, 158, 2, WHISK, COL_BG);
    g.drawWideLine(162, 140, 182, 134, 2, WHISK, COL_BG);
    g.drawWideLine(162, 146, 184, 146, 2, WHISK, COL_BG);
    g.drawWideLine(162, 152, 182, 158, 2, WHISK, COL_BG);
}

void PetPage::DrawNoseMouth() const {
    TFT_eSPI &g = app_ctx_.tft_;
    g.fillSmoothCircle(120, 143, 10, CAT, CAT); // 擦鼻嘴区(脸色，够盖住喵时上移的鼻)
    g.fillTriangle(114, 137, 126, 137, 120, 144, NOSE);
    g.drawWideLine(120, 144, 112, 150, 2, MOUTH, CAT);
    g.drawWideLine(120, 144, 128, 150, 2, MOUTH, CAT);
}

void PetPage::DrawMeowMouth() const {
    TFT_eSPI &g = app_ctx_.tft_;
    g.fillSmoothCircle(120, 143, 10, CAT, CAT); // 擦鼻嘴区
    g.fillTriangle(114, 134, 126, 134, 120, 140, NOSE);
    g.fillSmoothCircle(120, 148, 5, EYE, CAT); // 张嘴(O)
    g.fillSmoothCircle(120, 150, 2, PINK, EYE); // 小舌头
}

void PetPage::DrawOneEye(const int cx, const int st) const {
    TFT_eSPI &g = app_ctx_.tft_;
    if (st == EO_CLOSE) {
        // 眯眼 ‿
        g.drawWideLine(static_cast<float>(cx - 9), EYY - 2, static_cast<float>(cx), EYY + 4, 3, EYE, CAT);
        g.drawWideLine(static_cast<float>(cx), EYY + 4, static_cast<float>(cx + 9), EYY - 2, 3, EYE, CAT);
        return;
    }

    const int r = (st == EO_BIG) ? 16 : EYR; // 亮晶晶大眼
    g.fillSmoothCircle(cx, EYY, r, EYE, CAT);
    g.fillSmoothCircle(cx + 4, EYY - 5, 5, TFT_WHITE, EYE); // 高光(大=水汪汪)
    g.fillSmoothCircle(cx - 4, EYY + 5, 2, TFT_WHITE, EYE); // 小闪点
    if (st == EO_BIG) {
        g.fillSmoothCircle(cx - 5, EYY - 6, 2, TFT_WHITE, EYE);
    }
}

void PetPage::DrawEyes(const int mode) const {
    TFT_eSPI &g = app_ctx_.tft_;
    g.fillSmoothCircle(ELX, EYY, EYR + 3, CAT, CAT); // 擦旧眼(半径够盖大眼)
    g.fillSmoothCircle(ERX, EYY, EYR + 3, CAT, CAT);
    int L = EO_OPEN, R = EO_OPEN;
    switch (mode) {
        case 1: L = R = EO_CLOSE;
            break;
        case 2: L = EO_CLOSE;
            R = EO_OPEN;
            break; // 挤左眼
        case 3: L = EO_OPEN;
            R = EO_CLOSE;
            break; // 挤右眼
        case 4: L = R = EO_BIG;
            break;
        default: break;
    }
    DrawOneEye(ELX, L);
    DrawOneEye(ERX, R);
}

// 尾巴：贴在身体右侧下方摆动。清除区在身体右缘(x>166)与头下方(y>=174)，不碰头/身
void PetPage::DrawTail(const int phase) const {
    TFT_eSPI &g = app_ctx_.tft_;
    g.fillRect(167, 174, 60, 48, COL_BG);
    const bool fast = millis() < tail_fast_until_; // 兴奋时甩得更快
    const float a = sinf(static_cast<float>(phase) * (fast ? 0.55f : 0.25f));
    const int tipx = 212 + static_cast<int>(a * 8);
    const int tipy = 188 + static_cast<int>(a * 8);
    for (int i = 0; i <= 12; i++) {
        // 二次贝塞尔粗尾巴(平滑圆点)
        const float t = static_cast<float>(i) / 12.0f;
        const int x = static_cast<int>((1 - t) * (1 - t) * 166 + 2 * (1 - t) * t * 206
                                       + t * t * static_cast<float>(tipx));
        const int y = static_cast<int>((1 - t) * (1 - t) * 206 + 2 * (1 - t) * t * 200
                                       + t * t * static_cast<float>(tipy));
        const int r = 6 - static_cast<int>(t * 1.5f); // 尾根粗、尾尖细
        g.fillSmoothCircle(x, y, r, CAT, COL_BG);
    }
}

void PetPage::DrawHeart(const int x, const int y, const uint16_t c) const {
    app_ctx_.tft_.fillSmoothCircle(x - 3, y, 3, c, COL_BG);
    app_ctx_.tft_.fillSmoothCircle(x + 3, y, 3, c, COL_BG);
    app_ctx_.tft_.fillTriangle(x - 6, y + 1, x + 6, y + 1, x, y + 8, c);
}

void PetPage::UpdateHearts() {
    if (millis() - last_heart_ms_ >= 2500) {
        // 生成新爱心
        last_heart_ms_ = millis();
        for (int i = 0; i < 3; i++)
            if (!hearts_[i].on_) {
                hearts_[i].on_ = true;
                hearts_[i].x_ = 192 + (int) random(-6, 7);
                hearts_[i].y_ = 108;
                break;
            }
    }

    for (int i = 0; i < 3; i++) {
        if (!hearts_[i].on_) continue;
        app_ctx_.tft_.fillRect(hearts_[i].x_ - 8, hearts_[i].y_ - 6, 18, 18, COL_BG); // 擦旧
        hearts_[i].y_ -= 2;
        if (hearts_[i].y_ < 46) {
            hearts_[i].on_ = false;
            continue;
        }
        DrawHeart(hearts_[i].x_, hearts_[i].y_, HEARTC);
    }
}

void PetPage::ClearHearts() {
    for (int i = 0; i < 3; i++) {
        hearts_[i].on_ = false;
    }

    app_ctx_.tft_.fillRect(180, 44, 46, 72, COL_BG);
}

void PetPage::DrawZzz(const int phase) const {
    app_ctx_.tft_.fillRect(172, 42, 58, 52, COL_BG);
    app_ctx_.tft_.setTextColor(COL_LABEL);
    app_ctx_.tft_.setTextDatum(ML_DATUM);
    const int p = phase % 4;
    if (p >= 1) {
        app_ctx_.tft_.drawString("z", 176, 84, 2);
    }

    if (p >= 2) {
        app_ctx_.tft_.drawString("z", 192, 68, 2);
    }

    if (p >= 3) {
        app_ctx_.tft_.drawString("Z", 208, 52, 4);
    }
}

bool PetPage::IsNight() {
    if (!AppContext::TimeReady()) return false;
    const time_t n = time(nullptr);
    tm t{};
    localtime_r(&n, &t);
    return t.tm_hour < 7 || t.tm_hour >= 22;
}
