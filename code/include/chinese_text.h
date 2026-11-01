//
// Created by Zhang Zhimeng on 2026/8/25.
//
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// 任意中文渲染：16px 点阵，字库在 LittleFS 的 /cnfont.bin（由 tools/make_cnfont.py 生成）。
// - 懒加载：首次调用自动挂载 LittleFS 并打开字库，无需改 setup()。
// - 混排：ASCII 用内置 Font2，汉字/中文标点用点阵。
// - bg = -1 表示透明（只画前景像素）；否则用该 16bit 颜色做不透明底。
// 用法示例：CnText::drawCentered(tft, 120, 100, "多云 25度", COL_TIME);
namespace CnText {
    bool Begin();

    bool Ready();

    int Height(); // 字库原生高度(16/24/32...)

    int Width(TFT_eSPI &tft, const char *utf8, int scale = 1);

    void Draw(TFT_eSPI &tft, int x, int y, const char *utf8, uint16_t fg, int bg = -1, int scale = 1);

    void DrawCentered(TFT_eSPI &tft, int cx, int y, const char *utf8, uint16_t fg, int bg = -1, int scale = 1);
}
