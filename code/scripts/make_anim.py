#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把一段可爱猫咪动画(GIF / 一叠 PNG)转成 data/catanim.bin，供 ESP 的 PetPage 播放。
帧用"调色板索引"存储(8bpp≤256 色 / 4bpp≤16 色)，ESP 端按行流式解码推屏，省 RAM。
所有帧共享同一张调色板，保证各帧配色一致、不跳色。

依赖: pip install pillow
用法:
    1) 准备素材(二选一)：
       - 一个 GIF：      cat.gif
       - 一个文件夹：    frames/ 里放 000.png 001.png ...（按文件名排序）
    2) 改下面的 INPUT / 尺寸 / 帧率，运行: python make_anim.py
    3) pio run -t uploadfs  把 data/catanim.bin 传到 ESP 的 LittleFS
注意: LittleFS(本项目 2MB)里若还放着 24px 中文字库(~1.5MB)，给动画只剩 ~400KB。
      脚本会打印占用并在超预算时警告；可减小尺寸/帧数，或把 MAX_COLORS 调到 ≤16 转 4bpp。
"""

import os
import sys
import struct
from PIL import Image, ImageSequence

# ============ 改这里 ============
INPUT      = "dog.gif"                    # GIF 路径，或放帧的文件夹
OUT        = os.path.join("data", "catanim.bin")
TARGET_W   = 180        # 目标帧宽(像素)，建议 ≤ 200
TARGET_H   = 180        # 目标帧高，PetPage 正文区约 210 高，建议 ≤ 200
FPS        = 10         # 播放帧率
MAX_COLORS = 128        # 调色板颜色数；≤16 自动用 4bpp(更省空间)
BG         = (0, 0, 0)  # 透明/留白填充色(和屏幕黑底一致)
DITHER     = False      # 平涂卡通建议 False(更干净)；有渐变可开 True
FS_BUDGET_KB = 420      # 超过就警告(按"字库占 1.5MB"估算的剩余空间)
# ================================


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_frames():
    if os.path.isdir(INPUT):
        names = sorted(f for f in os.listdir(INPUT)
                       if f.lower().endswith((".png", ".jpg", ".jpeg", ".bmp", ".gif")))
        if not names:
            sys.exit(f"[err] 文件夹 {INPUT} 里没有图片")
        return [Image.open(os.path.join(INPUT, n)) for n in names]
    if not os.path.exists(INPUT):
        sys.exit(f"[err] 找不到素材: {INPUT}（放个 cat.gif 或改 INPUT）")
    im = Image.open(INPUT)
    return [f.copy() for f in ImageSequence.Iterator(im)]


def fit_canvas(im):
    im = im.convert("RGBA")
    im.thumbnail((TARGET_W, TARGET_H), Image.LANCZOS)
    canvas = Image.new("RGBA", (TARGET_W, TARGET_H), BG + (255,))
    ox = (TARGET_W - im.width) // 2
    oy = (TARGET_H - im.height) // 2
    canvas.paste(im, (ox, oy), im)          # 用 alpha 合成到底色(黑)
    return canvas.convert("RGB")


def main():
    os.makedirs("data", exist_ok=True)
    frames = [fit_canvas(f) for f in load_frames()]
    n = len(frames)
    if n == 0:
        sys.exit("[err] 没有帧")

    # 所有帧拼一张大图，量化出"共享调色板"，再用它映射每帧 -> 索引(各帧配色一致)
    montage = Image.new("RGB", (TARGET_W, TARGET_H * n))
    for i, fr in enumerate(frames):
        montage.paste(fr, (0, i * TARGET_H))
    pal_img = montage.quantize(colors=MAX_COLORS)
    dither = 1 if DITHER else 0
    idx_frames = [fr.quantize(palette=pal_img, dither=dither) for fr in frames]

    palette = pal_img.getpalette()          # [r,g,b, r,g,b, ...]
    pal_len = min(MAX_COLORS, len(palette) // 3)
    pal565 = [rgb565(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2])
              for i in range(pal_len)]

    bpp = 4 if pal_len <= 16 else 8
    data = bytearray()
    for fr in idx_frames:
        px = fr.load()
        for y in range(TARGET_H):
            if bpp == 8:
                for x in range(TARGET_W):
                    data.append(px[x, y] & 0xFF)
            else:                            # 4bpp: 每字节 2 像素(高位在前)
                x = 0
                while x < TARGET_W:
                    hi = px[x, y] & 0x0F
                    lo = (px[x + 1, y] & 0x0F) if x + 1 < TARGET_W else 0
                    data.append((hi << 4) | lo)
                    x += 2

    with open(OUT, "wb") as f:
        f.write(b"ANM1")
        f.write(struct.pack("<HHHHHH", TARGET_W, TARGET_H, n, FPS, bpp, pal_len))
        for c in pal565:
            f.write(struct.pack("<H", c))
        f.write(data)

    total = 4 + 12 + pal_len * 2 + len(data)
    print(f"帧数 {n}  尺寸 {TARGET_W}x{TARGET_H}  {bpp}bpp  调色板 {pal_len} 色  帧率 {FPS}")
    print(f"wrote {OUT}: {total} bytes ({total/1024:.0f} KB)")
    if total / 1024 > FS_BUDGET_KB:
        print(f"[warn] 超出预算 {FS_BUDGET_KB}KB！减小尺寸/帧数，"
              f"或把 MAX_COLORS 调到 ≤16(转 4bpp 再省一半)")


if __name__ == "__main__":
    main()
