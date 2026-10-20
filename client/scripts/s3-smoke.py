#!/usr/bin/env python3
"""S3 联调：两人自动 P2P、第三人切 SFU、呼叫接听；凭证只在内存和进程 stdin 中传递。"""
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
parser.add_argument("--audio", choices=["fake", "none"], default="fake")
parser.add_argument("--phase", choices=["p2p", "call", "all"], default="all")
parser.add_argument("--switch-at", type=float, default=10.0, help="第三人加入延迟（秒）")
parser.add_argument("--duration", type=int, default=24)
parser.add_argument("--dev-token", action="store_true", required=True)
parser.add_argument("--report", help="保存不含凭证的测试结果 JSON")
args = parser.parse_args()
if not 8 <= args.duration <= 3600 or not 2 <= args.switch_at <= args.duration - 6:
    parser.error("要求 8～3600 秒，且第三人加入时间不晚于时长减 6 秒")
url = urllib.parse.urlsplit(args.server)
if url.scheme != "http" or not url.netloc or url.username:
    parser.error("仅用于显式启用的 HTTP 开发服务")
ws = "ws://" + url.netloc + "/talk/v1/ws"


def run(uid, room, duration, delay, extra=()):
    time.sleep(delay)
    query = urllib.parse.urlencode({"uid": uid, "rooms": room})
    with urllib.request.urlopen(args.server.rstrip("/") + "/dev/token?" + query, timeout=8) as response:
        token = json.load(response)["token"]
    command = [args.cli, "--url", ws, "--room", room, "--audio", args.audio,
               "--duration", str(duration), "--token-stdin", *extra]
    if args.ssh:
        command = ["ssh", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=5",
                   args.ssh, shlex.join(command)]
        if args.control_path:
            command[1:1] = ["-S", args.control_path]
        if args.sshpass:
            command = ["sshpass", "-e"] + command
    result = subprocess.run(command, input=token + "\n", text=True, capture_output=True,
                            timeout=duration + 60)
    events = []
    for line in result.stdout.splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return {"uid": uid, "exit": result.returncode, "events": events,
            "stderr_present": bool(result.stderr.strip())}


def emitted(record, name):
    return [e["data"] for e in record["events"] if e.get("event") == name]


def index_of(record, name, match=None):
    for i, e in enumerate(record["events"]):
        if e.get("event") == name and (match is None or match(e["data"])):
            return i
    return None


def errors(record):
    return [{"event": e["event"], "data": e["data"]} for e in record["events"]
            if e.get("event") in ("error", "media.error")
            or (e.get("event") == "result" and not e["data"].get("ok"))]


def summarize(record):
    return {"uid": record["uid"], "exit": record["exit"],
            "events": [e.get("event") for e in record["events"]],
            "errors": errors(record), "stderr_present": record["stderr_present"]}


