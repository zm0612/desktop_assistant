#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
在你的 PyTorch 训练循环里调用 report(...)，把进度推到 ESP 的训练页(/train)。
pip install requests

用法示例:
    from train_report import report
    for epoch in range(EPOCHS):
        for step, batch in enumerate(loader):
            loss = train_step(batch)
            if step % 20 == 0:
                report(name="resnet50", status="training",
                       epoch=epoch+1, epochs=EPOCHS,
                       step=step+1, steps=len(loader),
                       loss=float(loss), acc=None, eta="")
    report(name="resnet50", status="done", epoch=EPOCHS, epochs=EPOCHS,
           step=0, steps=0, loss=float(loss), acc=0.93, eta="")
"""

import requests

# ============ 改这里 ============
ESP_IP = "192.168.31.8"  # ESP 的局域网 IP（看训练页 Waiting 提示）
ESP_PORT = 80
# ================================

_URL = f"http://{ESP_IP}:{ESP_PORT}/train"


def report(name="job", status="training", epoch=0, epochs=0,
           step=0, steps=0, loss=None, acc=None, eta=""):
    """非阻塞、失败也不影响训练；字段都可选。"""
    data = {"name": name, "status": status,
            "epoch": epoch, "epochs": epochs,
            "step": step, "steps": steps, "eta": eta}
    if loss is not None:
        data["loss"] = float(loss)
    if acc is not None:
        data["acc"] = float(acc)
    try:
        requests.post(_URL, json=data, timeout=1)
    except Exception:
        pass  # 训练优先，推送失败静默忽略


if __name__ == "__main__":
    # 自测：发一条假数据
    report(name="demo", status="training", epoch=3, epochs=100,
           step=1200, steps=5000, loss=0.0234, acc=0.91, eta="1h23m")
    print("sent test report to", _URL)
