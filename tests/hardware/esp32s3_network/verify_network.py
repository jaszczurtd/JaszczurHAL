#!/usr/bin/env python3
"""Host side of the ESP32-S3 network hardware test.

``run`` reads the suite, ports and host address the firmware announces and
serves them. For the basic suite: UDP/TCP echo, a TCP sink that never reads,
a TCP port that closes at once and a port that refuses connections; it then
probes the device's servers (server_probes.py), restarts and stops them. For
the SERVICES suite: NTP, TLS and plain HTTP peers, a peer that drops the TLS
handshake and one that never answers, MQTT and MQTTS brokers, and UDP/TCP
echo and MQTT on the WireGuard address.

``prepare-tls`` creates the test CA, the server certificate and the
firmware header ``net_test_tls.h`` before the SERVICES build.
"""

from __future__ import annotations

import argparse
import datetime
import ipaddress
import json
from pathlib import Path
import socket
import ssl
import struct
import sys
import threading
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
import server_probes  # noqa: E402

HERE = Path(__file__).resolve().parent
TLS_DIR = HERE / ".tls"
TLS_HEADER = HERE / "net_test_tls.h"
PREFIX = "JH_ESP32_NET "
REQUIRED_CHECKS = {
    "basic": {
        "wifi_mode", "scan", "auth_failure", "no_network", "join", "dns", "ping",
        "udp_echo", "udp_timeout", "tcp_echo", "tcp_refused", "tcp_peer_close",
        "tcp_backpressure", "concurrent_sockets", "bsd_tcp_echo",
        "bsd_udp_echo", "teardown_blocked_udp", "teardown_blocked_tcp",
        "teardown_rejoin", "stale_handle", "rejoin", "wifi_off", "wifi_on",
        "reinit_join", "servers_start", "servers_probe", "servers_restart",
        "servers_reprobe", "servers_stop",
    },
    "services": {
        "wifi_mode", "join", "tls_anchors", "tls_no_time", "wg_no_time",
        "ntp_unreachable", "ntp_fallback", "libc_time", "tls_valid",
        "tls_repeat", "tls_wrong_host", "tls_wrong_ca", "tls_expired",
        "tls_not_yet_valid", "tls_entropy_failure", "tls_cancel",
        "tls_transport_drop", "tls_timeout", "https_client", "http_client",
        "mqtt", "mqtts_configure", "mqtts", "mqtts_reconnect",
        "mqtts_wrong_host", "mqtts_disable", "mqtt_after_tls",
        "wg_split_handshake", "wg_split_udp", "wg_split_tcp", "wg_split_mqtt",
        "wg_split_lan", "wg_split_end", "wg_end_open_tcp", "wg_cycles", "wg_full_handshake",
        "wg_full_udp", "wg_full_tcp", "wg_full_end", "wg_hostname_handshake",
        "wg_hostname_udp", "wg_underlay_loss", "wg_rejoin",
        "wg_recovered_handshake", "wg_recovered_udp", "wg_recovered_end",
        "tcpip_stack",
    },
}
TLS_BODY = b"JH-ESP-TLS-OK"
NTP_PORT = 10123
NTP_EPOCH_OFFSET = 2_208_988_800
MAX_NTP_DRIFT_S = 5


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                         write_timeout=5.0, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = path
    port.open()
    return port


def parse(line: str) -> dict[str, str] | None:
    position = line.find(PREFIX)
    if position < 0:
        return None
    fields: dict[str, str] = {}
    for token in line[position + len(PREFIX):].split():
        key, separator, value = token.partition("=")
        if separator:
            fields[key] = value
    return fields


def listener(host: str, port: int, receive_buffer: int = 0) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if receive_buffer:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    server.bind((host, port))
    server.listen(8)
    return server


def serve_forever(server: socket.socket, handle) -> None:
    def loop() -> None:
        while True:
            try:
                connection, _ = server.accept()
            except OSError:
                return
            threading.Thread(target=guarded, args=(handle, connection),
                             daemon=True).start()
    threading.Thread(target=loop, daemon=True).start()


def guarded(handle, connection: socket.socket) -> None:
    try:
        handle(connection)
    except (OSError, ValueError, ConnectionError):
        connection.close()


def echo(connection: socket.socket) -> None:
    with connection:
        while True:
            data = connection.recv(4096)
            if not data:
                return
            connection.sendall(data)


def udp_echo(host: str, port: int) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind((host, port))

    def loop() -> None:
        while True:
            data, sender = server.recvfrom(2048)
            server.sendto(data, sender)
    threading.Thread(target=loop, daemon=True).start()


