#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 16x16 1bit 中文点阵字库 data/cnfont.bin，供 ESP 的 CnText 模块使用。
按 Unicode 区段连续存储，ESP 端按 (码点-区段起点)*32 直接算偏移，无需 GB2312 转换表。

依赖: pip install pillow
用法:
    1) 准备一个含中文的字体文件（推荐 思源黑体 SourceHanSansSC / Noto Sans CJK SC）
    2) 改下面的 FONT_PATH，运行: python make_cnfont.py
    3) 生成的 data/cnfont.bin 用 `pio run -t uploadfs` 传到 ESP 的 LittleFS
覆盖: CJK 汉字(U+4E00-9FFF) + CJK 标点(U+3000-303F) + 全角(U+FF00-FFEF)
      约 21296 字 x 32 字节 ≈ 680KB（LittleFS 1MB 放得下）
"""

import os
import struct
from PIL import Image, ImageFont, ImageDraw

# ============ 改这里 ============
FONT_PATH = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"  # 换成你本地的中文字体
CELL = 24  # 点阵格子(=字高宽)。16/24/32 均可（改动需同步 LittleFS 分区大小）
SIZE = 22  # 字号，经验值 ≈ CELL-2；被裁再调小、留白多再调大
Y_OFFSET = 0  # 垂直微调(像素)：字偏上调正、偏下调负
# ================================

GW = GH = CELL
BPR = (GW + 7) // 8  # 每行字节数
BYTES_PER_GLYPH = BPR * GH
THRESHOLD = 110
RANGES = [(0x4E00, 0x9FFF), (0x3000, 0x303F), (0xFF00, 0xFFEF)]
OUT = os.path.join("data", "cnfont.bin")


def main():
    os.makedirs("data", exist_ok=True)
    font = ImageFont.truetype(FONT_PATH, SIZE)
    asc, desc = font.getmetrics()
    baseline = (CELL + asc - desc) / 2.0 + Y_OFFSET  # 把 ascent+descent 居中，得到统一基线
    if asc + desc > CELL:
        print(f"[warn] 行高 {asc + desc}px > 格子 {CELL}px，上下可能略裁，可把 SIZE 调小")
    data = bytearray()
    hdr_ranges = []

    for (start, end) in RANGES:
        count = end - start + 1
        hdr_ranges.append((start, count))
        for cp in range(start, end + 1):
            img = Image.new("L", (GW, GH), 0)
            # 统一基线摆放(anchor="ms"=水平居中+基线对齐)：汉字填满、标点自然贴底
            ImageDraw.Draw(img).text((GW / 2.0, baseline), chr(cp),
                                     anchor="ms", fill=255, font=font)
            for y in range(GH):
                b = 0;
                nbits = 0
                for x in range(GW):
                    b = (b << 1) | (1 if img.getpixel((x, y)) >= THRESHOLD else 0)
                    nbits += 1
                    if nbits == 8:
                        data.append(b);
                        b = 0;
                        nbits = 0
                if nbits:  # 行末不足 8 位，左对齐补齐
                    data.append(b << (8 - nbits))
        print(f"range U+{start:04X}-U+{end:04X}: {count} glyphs done")

    with open(OUT, "wb") as f:
        f.write(b"CNF1")
        f.write(struct.pack("<HHH", GW, GH, BYTES_PER_GLYPH))
        f.write(struct.pack("<H", len(hdr_ranges)))
        for (s, c) in hdr_ranges:
            f.write(struct.pack("<II", s, c))
        f.write(data)

    total = 4 + 6 + 2 + len(hdr_ranges) * 8 + len(data)
    print(f"wrote {OUT}: {total} bytes ({total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
