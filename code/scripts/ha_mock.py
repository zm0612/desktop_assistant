#!/usr/bin/env python3
# coding: utf-8
"""
ha_mock.py —— 假的 Home Assistant REST API，用来在**不安装 HA** 的情况下测试 home 页。

home 页只会向 HA 发：GET {ha_url}/api/states/{entity_id}  （带 Bearer 令牌），
并且只读返回 JSON 里的 "state" 字段（见 src/home_page.cpp: HaState）。
所以这里用 Python 标准库起一个最小服务器，对任意 /api/states/xxx 回一个合理的
state 即可，无需真装 Home Assistant，也不用 pip 装任何东西。

用法：
  1) 电脑上运行：   python scripts/ha_mock.py            # 监听 0.0.0.0:8123
     换端口：       python scripts/ha_mock.py --port 8123
  2) 查电脑局域网 IP： Linux/mac `ip addr` / `ifconfig`；Windows `ipconfig`（如 192.168.1.50）
  3) 设备网页「米家(HA)」卡片这样填：
       HA 地址        = http://<电脑IP>:8123      （结尾不要加斜杠）
       长期访问令牌   = 随便填非空，如 test        （本 mock 不校验）
       温度/湿度/PM2.5 = sensor.temp / sensor.humid / sensor.pm25
       净化器/灯/电视  = fan.x / light.x / media_player.tv
     保存并重启，切到 home 页即可看到假数据。
  4) 终端会打印设备发来的每一次请求，可确认设备在正常轮询。

注意：确保电脑防火墙放行该端口，且设备与电脑在同一局域网。
"""

import argparse
import json
import random
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def fake_state(eid: str) -> str:
    """按 entity_id 里的关键字返回一个合理的 state（带轻微抖动，便于看到刷新）。"""
    e = eid.lower()
    if "temp" in e:
        return f"{22.0 + random.uniform(-1.5, 1.5):.1f}"  # ~22℃ 上下抖动
    if "humid" in e:
        return f"{55 + random.randint(-5, 5)}"
    if "pm" in e:
        return f"{max(0, 12 + random.randint(-6, 20))}"  # 偶尔冲高，看 PM 颜色变化
    if "fan" in e or "purifier" in e:
        return "on"
    if "light" in e:
        return "on"
    if "other" in e:
        return "on"
    if "media_player" in e or "tv" in e:
        return "playing"
    return "on"


class Handler(BaseHTTPRequestHandler):
    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path.startswith("/api/states/"):
            eid = path[len("/api/states/"):]
            state = fake_state(eid)
            print(f"  GET {path}  -> state={state}")
            self._json(200, {"entity_id": eid, "state": state, "attributes": {}})
        elif path in ("/api", "/api/"):
            self._json(200, {"message": "API running."})  # HA 根接口，方便 curl 自测
        else:
            self._json(404, {"message": "not found"})

    def log_message(self, *args):
        pass  # 屏蔽默认访问日志，用上面 do_GET 里的打印代替


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8123)
    args = ap.parse_args()
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"[ha_mock] fake Home Assistant on http://{args.host}:{args.port}")
    print(f"[ha_mock] 设备网页 HA 地址填 http://<本机IP>:{args.port} ，令牌随便填。Ctrl-C 退出。")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[ha_mock] bye")


if __name__ == "__main__":
    main()
