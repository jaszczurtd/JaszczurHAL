"""Host probes of the device servers in the basic suite.

Each probe returns True when the device behaved as specified. Probes of one
phase run in order against the servers the firmware announced on its
``check=serve`` line.
"""

from __future__ import annotations

import base64
import hashlib
import http.client
import os
import socket
import subprocess
import time

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CONSOLE_PASSWORD = b"jh-esp-console"
UPLOAD_TOKEN = "jh-esp-upload"
TIMEOUT_S = 5.0


def request(ip: str, port: int, method: str, path: str, body: bytes | None = None,
            headers: dict[str, str] | None = None) -> tuple[int, dict[str, str], bytes]:
    connection = http.client.HTTPConnection(ip, port, timeout=TIMEOUT_S)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        reply = connection.getresponse()
        return reply.status, {k.lower(): v for k, v in reply.getheaders()}, reply.read()
    finally:
        connection.close()


def read_until_closed(connection: socket.socket, limit_s: float) -> tuple[bytes, float]:
    """Everything the peer sends until it closes, and how long that took."""
    start = time.monotonic()
    data = b""
    connection.settimeout(0.5)
    while time.monotonic() - start < limit_s:
        try:
            chunk = connection.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data, time.monotonic() - start


def raw_exchange(ip: str, port: int, payload: bytes, limit_s: float = 8.0) -> bytes:
    with socket.create_connection((ip, port), timeout=TIMEOUT_S) as connection:
        connection.sendall(payload)
        return read_until_closed(connection, limit_s)[0]


# ── HTTP and files ──────────────────────────────────────────────────────────


def http_probes(ip: str, port: int) -> dict[str, bool]:
    results: dict[str, bool] = {}
    status, _, body = request(ip, port, "GET", "/jh-net")
    results["http_get"] = status == 200 and body == b"JH-ESP-HTTP-OK"
    status, headers, body = request(ip, port, "HEAD", "/jh-net")
    results["http_head"] = status == 200 and body == b"" and \
        headers.get("content-length") == "14"
    status, _, body = request(ip, port, "POST", "/echo", b"JH-POST-BODY")
    results["http_post"] = status == 201 and body == b"JH-POST-BODY"
    results["http_404"] = request(ip, port, "GET", "/missing")[0] == 404
    results["http_500"] = request(ip, port, "GET", "/fail")[0] == 500
    reply = raw_exchange(ip, port, b"BOGUS\r\n\r\n")
    results["http_malformed"] = reply.startswith(b"HTTP/1.1 400")

    status, headers, body = request(ip, port, "GET", "/fs/hello.txt")
    etag = headers.get("etag", "")
    results["files_get"] = status == 200 and body == b"JH-ESP-FILE\n" and bool(etag)
    results["files_not_modified"] = request(
        ip, port, "GET", "/fs/hello.txt", headers={"If-None-Match": etag})[0] == 304
    results["files_upload_denied"] = request(
        ip, port, "PUT", "/fs/upload.txt", b"JH-UPLOAD")[0] == 403
    status = request(ip, port, "PUT", "/fs/upload.txt", b"JH-UPLOAD",
                     {"X-JH-Token": UPLOAD_TOKEN})[0]
    stored = request(ip, port, "GET", "/fs/upload.txt")
    results["files_upload"] = status == 201 and stored[0] == 200 and \
        stored[2] == b"JH-UPLOAD"
    results["files_traversal"] = request(
        ip, port, "GET", "/fs/../secret")[0] in (400, 403, 404)

    status, _, body = request(ip, port, "POST", "/cmd", b'{"cmd":"ping"}',
                              {"Content-Type": "application/json"})
    results["commands_http"] = status == 200 and b"pong" in body
    results["commands_unknown"] = request(
        ip, port, "POST", "/cmd", b'{"cmd":"jh-none"}')[0] == 404
    results["commands_bad_json"] = request(
        ip, port, "POST", "/cmd", b'{"cmd":')[0] == 400
    return results


