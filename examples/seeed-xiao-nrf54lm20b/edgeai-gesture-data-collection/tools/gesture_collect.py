#!/usr/bin/env python3
"""Collect labelled XIAO nRF54LM20B gesture recordings over USB CDC."""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - depends on the user's host
    raise SystemExit(
        "pyserial is required. Install it with: python -m pip install pyserial"
    ) from exc


SEEED_VID = 0x2886
XIAO_NRF54LM20B_PID = 0x8013
BAUD_RATE = 115200
CSV_HEADER = [
    "accel_x",
    "accel_y",
    "accel_z",
    "gyro_x",
    "gyro_y",
    "gyro_z",
    "label",
]
CSV_ROW = re.compile(r"^-?\d+(?:,-?\d+){5},[A-Za-z0-9_-]+$")


def find_xiao_port() -> str:
    """Return the only connected XIAO nRF54LM20B CDC port."""
    candidates = [
        port.device
        for port in list_ports.comports()
        if port.vid == SEEED_VID and port.pid == XIAO_NRF54LM20B_PID
    ]
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1:
        raise RuntimeError(
            "Multiple XIAO nRF54LM20B ports found; specify one with --port."
        )
    raise RuntimeError(
        "No XIAO nRF54LM20B USB CDC port found "
        f"(VID:PID={SEEED_VID:04X}:{XIAO_NRF54LM20B_PID:04X}). "
        "Connect the board or specify --port."
    )


def read_line(port: serial.Serial, timeout: float) -> str | None:
    """Read one decoded line, returning None when the timeout expires."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = port.readline()
        if raw:
            return raw.decode("ascii", errors="ignore").strip()
    return None


def wait_for_response(port: serial.Serial, expected: str, timeout: float = 2.0) -> None:
    """Wait for a firmware response while ignoring startup and CSV lines."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = read_line(port, min(0.2, max(0.0, deadline - time.monotonic())))
        if line and expected in line:
            return
    raise RuntimeError(f"Timed out waiting for board response: {expected}")


def send_command(port: serial.Serial, command: str, expected: str) -> None:
    port.write((command + "\r\n").encode("ascii"))
    port.flush()
    wait_for_response(port, expected)


def collect_recording(
    port: serial.Serial,
    label: str,
    output_file: Path,
    duration_seconds: float,
) -> int:
    """Capture one fixed-duration recording and return the number of rows."""
    port.reset_input_buffer()
    send_command(port, f"label {label}", f"ok: label={label}")
    send_command(port, "start", f"ok: recording label={label}")

    rows = 0
    deadline = time.monotonic() + duration_seconds
    with output_file.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(CSV_HEADER)
        while time.monotonic() < deadline:
            line = read_line(port, min(0.25, max(0.0, deadline - time.monotonic())))
            if line is None:
                continue
            if not CSV_ROW.fullmatch(line):
                continue
            fields = line.split(",")
            if fields[-1] != label:
                continue
            writer.writerow(fields)
            rows += 1
            if rows % 100 == 0:
                stream.flush()

    send_command(port, "stop", "ok: stopped")
    return rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect labelled gesture CSV recordings from a XIAO nRF54LM20B."
    )
    parser.add_argument(
        "--port",
        help="Serial port to use. If omitted, auto-detect VID:PID 2886:8013.",
    )
    parser.add_argument("--label", required=True, help="Gesture class label.")
    parser.add_argument(
        "--count", type=int, default=1, help="Number of recordings (default: 1)."
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=3.0,
        help="Recording duration in seconds (default: 3.0).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("gesture_dataset"),
        help="Dataset root directory (default: gesture_dataset).",
    )
    parser.add_argument(
        "--prepare",
        type=float,
        default=2.0,
        help="Countdown time before each recording (default: 2.0 seconds).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.count < 1 or args.duration <= 0 or args.prepare < 0:
        raise SystemExit("count must be >= 1, duration must be > 0, and prepare must be >= 0")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,31}", args.label):
        raise SystemExit("label must be 1-31 ASCII characters: letters, digits, '_' or '-'")

    try:
        port_name = args.port or find_xiao_port()
        label_dir = args.output / args.label
        label_dir.mkdir(parents=True, exist_ok=True)
        print(f"Using USB CDC port {port_name} (VID:PID=2886:8013)")
        print(f"Saving recordings under {label_dir}")
        with serial.Serial(port_name, BAUD_RATE, timeout=0.1) as port:
            time.sleep(0.5)
            port.reset_input_buffer()
            for index in range(1, args.count + 1):
                print(f"\nRecording {index}/{args.count} — label: {args.label}")
                if args.prepare:
                    end = time.monotonic() + args.prepare
                    while time.monotonic() < end:
                        remaining = max(0, int(end - time.monotonic() + 0.99))
                        print(f"Starting in {remaining}...", end="\r", flush=True)
                        time.sleep(min(0.25, max(0.01, end - time.monotonic())))
                    print(" " * 30, end="\r")
                path = label_dir / f"{args.label}_{index:03d}.csv"
                rows = collect_recording(port, args.label, path, args.duration)
                print(f"Saved {path} ({rows} samples)")
    except (OSError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