class BasicFixture:
    """UDP/TCP services of the basic suite on the host address."""

    def __init__(self, host: str, ports: dict[str, int]) -> None:
        self.sinks: list[socket.socket] = []
        closed = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if closed.connect_ex((host, ports["tcp_closed"])) == 0:
            raise RuntimeError(f"port {ports['tcp_closed']} must refuse connections")
        closed.close()
        udp_echo(host, ports["udp_echo"])
        serve_forever(listener(host, ports["tcp_echo"]), echo)
        # A small window fills quickly, so the device reaches backpressure.
        serve_forever(listener(host, ports["tcp_sink"], 4096), self.sinks.append)
        serve_forever(listener(host, ports["tcp_close"]), lambda c: c.close())


# ── SERVICES suite ──────────────────────────────────────────────────────────


def http_reply(connection: socket.socket) -> None:
    """Read one request head and answer it with the test body."""
    with connection:
        request = b""
        while b"\r\n\r\n" not in request:
            chunk = connection.recv(1024)
            if not chunk:
                return
            request += chunk
        connection.sendall(
            b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            + str(len(TLS_BODY)).encode() + b"\r\nConnection: close\r\n\r\n"
            + TLS_BODY)


def tls_wrapped(context: ssl.SSLContext, handle):
    def serve(connection: socket.socket) -> None:
        connection.settimeout(30)
        with context.wrap_socket(connection, server_side=True) as secure:
            handle(secure)
    return serve


def drop_after_hello(connection: socket.socket) -> None:
    with connection:
        connection.recv(4096)


def silent(connection: socket.socket, held: list[socket.socket]) -> None:
    held.append(connection)


def read_exact(connection: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("client closed the connection")
        data.extend(chunk)
    return bytes(data)


def remaining_length(length: int) -> bytes:
    encoded = bytearray()
    while True:
        digit = length % 128
        length //= 128
        encoded.append(digit | (0x80 if length else 0))
        if not length:
            return bytes(encoded)


def mqtt_echo(connection: socket.socket) -> None:
    """MQTT 3.1.1 broker for one client: echoes each QoS 0 publish."""
    with connection:
        connection.settimeout(30)
        while True:
            header = read_exact(connection, 1)[0]
            length, multiplier = 0, 1
            while True:
                digit = read_exact(connection, 1)[0]
                length += (digit & 0x7F) * multiplier
                if not digit & 0x80:
                    break
                multiplier *= 128
                if multiplier > 128 ** 3:
                    raise ValueError("malformed MQTT remaining length")
            body = read_exact(connection, length)
            kind = header >> 4
            if kind == 1:
                connection.sendall(b"\x20\x02\x00\x00")
            elif kind == 8:
                connection.sendall(b"\x90\x03" + body[:2] + b"\x00")
            elif kind == 3:
                connection.sendall(b"\x30" + remaining_length(len(body)) + body)
            elif kind == 12:
                connection.sendall(b"\xd0\x00")
            elif kind == 14:
                return


def ntp_timestamp(unix_seconds: float) -> bytes:
    ntp = unix_seconds + NTP_EPOCH_OFFSET
    seconds = int(ntp)
    return struct.pack("!II", seconds & 0xFFFFFFFF,
                       int((ntp - seconds) * (1 << 32)) & 0xFFFFFFFF)


def ntp_server(host: str, port: int) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind((host, port))

    def loop() -> None:
        while True:
            request, peer = server.recvfrom(512)
            received = time.time()
            if len(request) < 48 or request[0] & 0x07 != 3:
                continue
            response = bytearray(48)
            response[0] = 0x24  # LI=0, VN=4, mode=server
            response[1] = 1
            response[2] = request[2]
            response[3] = 0xEC
            response[12:16] = b"JHES"
            response[16:24] = ntp_timestamp(received - 0.001)
            response[24:32] = request[40:48]
            response[32:40] = ntp_timestamp(received)
            response[40:48] = ntp_timestamp(time.time())
            server.sendto(bytes(response), peer)
    threading.Thread(target=loop, daemon=True).start()


class ServicesFixture:
    """Peers of the SERVICES suite on the host and WireGuard addresses."""

    def __init__(self, host: str, ports: dict[str, int], wg_peer: str) -> None:
        load_tls_state(host)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(TLS_DIR / "server.pem", TLS_DIR / "server-key.pem")
        self.held: list[socket.socket] = []
        ntp_server(host, NTP_PORT)
        serve_forever(listener(host, ports["tls"]), tls_wrapped(context, http_reply))
        serve_forever(listener(host, ports["tls_drop"]), drop_after_hello)
        serve_forever(listener(host, ports["tls_silent"]),
                      lambda c: silent(c, self.held))
        serve_forever(listener(host, ports["http"]), http_reply)
        serve_forever(listener(host, ports["lan_tcp_echo"]), echo)
        serve_forever(listener(host, ports["mqtt"]), mqtt_echo)
        serve_forever(listener(host, ports["mqtts"]), tls_wrapped(context, mqtt_echo))
        try:
            udp_echo(wg_peer, ports["wg_udp_echo"])
            serve_forever(listener(wg_peer, ports["wg_tcp_echo"]), echo)
            serve_forever(listener(wg_peer, ports["mqtt"]), mqtt_echo)
        except OSError as error:
            raise RuntimeError(
                f"WireGuard address {wg_peer} is not up on this host; see README.md"
            ) from error


# ── Test certificates ───────────────────────────────────────────────────────


def tls_hostname(host: str) -> str:
    """Public wildcard-DNS name of the host address, used for TLS identity."""
    return host.replace(".", "-") + ".sslip.io"


def load_tls_state(host: str) -> dict:
    state_path = TLS_DIR / "state.json"
    if not state_path.is_file():
        raise RuntimeError("run prepare-tls first")
    state = json.loads(state_path.read_text(encoding="utf-8"))
    if state["host"] != host:
        raise RuntimeError(f"certificates were made for {state['host']}, "
                           f"the firmware uses {host}; run prepare-tls again")
    return state


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 12])
            for offset in range(0, len(data), 12)]
    return (f"static const unsigned char {name}[] = {{\n    "
            + ",\n    ".join(rows) + "};\n")


