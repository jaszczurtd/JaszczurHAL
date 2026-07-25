#!/usr/bin/env python3

import argparse
import hashlib
import json
import threading
import time

import serial


def deterministic_payload(length: int, salt: int) -> bytes:
    return bytes(((index * 73 + salt * 29) & 0xFF) for index in range(length))


def read_exact(port: serial.Serial, length: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while len(received) < length:
        chunk = port.read(length - len(received))
        if chunk:
            received.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"received {len(received)} of {length} expected bytes"
            )
    return bytes(received)


def drain(port: serial.Serial) -> None:
    port.reset_input_buffer()
    port.reset_output_buffer()


def exchange(
    port: serial.Serial, payload: bytes, timeout_s: float, pause_before_read_s: float
) -> float:
    drain(port)
    started = time.monotonic()
    writer_result = {"written": 0, "error": None}

    def write_payload() -> None:
        try:
            writer_result["written"] = port.write(payload)
            port.flush()
        except Exception as exc:  # Propagate the writer failure on the main thread.
            writer_result["error"] = exc

    writer = threading.Thread(target=write_payload, name="cdc-probe-writer")
    writer.start()
    if pause_before_read_s > 0.0:
        time.sleep(pause_before_read_s)
    echoed = read_exact(port, len(payload), timeout_s)
    writer.join(timeout_s)
    if writer.is_alive():
        raise TimeoutError("CDC writer did not finish")
    if writer_result["error"] is not None:
        raise writer_result["error"]
    if writer_result["written"] != len(payload):
        raise RuntimeError(
            f"wrote {writer_result['written']} of {len(payload)} bytes"
        )
    elapsed = time.monotonic() - started
    if echoed != payload:
        mismatch = next(
            (
                index
                for index, (expected, actual) in enumerate(zip(payload, echoed))
                if expected != actual
            ),
            min(len(payload), len(echoed)),
        )
        raise RuntimeError(f"echo mismatch at byte {mismatch}")
    return elapsed


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path,
        baudrate=115200,
        timeout=0.1,
        write_timeout=10.0,
        exclusive=True,
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--bytes", type=int, default=262144)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--backpressure-pause", type=float, default=0.75)
    args = parser.parse_args()

    phases = []
    with open_port(args.port) as port:
        payload = deterministic_payload(4096, 1)
        elapsed = exchange(port, payload, args.timeout, 0.0)
        phases.append(("initial", payload, elapsed))

        payload = deterministic_payload(args.bytes, 2)
        elapsed = exchange(
            port, payload, args.timeout, args.backpressure_pause
        )
        phases.append(("throughput_backpressure", payload, elapsed))

    time.sleep(0.25)

    with open_port(args.port) as port:
        payload = deterministic_payload(16384, 3)
        elapsed = exchange(port, payload, args.timeout, 0.0)
        phases.append(("reconnect", payload, elapsed))

    result = {
        "port": args.port,
        "phases": [
            {
                "name": name,
                "bytes": len(payload),
                "elapsedSeconds": round(elapsed, 3),
                "bytesPerSecond": round(len(payload) / elapsed),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            for name, payload, elapsed in phases
        ],
        "status": "pass",
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
