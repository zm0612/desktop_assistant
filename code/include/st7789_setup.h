//
// Created by Zhang Zhimeng on 2026/8/22.
//
#pragma once

#define USER_SETUP_LOADED

// ================= 驱动 & 分辨率 =================
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ================= 控制引脚 =================
// SPI 数据/时钟在 ESP8266 上是硬件固定脚，不用配
// 注意: SPI这里使用的是默认引脚，不建议修改。
// SCLK=GPIO14(SCL)  MOSI=GPIO13(SDA)  MISO=GPIO12(悬空不接)
#define TFT_MISO  (-1)
#define TFT_CS   (-1)   // CS 直接接地 -> -1
#define TFT_DC    2   // GPIO2  -> D/C
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_RST  15   // GPIO15 -> RST

// ================= 面板选项 =================
#define TFT_INVERSION_ON        // 多数 240x240 ST7789 IPS 需要反显；若颜色反了就注释掉

// ST7789 在 ESP8266 上必须用 MODE3（TFT_eSPI 对 ST7789 本身也是默认 MODE3；这里显式写出以作说明）
#define TFT_SPI_MODE SPI_MODE3

// ================= 字体 =================
#define LOAD_GLCD
#define LOAD_FONT2 // 8 x 16
#define LOAD_FONT4 // 14 x 26
#define LOAD_FONT6 // 28 x 48
#define LOAD_FONT7 // 16 x 48 七段数码管风格，用于酷炫大时钟
#define LOAD_GFXFF

// ================= SPI 速率 =================
#define SPI_FREQUENCY 40000000

// 不使用触摸屏，这里显示禁用，避免编译警告
#define TOUCH_CS (-1)