def http_slow_client(ip: str, port: int) -> bool:
    """A client that never finishes its request is timed out with 408 while
    another client is served."""
    with socket.create_connection((ip, port), timeout=TIMEOUT_S) as slow:
        slow.sendall(b"GET /jh-net HTTP/1.1\r\nHost: x\r\n")
        served = request(ip, port, "GET", "/jh-net")[0] == 200
        reply, waited = read_until_closed(slow, 30.0)
    return served and reply.startswith(b"HTTP/1.1 408") and waited < 30.0


def http_pool_exhaustion(ip: str, port: int, idle_clients: int = 3) -> bool:
    """Idle connections fill the client pool; the server frees them on their
    first-byte timeout and serves a real request afterwards."""
    idle = [socket.create_connection((ip, port), timeout=TIMEOUT_S)
            for _ in range(idle_clients)]
    try:
        start = time.monotonic()
        while time.monotonic() - start < 20.0:
            try:
                if request(ip, port, "GET", "/jh-net")[0] == 200:
                    return True
            except OSError:
                time.sleep(0.5)
        return False
    finally:
        for connection in idle:
            connection.close()


# ── WebSocket ───────────────────────────────────────────────────────────────


def ws_connect(ip: str, port: int) -> tuple[socket.socket, bytes]:
    key = base64.b64encode(os.urandom(16)).decode()
    connection = socket.create_connection((ip, port), timeout=TIMEOUT_S)
    connection.sendall(
        (f"GET /ws HTTP/1.1\r\nHost: {ip}\r\nUpgrade: websocket\r\n"
         f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
         "Sec-WebSocket-Version: 13\r\n\r\n").encode())
    response = b""
    while b"\r\n\r\n" not in response:
        chunk = connection.recv(1024)
        if not chunk:
            break
        response += chunk
    head, _, rest = response.partition(b"\r\n\r\n")
    accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest())
    if b" 101 " not in head or accept not in head:
        connection.close()
        raise ConnectionError(head.split(b"\r\n", 1)[0].decode(errors="replace"))
    return connection, rest


def ws_frame(payload: bytes, masked: bool = True, opcode: int = 0x1) -> bytes:
    header = bytes([0x80 | opcode])
    length = len(payload)
    mask_bit = 0x80 if masked else 0
    if length < 126:
        header += bytes([mask_bit | length])
    else:
        header += bytes([mask_bit | 126]) + length.to_bytes(2, "big")
    if not masked:
        return header + payload
    mask = os.urandom(4)
    return header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


def ws_read_text(connection: socket.socket, pending: bytes) -> bytes | None:
    data = pending
    while len(data) < 2 or len(data) < 2 + (data[1] & 0x7F):
        chunk = connection.recv(1024)
        if not chunk:
            return None
        data += chunk
    if data[0] != 0x81:
        return None
    return data[2:2 + (data[1] & 0x7F)]


def ws_exchange(ip: str, port: int, payload: bytes) -> bytes | None:
    connection, pending = ws_connect(ip, port)
    with connection:
        connection.sendall(ws_frame(payload))
        return ws_read_text(connection, pending)


def ws_closes_on(ip: str, port: int, frame: bytes) -> bool:
    """The server ends the session (close frame or EOF) after a bad frame."""
    connection, _ = ws_connect(ip, port)
    with connection:
        connection.sendall(frame)
        reply, waited = read_until_closed(connection, 6.0)
    return waited < 6.0 and (reply == b"" or reply[0] == 0x88)


