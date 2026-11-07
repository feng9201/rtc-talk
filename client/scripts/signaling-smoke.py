#!/usr/bin/env python3
"""Short development-only signaling smoke in a new isolated room (no media)."""
import argparse
import json
import subprocess
import urllib.parse
import urllib.request
import uuid
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--cli", required=True)
    parser.add_argument("--duration", type=int, default=1)
    parser.add_argument("--dev-token", action="store_true", required=True)
    parser.add_argument("--report")
    args = parser.parse_args()
    endpoint = urllib.parse.urlsplit(args.server)
    if (endpoint.scheme not in ("http", "https") or not endpoint.netloc or
            endpoint.username or endpoint.password or endpoint.query or endpoint.fragment or
            endpoint.path not in ("", "/") or not 1 <= args.duration <= 10):
        parser.error("explicit HTTP(S) development origin and duration 1..10 required")
    suffix = uuid.uuid4().hex[:20]
    uid, room = "rk-a2-" + suffix, "rk-a2-" + suffix
    report = {"ok": False, "scope": "signaling-only", "room": room}
    try:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        query = urllib.parse.urlencode({"uid": uid, "rooms": room})
        with opener.open(args.server.rstrip("/") + "/dev/token?" + query, timeout=8) as response:
            token = json.load(response)["token"]
        if not isinstance(token, str) or not 1 <= len(token) <= 8192:
            raise ValueError("invalid credential response")
        command = [args.cli, "--signaling-only", "--token-stdin", "--url",
                   ("wss://" if endpoint.scheme == "https" else "ws://") + endpoint.netloc + "/talk/v1/ws",
                   "--room", room, "--duration", str(args.duration)]
        if endpoint.scheme == "http":
            command.append("--development-allow-ws")
        result = subprocess.run(command, input=token + "\n", text=True,
                                capture_output=True, timeout=args.duration + 45)
        token = ""
        events = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        names = [event.get("name") for event in events]
        required = ("session.ready", "room.joined", "room.left", "session.bye", "session.closed")
        joined = [event["data"] for event in events if event.get("name") == "room.joined"]
        report.update({
            "exit": result.returncode,
            "events": names,
            "errors": [event["data"].get("code") for event in events if event.get("name") == "error"],
            "peer_count": joined[-1].get("peer_count") if joined else None,
            "stderr_present": bool(result.stderr.strip()),
        })
        report["ok"] = (result.returncode == 0 and all(names.count(name) == 1 for name in required)
                        and not report["errors"] and report["peer_count"] == 0)
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired):
        # Never print exception details or TimeoutExpired.input (contains JWT).
        report["error"] = "signaling transport/response/timeout failure"
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.report:
        Path(args.report).write_text(text + "\n", encoding="utf-8")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
