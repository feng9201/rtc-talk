#!/usr/bin/env python3
"""显式开发模式联调；凭证只在内存和进程 stdin 中传递，不写日志。"""
import argparse
import concurrent.futures
import json
import pathlib
import shlex
import subprocess
import time
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument("--server", required=True, help="开发 HTTP 地址")
parser.add_argument("--cli", required=True, help="本机或远端 CLI 绝对路径")
parser.add_argument("--ssh", help="已验证 host key 的 SSH user@host")
parser.add_argument("--sshpass", action="store_true", help="从环境 SSHPASS 获取 SSH 口令")
parser.add_argument("--control-path", help="复用已认证的 SSH 控制连接")
parser.add_argument("--audio", choices=["none", "fake"], default="none")
parser.add_argument("--duration", type=int, default=15)
parser.add_argument("--clients", type=int, default=2)
parser.add_argument("--stagger", type=float, default=0, help="各客户端启动间隔，用于与同时入房对照")
parser.add_argument("--dev-token", action="store_true", required=True)
parser.add_argument("--report", help="保存不含凭证的测试结果 JSON")
args = parser.parse_args()
if not 1 <= args.clients <= 3 or not 1 <= args.duration <= 3600:
    parser.error("限定 1～3 个客户端、1～3600 秒")
url = urllib.parse.urlsplit(args.server)
if url.scheme != "http" or not url.netloc or url.username:
    parser.error("仅用于显式启用的 HTTP 开发服务")
ws = "ws://" + url.netloc + "/talk/v1/ws"
room = "rk-smoke-" + str(int(time.time()))


def run(index):
    time.sleep(index * args.stagger)
    query = urllib.parse.urlencode({"uid": room + "-" + str(index), "rooms": room})
    with urllib.request.urlopen(args.server.rstrip("/") + "/dev/token?" + query, timeout=8) as response:
        token = json.load(response)["token"]
    command = [args.cli, "--url", ws, "--room", room, "--audio", args.audio,
               "--duration", str(args.duration), "--token-stdin"]
    if args.ssh:
        command = ["ssh", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=5",
                   args.ssh, shlex.join(command)]
        if args.control_path:
            command[1:1] = ["-S", args.control_path]
        if args.sshpass:
            command = ["sshpass", "-e"] + command
    result = subprocess.run(command, input=token + "\n", text=True, capture_output=True,
                            timeout=args.duration + 60)
    events = []
    for line in result.stdout.splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    names = [e.get("event") for e in events]
    stats = [e["data"]["data"] for e in events if e.get("event") == "result"
             and e["data"].get("method") == "media.stats" and e["data"].get("ok")]
    good = result.returncode == 0 and "joined" in names and "left" in names
    connected = {e["data"]["pc"] for e in events if e.get("event") == "media.state"
                 and e["data"].get("state") == 2}
    if args.audio == "fake":
        good = good and connected == {"send", "recv"} and "subscribed" in names
        good = good and any(400 <= s.get("fake_playout", {}).get("frequency_hz", 0) <= 480
                            and s["fake_playout"].get("rms", 0) > 100
                            and s["fake_playout"].get("signal_frames", 0) > 100
                            and s["fake_playout"].get("clipped", 1) == 0 for s in stats)
    errors = [e for e in events if e.get("event") in ("error", "media.error")
              or (e.get("event") == "result" and not e["data"].get("ok"))]
    return {"client": index, "ok": bool(good), "exit": result.returncode,
            "events": names, "stats": stats, "errors": errors,
            "stderr_present": bool(result.stderr.strip())}


with concurrent.futures.ThreadPoolExecutor(max_workers=args.clients) as pool:
    results = list(pool.map(run, range(args.clients)))
report = json.dumps({"room": room, "audio": args.audio, "stagger": args.stagger, "results": results}, ensure_ascii=False, indent=2)
if args.report:
    pathlib.Path(args.report).write_text(report + "\n", encoding="utf-8")
print(report)
raise SystemExit(0 if all(r["ok"] for r in results) else 1)