def ws_probes(ip: str, port: int) -> dict[str, bool]:
    results = {
        "ws_echo": ws_exchange(ip, port, b"JH-ESP-WS-REQ") == b"JH-ESP-WS-OK",
    }
    reply = ws_exchange(ip, port, b'{"cmd":"ping"}')
    results["ws_command"] = reply is not None and b"pong" in reply
    results["ws_unmasked"] = ws_closes_on(ip, port, ws_frame(b"JH-UNMASKED", masked=False))
    results["ws_oversize"] = ws_closes_on(ip, port, ws_frame(b"x" * 300))
    sessions = [ws_connect(ip, port)[0] for _ in range(2)]
    try:
        try:
            extra, _ = ws_connect(ip, port)
            extra.close()
            refused = False
        except ConnectionError as error:
            refused = "503" in str(error)
    finally:
        for connection in sessions:
            connection.close()
    time.sleep(0.5)
    results["ws_pool"] = refused and ws_exchange(
        ip, port, b"JH-ESP-WS-REQ") == b"JH-ESP-WS-OK"
    return results


# ── Net console ─────────────────────────────────────────────────────────────


def console_read(connection: socket.socket, marker: bytes, limit_s: float = 5.0) -> bytes:
    data = b""
    start = time.monotonic()
    connection.settimeout(0.5)
    while marker not in data and time.monotonic() - start < limit_s:
        try:
            chunk = connection.recv(1024)
        except socket.timeout:
            continue
        if not chunk:
            break
        data += chunk
    return data


def console_login(ip: str, port: int, password: bytes) -> tuple[socket.socket, bytes]:
    connection = socket.create_connection((ip, port), timeout=TIMEOUT_S)
    greeting = console_read(connection, b"Password: ")
    if b"Password: " not in greeting:
        return connection, greeting
    connection.sendall(password + b"\n")
    return connection, console_read(connection, b"\r\n> " if password == CONSOLE_PASSWORD
                                    else b"ERR auth")


def console_probes(ip: str, port: int) -> dict[str, bool]:
    results: dict[str, bool] = {}
    connection, reply = console_login(ip, port, b"jh-wrong")
    with connection:
        results["console_auth_failure"] = b"ERR auth" in reply
    connection, reply = console_login(ip, port, CONSOLE_PASSWORD)
    with connection:
        connection.sendall(b"ping\n")
        results["console_command"] = b"OK" in reply and \
            b"JH-ESP-CONSOLE-OK" in console_read(connection, b"JH-ESP-CONSOLE-OK")
    held = [console_login(ip, port, CONSOLE_PASSWORD)[0] for _ in range(2)]
    try:
        with socket.create_connection((ip, port), timeout=TIMEOUT_S) as extra:
            results["console_busy"] = b"ERR busy" in console_read(extra, b"ERR busy")
    finally:
        for connection in held:
            connection.close()
    return results


# ── IPv6 ────────────────────────────────────────────────────────────────────


def link_local_from_mac(mac: str) -> str:
    octets = [int(part, 16) for part in mac.split(":")]
    octets[0] ^= 0x02
    eui = octets[:3] + [0xFF, 0xFE] + octets[3:]
    groups = [f"{eui[i] << 8 | eui[i + 1]:x}" for i in range(0, 8, 2)]
    return "fe80::" + ":".join(groups)


def interface_of(address: str) -> str:
    """Host interface that owns the IPv4 address the firmware talks to."""
    output = subprocess.run(["ip", "-o", "-4", "addr", "show"], check=True,
                            capture_output=True, text=True).stdout
    for line in output.splitlines():
        fields = line.split()
        if f"{address}/" in line:
            return fields[1]
    raise RuntimeError(f"no host interface holds {address}")


