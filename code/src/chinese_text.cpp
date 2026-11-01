//
// Created by Zhang Zhimeng on 2026/8/25.
//
#include "chinese_text.h"
#include <LittleFS.h>

namespace {
    struct Range {
        uint32_t start, count, dataOff;
    };

    bool g_tried = false, g_ready = false;
    File g_file;
    Range g_ranges[6];
    int g_nRanges = 0;
    uint16_t g_gw = 16, g_gh = 16, g_bpg = 32;

    bool LoadHeader() {
        File f = LittleFS.open("/cnfont.bin", "r");
        if (!f) {
            return false;
        }

        char magic[4];
        if (f.read(reinterpret_cast<uint8_t *>(magic), 4) != 4 || memcmp(magic, "CNF1", 4) != 0) {
            f.close();
            return false;
        }

        uint16_t n = 0;
        f.read(reinterpret_cast<uint8_t *>(&g_gw), 2);
        f.read(reinterpret_cast<uint8_t *>(&g_gh), 2);
        f.read(reinterpret_cast<uint8_t *>(&g_bpg), 2);
        f.read(reinterpret_cast<uint8_t *>(&n), 2);
        const uint32_t dataStart = 4 + 2 + 2 + 2 + 2 + static_cast<uint32_t>(n) * 8;
        uint32_t cum = dataStart;
        g_nRanges = 0;
        for (uint16_t i = 0; i < n; i++) {
            uint32_t s = 0, c = 0;
            f.read(reinterpret_cast<uint8_t *>(&s), 4);
            f.read(reinterpret_cast<uint8_t *>(&c), 4);

            if (i < 6) {
                g_ranges[i] = {.start = s, .count = c, .dataOff = cum};
                g_nRanges++;
            }
            cum += c * g_bpg;
        }
        g_file = f; // 保持打开，后续按需 seek 读字模
        return g_nRanges > 0;
    }

    long GlyphOffset(const uint32_t cp) {
        for (int i = 0; i < g_nRanges; i++)
            if (cp >= g_ranges[i].start && cp < g_ranges[i].start + g_ranges[i].count)
                return static_cast<long>(g_ranges[i].dataOff + (cp - g_ranges[i].start) * g_bpg);
        return -1;
    }

    // 解析下一个 UTF-8 码点，返回消耗的字节数
    int NextCp(const uint8_t *p, uint32_t &cp) {
        const uint8_t b = p[0];
        if (b < 0x80) {
            cp = b;
            return 1;
        }

        int len;
        if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            len = 4;
        } else {
            cp = '?';
            return 1;
        }

        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                cp = '?';
                return i;
            } // 非法续字节
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        return len;
    }

    void DrawGlyph(TFT_eSPI &tft, const int x, const int y, const uint32_t cp, const uint16_t fg, const int bg,
                   const int scale) {
        const int W = g_gw; // 每行字节数
        const int H = g_gh;
        const int bpr = (W + 7) / 8;
        if (bg >= 0) {
            tft.fillRect(x, y, W * scale, H * scale, static_cast<uint16_t>(bg));
        }

        const long off = GlyphOffset(cp);
        if (off < 0 || !g_file) {
            return; // 字库里没有该字 -> 留空
        }

        uint8_t buf[200];
        if (g_bpg == 0 || static_cast<int>(g_bpg) > static_cast<int>(sizeof(buf))) {
            return;
        }

        g_file.seek(off);
        if (g_file.read(buf, g_bpg) != static_cast<int>(g_bpg)) {
            return;
        }

        for (int row = 0; row < H; row++) {
            const uint8_t *rp = buf + row * bpr;
            for (int col = 0; col < W; col++) {
                if (rp[col >> 3] & (0x80 >> (col & 7))) {
                    if (scale == 1) tft.drawPixel(x + col, y + row, fg);
                    else tft.fillRect(x + col * scale, y + row * scale, scale, scale, fg);
                }
            }
        }
    }
}

namespace CnText {
    bool Begin() {
        if (g_tried) return g_ready;
        g_tried = true;
        if (!LittleFS.begin()) return false;
        g_ready = LoadHeader();
        return g_ready;
    }

    bool Ready() { return g_ready; }

    int Height() { return g_gh; }

    int Width(TFT_eSPI &tft, const char *utf8, int scale) {
        if (scale < 1) {
            scale = 1;
        }

        auto p = reinterpret_cast<const uint8_t *>(utf8);
        int w = 0;
        while (*p) {
            uint32_t cp;
            const int n = NextCp(p, cp);
            p += n;
            if (cp < 0x80) {
                const char s[2] = {static_cast<char>(cp), 0};
                w += tft.textWidth(s, 2) * scale;
            } else w += g_gw * scale;
        }
        return w;
    }

    void Draw(TFT_eSPI &tft, const int x, const int y, const char *utf8, const uint16_t fg, const int bg, int scale) {
        if (!g_ready) {
            Begin();
        } // 懒加载（LittleFS 不可用也不崩，只是汉字留空）

        if (scale < 1) {
            scale = 1;
        }

        const auto *p = reinterpret_cast<const uint8_t *>(utf8);
        int cx = x;
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(scale); // ASCII 用内置字体整数放大

        while (*p) {
            uint32_t cp;
            const int n = NextCp(p, cp);
            p += n;
            if (cp < 0x80) {
                if (bg >= 0) tft.setTextColor(fg, static_cast<uint16_t>(bg));
                else tft.setTextColor(fg);
                const int w = tft.drawChar(static_cast<uint16_t>(cp), cx, y, 2);
                cx += (w > 0 ? w : 8 * scale);
            } else {
                DrawGlyph(tft, cx, y, cp, fg, bg, scale);
                cx += g_gw * scale;
            }
        }
        tft.setTextSize(1); // 复位，避免影响别的页
    }

    void DrawCentered(TFT_eSPI &tft, const int cx, const int y, const char *utf8, const uint16_t fg, const int bg,
                      const int scale) {
        Draw(tft, cx - Width(tft, utf8, scale) / 2, y, utf8, fg, bg, scale);
    }
} // namespace CnText
