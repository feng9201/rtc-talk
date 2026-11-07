#!/usr/bin/env python3
"""Development-only RK audio verification. No device/mixer/service modifications.

Credentials are taken from an environment variable, JWTs only from stdin. All
uploads/logs go to a unique /tmp directory which is retained for diagnosis.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import urllib.parse
import urllib.request
import uuid


class SmokeFailure(RuntimeError):
    """Only fixed diagnostic labels authored in this script; never raw SDK text."""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--known-hosts", required=True)
    parser.add_argument("--password-env", default="RTC_BOARD_PASSWORD")
    parser.add_argument("--server", required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--dev-token", action="store_true", required=True)
    parser.add_argument("--existing-deployment", help="Reuse this script's isolated /tmp deployment for diagnosis")
    parser.add_argument("--scenario", choices=("all", "duplex", "default-muted"), default="all")
    parser.add_argument("--capture-core", action="store_true", help="Single-instance diagnosis only; core stays in isolated /tmp")
    args = parser.parse_args()
    if args.capture_core and args.scenario != "default-muted":
        parser.error("core capture requires the single-instance default-muted scenario")
    if not re.fullmatch(r"[a-zA-Z0-9_.:-]+", args.host) or not re.fullmatch(r"[a-zA-Z0-9_-]+", args.user):
        parser.error("invalid board address")
    if not os.environ.get(args.password_env):
        parser.error("board password environment variable is missing")
    base = args.server.rstrip("/")
    parsed = urllib.parse.urlsplit(base)
    if (parsed.scheme not in ("http", "https") or not parsed.netloc or parsed.path or
            parsed.username or parsed.password or parsed.query or parsed.fragment):
        parser.error("server must be an HTTP(S) origin")
    # Deliberately explicit: this script uses the development token endpoint.
    env = os.environ.copy()
    env["SSHPASS"] = env.pop(args.password_env)
    common = ["-o", "ConnectTimeout=5", "-o", "ConnectionAttempts=1",
              "-o", "NumberOfPasswordPrompts=1", "-o", "StrictHostKeyChecking=yes",
              "-o", "UserKnownHostsFile=" + str(Path(args.known_hosts).resolve())]
    target = args.user + "@" + args.host
    # OpenSSH 8.x already uses the legacy SCP protocol and has no -O switch.
    # Newer clients need -O for boards without an SFTP subsystem. Inspect local
    # usage before uploading; do not retry remote writes with guessed flags.
    scp_usage = subprocess.run(["scp"], text=True, capture_output=True, timeout=5).stderr
    scp_legacy = ["-O"] if re.search(r"usage: scp \[[^\]]*O", scp_usage) else []

    def ssh(command, token=None, timeout=15):
        return subprocess.run(["sshpass", "-e", "ssh"] + common + [target, command],
                              input=token, text=True, capture_output=True, env=env, timeout=timeout)

    if args.existing_deployment:
        remote = args.existing_deployment
    else:
        result = ssh("mktemp -d /tmp/rtc-rk-audio.XXXXXXXX")
        remote = result.stdout.strip() if result.returncode == 0 else ""
    if not re.fullmatch(r"/tmp/rtc-rk-audio\.[a-zA-Z0-9]{8}", remote):
        raise SmokeFailure("board temporary deployment directory unavailable")
    print("board deployment: " + remote, file=sys.stderr, flush=True)
    if ssh("mkdir -p " + remote + "/bin " + remote + "/lib").returncode:
        raise SmokeFailure("board deployment directory creation failed")
    product = Path(__file__).resolve().parent.parent
    sdk = product / "third_party/log-cpp/lib/linux-aarch64/librtc_log_adapter.so.1.0.0"

    def upload(source, destination):
        completed = subprocess.run(["sshpass", "-e", "scp"] + scp_legacy + common +
                                   [str(source.resolve()), target + ":" + destination],
                                   capture_output=True, text=True, env=env, timeout=30)
        if completed.returncode:
            raise SmokeFailure("board artifact upload failed")

    if not args.existing_deployment:
        upload(args.build / "rtc-cli", remote + "/bin/rtc-cli")
        upload(sdk, remote + "/lib/librtc_log_adapter.so.1")
        upload(args.build / "librtc_audio.so.0.1.0", remote + "/lib/librtc_audio.so.0")
    suites = ["session", "operations", "video", "concurrency", "media_worker",
              "log_abi", "protocol", "rpc", "logging", "audio_backend", "audio_tls", "transport"]
    if not args.existing_deployment:
        for suite in suites:
            upload(args.build / ("rtc_" + suite + "_test"), remote + "/bin/rtc_" + suite + "_test")
    unit_results = {}
    for suite in ([] if args.existing_deployment else suites):
        print("board test: " + suite, file=sys.stderr, flush=True)
        tested = ssh("LD_LIBRARY_PATH=" + remote + "/lib " + remote + "/bin/rtc_" + suite + "_test", timeout=30)
        unit_results[suite] = tested.returncode == 0
        if tested.returncode:
            raise SmokeFailure("board test failed: " + suite + " (exit " + str(tested.returncode) + ")")

    ws = urllib.parse.urlunsplit(("wss" if parsed.scheme == "https" else "ws", parsed.netloc, "/talk/v1/ws", "", ""))

    def client(room, index, unmute):
        uid = "rk-audio-" + uuid.uuid4().hex
        query = urllib.parse.urlencode({"uid": uid, "rooms": room})
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(base + "/dev/token?" + query, timeout=8) as response:
            token = json.load(response)["token"]
        if not isinstance(token, str) or not 1 <= len(token) <= 8192:
            raise SmokeFailure("invalid development token response")
        log_file = remote + "/client-" + uuid.uuid4().hex[:12] + "-" + str(index) + ".log"
        command = [remote + "/bin/rtc-cli", "--audio", "--development-fake-audio",
                   "--token-stdin", "--url", ws, "--room", room,
                   "--duration", "12" if unmute else "6",
                   "--log-file", log_file]
        if parsed.scheme == "http":
            command.append("--development-allow-ws")
        if unmute:
            command.append("--unmute")
        # A shell-side marker distinguishes SSH exit=255 from the actual child
        # exit status, including signals. Never include the JWT in shell text.
        remote_command = ("cd " + remote + " || exit 2; umask 077; ulimit -c " +
                          ("65536" if args.capture_core else "0") + "; " +
                          "LD_LIBRARY_PATH=" + remote + "/lib timeout 45 " + shlex.join(command) +
                          "; rtc_test_exit=$?; printf '\\nRTC_CLIENT_EXIT=%s\\n' \"$rtc_test_exit\"; exit \"$rtc_test_exit\"")
        completed = ssh(remote_command, token + "\n", timeout=55)
        marker = re.search(r"(?m)^RTC_CLIENT_EXIT=([0-9]{1,3})$", completed.stdout)
        remote_exit = int(marker.group(1)) if marker else None
        log_check = ssh("grep -Fq " + shlex.quote('"event":"media.initialized"') + " " + shlex.quote(log_file))
        media_log_present = log_check.returncode == 0
        # CLI output is sanitized structured events. Never retain raw stdout or
        # arbitrary stderr (which could contain credentials in an error path).
        events = []
        for line in completed.stdout.splitlines():
            try:
                event = json.loads(line)
                if event.get("schema") == 1 and isinstance(event.get("data"), dict):
                    events.append(event)
            except (ValueError, AttributeError):
                pass
        names = [event.get("name") for event in events]
        stats = [event["data"] for event in events if event.get("name") == "audio.stats"]
        def maximum(section, field):
            return max((item.get(section, {}).get(field, 0) for item in stats), default=0)
        sent, received = maximum("send", "sent"), maximum("recv", "received")
        signal_frames = maximum("fake_playout", "signal_frames")
        frequency = maximum("fake_playout", "frequency_hz")
        muted_events = [event["data"].get("muted") for event in events if event.get("name") == "audio.muted"]
        ok = (completed.returncode == 0 and remote_exit == 0 and media_log_present and
              "audio.published" in names and "room.left" in names and "error" not in names)
        if unmute:
            ok = ok and "media.subscribed" in names and sent > 50 and received > 50 and signal_frames > 50 and 420 <= frequency <= 460 and False in muted_events
        else:
            ok = ok and sent == 0 and False not in muted_events
        return {"ok": bool(ok), "exit_code": completed.returncode, "unmute_requested": unmute,
                "remote_exit_code": remote_exit, "log_file": log_file,
                "media_log_present": media_log_present,
                "sent": sent, "received": received, "signal_frames": signal_frames,
                "frequency_hz": frequency, "events": names, "stderr_present": bool(completed.stderr)}

    room = "rk-audio-" + uuid.uuid4().hex[:20]
    duplex, muted = [], None
    if args.scenario in ("all", "duplex"):
        print("board test: duplex audio", file=sys.stderr, flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(client, room, index, True) for index in range(2)]
            duplex = [future.result() for future in futures]
    if args.scenario in ("all", "default-muted"):
        muted = client("rk-muted-" + uuid.uuid4().hex[:20], 2, False)
    report = {"schema": 1, "remote_directory": remote, "board_tests": unit_results,
              "reused_deployment": bool(args.existing_deployment), "scenario": args.scenario,
              "duplex": duplex, "default_muted": muted,
              "ok": all(item["ok"] for item in duplex) and (muted is None or muted["ok"])}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SmokeFailure as error:
        print("board-audio-smoke: " + str(error), file=sys.stderr)
        sys.exit(2)
    except Exception as error:
        # Do not echo arbitrary exception strings / HTTP response bodies.
        print("board-audio-smoke: " + type(error).__name__ + "; no credentials recorded", file=sys.stderr)
        sys.exit(2)