def ipv6_probes(mac: str, host: str, udp_port: int, tcp_port: int) -> dict[str, bool]:
    scope = socket.if_nametoindex(interface_of(host))
    target = link_local_from_mac(mac)
    results: dict[str, bool] = {}
    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as udp:
        udp.settimeout(2.0)
        reply = None
        for _ in range(5):  # the address may still be tentative (DAD)
            udp.sendto(b"JH-ESP-V6-UDP", (target, udp_port, 0, scope))
            try:
                reply = udp.recvfrom(64)[0]
                break
            except socket.timeout:
                continue
        results["ipv6_udp"] = reply == b"JH-ESP-V6-UDP"
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as tcp:
            tcp.settimeout(TIMEOUT_S)
            tcp.connect((target, tcp_port, 0, scope))
            tcp.sendall(b"JH-ESP-V6-TCP")
            data = b""
            while len(data) < 13:
                chunk = tcp.recv(64)
                if not chunk:
                    break
                data += chunk
            results["ipv6_tcp"] = data == b"JH-ESP-V6-TCP"
    except OSError:
        results["ipv6_tcp"] = False
    return results


# ── Phases ──────────────────────────────────────────────────────────────────


PHASE_ONE_PROBES = {
    "http_get", "http_head", "http_post", "http_404", "http_500",
    "http_malformed", "files_get", "files_not_modified", "files_upload_denied",
    "files_upload", "files_traversal", "commands_http", "commands_unknown",
    "commands_bad_json", "ws_echo", "ws_command", "ws_unmasked", "ws_oversize",
    "ws_pool", "console_auth_failure", "console_command", "console_busy",
    "ipv6_udp", "ipv6_tcp", "http_slow_client", "http_pool_exhaustion",
}
PROBES = PHASE_ONE_PROBES | {
    "restart_http", "restart_ws", "stop_closes_clients", "stop_refuses_clients",
}


def guarded(name: str, probe) -> dict[str, bool]:
    """Run one probe group; an exception fails the group instead of the run."""
    try:
        result = probe()
    except (OSError, ConnectionError, http.client.HTTPException) as error:
        print(f"probe {name} failed: {error!r}", flush=True)
        return {f"{name}_error": False}
    return result if isinstance(result, dict) else {name: bool(result)}


def phase_one(fields: dict[str, str], host: str) -> dict[str, bool]:
    ip = fields["ip"]
    http_port, ws_port = int(fields["http"]), int(fields["ws"])
    results = guarded("http", lambda: http_probes(ip, http_port))
    results.update(guarded("ws", lambda: ws_probes(ip, ws_port)))
    results.update(guarded("console", lambda: console_probes(ip, int(fields["console"]))))
    results.update(guarded("ipv6", lambda: ipv6_probes(
        fields["mac"], host, int(fields["ipv6_udp"]), int(fields["ipv6_tcp"]))))
    results.update(guarded("http_slow_client", lambda: http_slow_client(ip, http_port)))
    results.update(guarded("http_pool_exhaustion",
                           lambda: http_pool_exhaustion(ip, http_port)))
    return results


def phase_two(fields: dict[str, str]) -> dict[str, bool]:
    ip = fields["ip"]
    results = guarded("restart_http", lambda: request(
        ip, int(fields["http"]), "GET", "/jh-net")[0] == 200)
    results.update(guarded("restart_ws", lambda: ws_exchange(
        ip, int(fields["ws"]), b"JH-ESP-WS-REQ") == b"JH-ESP-WS-OK"))
    return results


def hold_clients(fields: dict[str, str]) -> list[socket.socket]:
    """Open one connection to every server before the board stops them."""
    ip = fields["ip"]
    held = [socket.create_connection((ip, int(fields["http"])), timeout=TIMEOUT_S),
            ws_connect(ip, int(fields["ws"]))[0],
            console_login(ip, int(fields["console"]), CONSOLE_PASSWORD)[0]]
    return held


def stopped(fields: dict[str, str], held: list[socket.socket]) -> dict[str, bool]:
    ended = []
    for connection in held:
        with connection:
            ended.append(read_until_closed(connection, 5.0)[1] < 5.0)
    refused = []
    for key in ("http", "ws", "console"):
        try:
            socket.create_connection((fields["ip"], int(fields[key])), timeout=2.0).close()
            refused.append(False)
        except OSError:
            refused.append(True)
    return {"stop_closes_clients": all(ended), "stop_refuses_clients": all(refused)}