def prepare_tls(host: str, valid_days: int) -> dict:
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID

    now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    not_before = now - datetime.timedelta(days=1)
    not_after = now + datetime.timedelta(days=valid_days)

    def ca(name: str):
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
        certificate = (
            x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(not_before).not_valid_after(not_after)
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
            .add_extension(x509.KeyUsage(False, False, False, False, False, True,
                                         True, False, False), True)
            .sign(key, hashes.SHA256()))
        return key, certificate

    ca_key, ca_certificate = ca("JaszczurHAL ESP32 network test CA")
    _, other_certificate = ca("JaszczurHAL unrelated test CA")
    hostname = tls_hostname(host)
    server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    server_certificate = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, hostname)]))
        .issuer_name(ca_certificate.subject).public_key(server_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before).not_valid_after(not_after)
        .add_extension(x509.SubjectAlternativeName([
            x509.DNSName(hostname),
            x509.IPAddress(ipaddress.ip_address(host))]), False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
        .sign(ca_key, hashes.SHA256()))

    TLS_DIR.mkdir(mode=0o700, exist_ok=True)
    pem = serialization.Encoding.PEM
    (TLS_DIR / "server.pem").write_bytes(
        server_certificate.public_bytes(pem) + ca_certificate.public_bytes(pem))
    key_path = TLS_DIR / "server-key.pem"
    key_path.write_bytes(server_key.private_bytes(
        pem, serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption()))
    key_path.chmod(0o600)
    state = {"host": host, "hostname": hostname,
             "not_before": int(not_before.timestamp()),
             "not_after": int(not_after.timestamp())}
    (TLS_DIR / "state.json").write_text(json.dumps(state), encoding="utf-8")
    der = serialization.Encoding.DER
    TLS_HEADER.write_text(
        "#pragma once\n\n/* Generated by verify_network.py prepare-tls; "
        "ignored by Git. */\n"
        f"#define JH_NET_TEST_TLS_HOSTNAME \"{hostname}\"\n"
        f"#define JH_NET_TEST_TLS_NOT_BEFORE {state['not_before']}ull\n"
        f"#define JH_NET_TEST_TLS_NOT_AFTER {state['not_after']}ull\n\n"
        + c_array("kNetTestTlsCa", ca_certificate.public_bytes(der)) + "\n"
        + c_array("kNetTestTlsOtherCa", other_certificate.public_bytes(der)),
        encoding="utf-8")
    return state


