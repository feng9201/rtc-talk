#!/usr/bin/env python3
"""Standard-library WS fixture: regress actual server leave notification order."""
import base64
import hashlib
import json
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


def exact(connection, size):
    result = bytearray()
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise ValueError("unexpected EOF")
        result.extend(chunk)
    return bytes(result)


def receive(connection):
    first, second = exact(connection, 2)
    size = second & 127
    if size == 126:
        size = struct.unpack("!H", exact(connection, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(connection, 8))[0]
    if size > 256 * 1024 or not second & 128:
        raise ValueError("invalid client frame")
    mask = exact(connection, 4)
    return first & 15, bytes(c ^ mask[i % 4] for i, c in enumerate(exact(connection, size)))


def send(connection, value=None, opcode=1):
    data = json.dumps(value).encode() if opcode == 1 else struct.pack("!H", 1000)
    header = bytes([128 | opcode])
    header += bytes([len(data)]) if len(data) < 126 else bytes([126]) + struct.pack("!H", len(data))
    connection.sendall(header + data)


def fixture(listener, failures):
    try:
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(5)
            request = bytearray()
            while not request.endswith(b"\r\n\r\n"):
                request.extend(exact(connection, 1))
                if len(request) > 8192:
                    raise ValueError("oversized handshake")
            headers = dict(line.split(":", 1) for line in request.decode().split("\r\n")[1:] if ":" in line)
            assert headers["Authorization"].strip() == "Bearer test-token"
            digest = base64.b64encode(hashlib.sha1((headers["Sec-WebSocket-Key"].strip() +
                "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            connection.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Protocol: talk.v1.json\r\n"
                f"Sec-WebSocket-Accept: {digest}\r\n\r\n").encode())
            seen = []
            while True:
                opcode, payload = receive(connection)
                if opcode == 8:
                    send(connection, opcode=8)
                    break
                assert opcode == 1
                message = json.loads(payload)
                method = message["method"]
                seen.append(method)
                data = {}
                if method == "session.hello":
                    assert message["data"]["caps"]["p2p"] is False
                    data = {"session_id": "test-session", "uid": "test-user", "heartbeat_interval": 1}
                elif method == "room.join":
                    assert message["data"]["media"] == {"audio": {"send": False, "muted": True}, "video": {"send": False}}
                    data = {"room": {"room_id": "test-room"}, "media_mode": "sfu", "peers": []}
                elif method == "room.leave":
                    # Actual server behavior: last member's room closes first.
                    send(connection, {"v": 1, "type": "ntf", "method": "room.closed", "data": {"reason": "empty"}})
                elif method not in ("session.bye", "session.ping"):
                    raise ValueError("unexpected method")
                send(connection, {"v": 1, "type": "res", "id": message["id"], "ok": True, "data": data})
            assert "session.ping" in seen
            assert [method for method in seen if method != "session.ping"] == ["session.hello", "room.join", "room.leave", "session.bye"]
    except Exception:
        failures.append("fixture or client protocol mismatch")


def main():
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(6)
    failures = []
    worker = threading.Thread(target=fixture, args=(listener, failures), daemon=True)
    worker.start()
    with tempfile.TemporaryDirectory(prefix="rtc-rk-cli-test-") as temporary:
        result = subprocess.run([sys.argv[1], "--signaling-only", "--token-stdin", "--url",
            f"ws://127.0.0.1:{listener.getsockname()[1]}/talk/v1/ws", "--development-allow-ws",
            "--room", "test-room", "--duration", "2", "--log-file", str(Path(temporary) / "client.log")],
            input="test-token\n", text=True, capture_output=True, timeout=8)
        events = [json.loads(line)["name"] for line in result.stdout.splitlines()]
        assert result.returncode == 0, (result.returncode, events)
        assert all(name in events for name in ("session.ready", "room.joined", "room.left", "session.bye", "session.closed"))
        assert "error" not in events and "test-token" not in result.stdout
    worker.join(timeout=2)
    listener.close()
    assert not worker.is_alive() and not failures, failures
    print("PASS")


if __name__ == "__main__":
    main()
