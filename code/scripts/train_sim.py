#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""训练页(/train)造数据脚本：几十秒灌完一整轮假训练，用来验证 loss 曲线与防闪烁。

loss 走「指数衰减 + 递减噪声」，从 ~2.5 掉到 ~0.01（跨两个数量级），
所以设备端会自动切到对数纵轴（曲线标题出现 LOG 字样）。

用法:
    python tools/train_sim.py                                  # 300 epoch
    python tools/train_sim.py --host 192.168.1.42 --epochs 1200 --delay 0.02
    python tools/train_sim.py --name job-b                     # 换个名字即清空曲线重来

看什么:
  - epoch 数超过 110 后，曲线标题出现 /2、/4、/8…… 这是抽稀在生效，
    内存恒定 440 字节，曲线始终覆盖整个训练过程，只是分辨率变粗。
  - 刷新过程中除了进度条推进和数字变化，其余像素不应有整屏重画的闪烁。
"""

import argparse
import math
import random
import time

import requests


def fmt_eta(sec):
    sec = int(max(0, sec))
    if sec >= 3600:
        return f"{sec // 3600}h{sec % 3600 // 60}m"
    if sec >= 60:
        return f"{sec // 60}m{sec % 60}s"
    return f"{sec}s"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.31.8", help="ESP 的局域网 IP 或 infodisplay.local")
    ap.add_argument("--port", default="80", help="ESP的局域网端口")
    ap.add_argument("--name", default="sim-resnet", help="任务名；换名字设备端会清空历史曲线")
    ap.add_argument("--epochs", type=int, default=300)
    ap.add_argument("--steps", type=int, default=500, help="每个 epoch 的 step 总数")
    ap.add_argument("--per-epoch", type=int, default=3, help="每个 epoch 上报几次")
    ap.add_argument("--delay", type=float, default=0.05, help="两次上报之间的间隔秒数")
    a = ap.parse_args()

    url = f"http://{a.host}:{a.port}/train"
    total = a.epochs * a.per_epoch
    sent = failed = 0
    loss = 0.0

    for e in range(1, a.epochs + 1):
        for k in range(1, a.per_epoch + 1):
            frac = (e - 1 + k / a.per_epoch) / a.epochs
            loss = 2.5 * math.exp(-4.5 * frac) + 0.01
            loss += random.uniform(-0.06, 0.06) * (1 - frac) * loss
            loss = max(loss, 1e-4)  # 保持为正，对数纵轴才成立
            acc = 0.2 + 0.78 * (1 - math.exp(-3.5 * frac))
            done = sent + failed + 1
            body = {
                "name": a.name, "status": "training",
                "epoch": e, "epochs": a.epochs,
                "step": int(a.steps * k / a.per_epoch), "steps": a.steps,
                "loss": round(loss, 6), "acc": round(acc, 4),
                "eta": fmt_eta((total - done) * a.delay),
            }
            try:
                requests.post(url, json=body, timeout=2)
                sent += 1
            except Exception as ex:
                failed += 1
                if failed == 1:
                    print(f"上报失败（后续不再提示）: {ex}")
            time.sleep(a.delay)
        if e % 10 == 0 or e == a.epochs:
            print(f"epoch {e}/{a.epochs}  loss={loss:.4f}  ok={sent} fail={failed}")

    try:
        requests.post(url, json={"name": a.name, "status": "done",
                                 "epoch": a.epochs, "epochs": a.epochs,
                                 "step": 0, "steps": 0,
                                 "loss": round(loss, 6), "acc": 0.98, "eta": ""},
                      timeout=2)
    except Exception:
        pass
    print(f"完成：成功 {sent} 次，失败 {failed} 次 → {url}")


if __name__ == "__main__":
    main()