def run(path: str, timeout_s: float, raw_log: Path | None = None,
        soak_s: int = 0) -> dict[str, object]:
    checks: dict[str, list[int]] = {}
    host_probe: dict[str, bool] = {}
    fixture: object | None = None
    host = ""
    serving: dict[str, str] = {}
    held: list[socket.socket] = []
    suite = ""
    result: dict[str, str] | None = None
    deadline = time.monotonic() + timeout_s + soak_s
    soak_pending = False
    port = open_port(path)
    raw = raw_log.open("w", encoding="utf-8") if raw_log else None
    try:
        buffer = b""
        while (result is None or soak_pending) and time.monotonic() < deadline:
            buffer += port.read(256)
            while b"\n" in buffer:
                raw_line, buffer = buffer.split(b"\n", 1)
                line = raw_line.decode("utf-8", errors="replace").strip()
                if raw:
                    raw.write(line + "\n")
                    raw.flush()
                if checks and line.startswith("rst:"):
                    raise RuntimeError(f"board reset during the suite: {line}; "
                                       f"checks so far: {checks}")
                fields = parse(line)
                if fields is None:
                    continue
                print(line, flush=True)
                # A line cut by a reset while the port opens is skipped.
                if fields.get("suite") not in REQUIRED_CHECKS:
                    fields.pop("ready", None)
                if "ready" in fields and fixture is None:
                    suite = fields["suite"]
                    host = fields["host"]
                    ports = {key: int(value) for key, value in fields.items()
                             if key.startswith(("udp_", "tcp_", "tls", "http",
                                                "lan_", "mqtt", "wg_udp", "wg_tcp"))}
                    fixture = (BasicFixture(fields["host"], ports)
                               if suite == "basic" else
                               ServicesFixture(fields["host"], ports,
                                               fields["wg_peer"]))
                if "ready" in fields:
                    # Repeated while the firmware waits: a GO written while
                    # the board still boots after the port opened is lost.
                    port.write(b"GO\r\n")
                elif fields.get("check") == "serve" and fields.get("phase") == "1":
                    host_probe.update(server_probes.phase_one(fields, host))
                    port.write(b"RESTART\r\n")
                elif fields.get("check") == "serve":
                    serving = fields
                    host_probe.update(server_probes.phase_two(fields))
                    held = server_probes.hold_clients(fields)
                    port.write(b"STOP\r\n")
                elif fields.get("check") == "stopped":
                    host_probe.update(server_probes.stopped(serving, held))
                    port.write(b"DONE\r\n")
                elif fields.get("check") == "result":
                    result = fields
                    if soak_s > 0:
                        port.write(f"SOAK {soak_s}\r\n".encode())
                        soak_pending = True
                elif fields.get("check") == "soak":
                    soak_pending = False
                    checks.setdefault("soak", []).append(int(fields.get("pass", "0")))
                elif "check" in fields:
                    passed = int(fields.get("pass", "0"))
                    if fields["check"] == "ntp_fallback":
                        drift = abs(int(fields.get("unix", "0")) - time.time())
                        host_probe["ntp_time"] = drift <= MAX_NTP_DRIFT_S
                    checks.setdefault(fields["check"], []).append(passed)
    finally:
        port.close()
        if raw:
            raw.close()
    if result is None:
        raise TimeoutError(f"no result line from {path}; checks so far: {checks}")
    required = REQUIRED_CHECKS[suite] | ({"soak"} if soak_s > 0 else set())
    missing = sorted(required - checks.keys())
    failed = sorted(name for name, passes in checks.items() if 0 in passes)
    expected_probes = server_probes.PROBES if suite == "basic" else {"ntp_time"}
    summary = {"suite": suite, "result": result, "missing": missing,
               "failed": failed, "host_probe": host_probe}
    if missing or failed or result.get("pass") != "1" or \
            not all(host_probe.values()) or set(host_probe) != expected_probes:
        raise RuntimeError(f"network test failed: {json.dumps(summary)}")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run_parser = commands.add_parser("run", help="serve the host side of a suite")
    run_parser.add_argument("--port", required=True)
    run_parser.add_argument("--timeout", type=float, default=600.0)
    run_parser.add_argument("--raw-log", type=Path,
                            help="also write every serial line to this file")
    run_parser.add_argument("--soak-seconds", type=int, default=0,
                            help="after the suite, run continuous traffic this long")
    tls_parser = commands.add_parser("prepare-tls",
                                     help="create the test CA and net_test_tls.h")
    tls_parser.add_argument("--host", required=True,
                            help="host address the firmware uses (JH_NET_TEST_HOST)")
    tls_parser.add_argument("--valid-days", type=int, default=30)
    args = parser.parse_args()
    if args.command == "prepare-tls":
        print(json.dumps(prepare_tls(args.host, args.valid_days), sort_keys=True))
        return 0
    if not Path(args.port).exists():
        raise SystemExit(f"serial port {args.port} does not exist")
    print(json.dumps(run(args.port, args.timeout, args.raw_log, args.soak_seconds),
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