def p2p_phase():
    room = "rk-s3-p2p-" + str(int(time.time()))
    switch = args.switch_at
    total = args.duration
    starts = {"a": (0, total), "b": (2, total - 2), "c": (switch, total - switch)}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = {k: pool.submit(run, room + "-" + k, room, d, delay)
                   for k, (delay, d) in starts.items()}
        records = {k: f.result() for k, f in futures.items()}
    a, b, c = records["a"], records["b"], records["c"]
    checks = {}

    def check(name, ok):
        checks[name] = bool(ok)

    # 两人 auto：B 入房后触发 P2P，A 为 offerer、B 为 answerer；B 可能在入房快照里直接拿到 P2P。
    a_p2p = [m for m in emitted(a, "mediaMode") if m.get("media_mode") == "p2p"]
    b_p2p = [m for m in emitted(b, "mediaMode") if m.get("media_mode") == "p2p"]
    b_view = [m for m in emitted(b, "joined") if m.get("media_mode") == "p2p"]
    check("a 收到 P2P 模式", bool(a_p2p))
    check("b 收到 P2P 模式", bool(b_p2p or b_view))
    check("a 为 offerer", any(m.get("p2p", {}).get("role") == "offerer" for m in a_p2p))
    b_roles = {m.get("p2p", {}).get("role") for m in b_p2p + b_view}
    check("b 为 answerer", "answerer" in b_roles)
    if args.audio == "fake":
        check("a P2P 连通", any(m.get("state") == "connected" for m in emitted(a, "p2p.state")))
        check("b P2P 连通", any(m.get("state") == "connected" for m in emitted(b, "p2p.state")))
        stats = [e["data"].get("data", {}) for e in a["events"]
                 if e.get("event") == "result" and e["data"].get("method") == "media.stats"]
        paths = {s.get("p2p", {}).get("path") for s in stats if s.get("p2p", {}).get("path")}
        paths |= {m.get("path") for m in emitted(a, "p2p.state") if m.get("path")}
        check("a 报告 P2P 路径", bool(paths & {"host", "srflx", "prflx", "relay"}))
    # 第三人加入：先切 SFU 重新发布，再由服务端关闭 P2P。
    for name, record in (("a", a), ("b", b)):
        sfu = index_of(record, "mediaMode", lambda d: d.get("media_mode") == "sfu")
        closed = index_of(record, "p2p.closed")
        check(name + " 收到切 SFU", sfu is not None)
        check(name + " 收到 p2p.close", closed is not None)
        check(name + " 先切后断", sfu is not None and closed is not None and sfu < closed)
        if args.audio == "fake":
            check(name + " 切回后重新发布", sfu is not None and index_of(
                {"events": record["events"][sfu:]}, "published") is not None)
        check(name + " 无会话错误", not errors(record))
    check("c 入房成功", index_of(c, "joined") is not None)
    check("c 无会话错误", not errors(c))
    return {"room": room, "checks": checks, "clients": [summarize(r) for r in (a, b, c)]}


def call_phase():
    room = "rk-s3-call-" + str(int(time.time()))
    callee, caller = room + "-b", room + "-a"
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        b_future = pool.submit(run, callee, room, args.duration + 5, 0, ("--auto-accept",))
        a_future = pool.submit(run, caller, room, args.duration, 3,
                               ("--invite", callee))
        a, b = a_future.result(), b_future.result()
    checks = {}

    def check(name, ok):
        checks[name] = bool(ok)
    incoming = emitted(b, "call.incoming")
    check("b 收到来电", any(m.get("from") == caller for m in incoming))
    check("a 收到已接听", any(m.get("state") == "accepted" for m in emitted(a, "call.state")))
    check("a 入房", index_of(a, "joined") is not None)
    check("b 入房", index_of(b, "joined") is not None)
    def in_p2p(record):
        modes = emitted(record, "mediaMode") + emitted(record, "joined")
        return any(m.get("media_mode") == "p2p" for m in modes)

    check("a 进入 P2P", in_p2p(a))
    check("b 进入 P2P", in_p2p(b))
    if args.audio == "fake":
        check("a P2P 连通", any(m.get("state") == "connected" for m in emitted(a, "p2p.state")))
        check("b P2P 连通", any(m.get("state") == "connected" for m in emitted(b, "p2p.state")))
    check("a 无会话错误", not errors(a))
    check("b 无会话错误", not errors(b))
    return {"room": room, "checks": checks, "clients": [summarize(a), summarize(b)]}


phases = []
if args.phase in ("p2p", "all"):
    phases.append(p2p_phase())
if args.phase in ("call", "all"):
    phases.append(call_phase())
report = json.dumps({"audio": args.audio, "phases": phases}, ensure_ascii=False, indent=2)
if args.report:
    pathlib.Path(args.report).write_text(report + "\n", encoding="utf-8")
print(report)
raise SystemExit(0 if all(all(p["checks"].values()) for p in phases) else 1)
