#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PC -> ESP8266 局域网直推 agent。
每 INTERVAL 秒把 CPU/内存/音量/网速 POST 到 ESP 的 /pc；
若有 NVIDIA 显卡，再把 GPU 利用率/显存/温度/功耗 POST 到 /gpu。
Windows 和 Linux 都可用（音量、GPU 取不到就跳过，屏上显示 --）。

依赖:
    pip install psutil requests
    # Windows 若要音量: pip install pycaw comtypes
    # GPU 需已安装 NVIDIA 驱动（命令行能跑 nvidia-smi）
用法:
    1) 把 ESP_IP 改成你 ESP 的 IP（看屏幕 PC/GPU 页的 Waiting 提示，或路由器里查）
    2) python pc_agent.py
"""

import time
import platform
import subprocess
import requests
import psutil

# ============ 改这里 ============
ESP_IP = "192.168.31.8"  # ESP 的局域网 IP
ESP_PORT = 80  # PC 页端口
ESP_GPU_PORT = 80  # GPU 页端口
INTERVAL = 1  # 秒
# ===============================

PC_URL = f"http://{ESP_IP}:{ESP_PORT}/pc"
GPU_URL = f"http://{ESP_IP}:{ESP_GPU_PORT}/gpu"
SYS = platform.system()


# linux系统下获取音量的时候，需要安装库：pulseaudio-utils
# Ubuntu： sudo apt-get install pulseaudio-utils
def get_volume():
    """返回 0..100 的主音量；取不到返回 -1。"""
    try:
        if SYS == "Windows":
            from ctypes import cast, POINTER
            from comtypes import CLSCTX_ALL
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            dev = AudioUtilities.GetSpeakers()
            iface = dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            vol = cast(iface, POINTER(IAudioEndpointVolume))
            return round(vol.GetMasterVolumeLevelScalar() * 100)
        else:  # Linux (PulseAudio/PipeWire)
            import re
            out = subprocess.check_output(
                ["pactl", "get-sink-volume", "@DEFAULT_SINK@"],
                stderr=subprocess.DEVNULL,
            ).decode(errors="ignore")
            m = re.search(r"(\d+)%", out)
            return int(m.group(1)) if m else -1
    except Exception:
        return -1


def get_gpu():
    """用 nvidia-smi 取第一块 GPU 的信息；没有 NVIDIA 卡则返回 None。"""
    try:
        out = subprocess.check_output(
            ["nvidia-smi",
             "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw,name",
             "--format=csv,noheader,nounits"],
            stderr=subprocess.DEVNULL,
        ).decode(errors="ignore")
        line = out.strip().splitlines()[0]
        u, mu, mt, tp, pw, name = [x.strip() for x in line.split(",")]
        return {
            "util": int(float(u)),
            "memUsed": int(float(mu)),
            "memTotal": int(float(mt)),
            "temp": int(float(tp)),
            "power": int(float(pw)),
            "name": name,
        }
    except Exception:
        return None


def main():
    print(f"pushing to {PC_URL} and {GPU_URL} every {INTERVAL}s (Ctrl-C to stop)")
    psutil.cpu_percent(interval=None)
    last = psutil.net_io_counters()
    while True:
        time.sleep(INTERVAL)
        cur = psutil.net_io_counters()
        down = (cur.bytes_recv - last.bytes_recv) / INTERVAL / 1024 / 1024  # MB/s
        up = (cur.bytes_sent - last.bytes_sent) / INTERVAL / 1024 / 1024
        last = cur

        pc = {
            "cpu": round(psutil.cpu_percent(interval=None)),
            "ram": round(psutil.virtual_memory().percent),
            "vol": get_volume(),
            "down": round(down, 1),
            "up": round(up, 1),
        }

        try:
            requests.post(PC_URL, json=pc, timeout=2)
        except Exception as e:
            print("PC send failed:", e)

        gpu = get_gpu()
        if gpu:
            try:
                requests.post(GPU_URL, json=gpu, timeout=2)
            except Exception as e:
                print("GPU send failed:", e)


if __name__ == "__main__":
    main()
