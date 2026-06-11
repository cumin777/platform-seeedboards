#!/usr/bin/env python3
"""
UF2 upload tool for TinyUF2 bootloader.

Detects the UF2 mass storage device, optionally triggers bootloader mode
via serial DTR toggle, and copies the .uf2 firmware file.

Supports: Linux, Windows, macOS
"""

import os
import sys
import time
import shutil
import argparse
import subprocess
import platform

# ---------------------------------------------------------------------------
# UF2 drive detection
# ---------------------------------------------------------------------------

UF2_VOLUME_LABEL = "XIAOC5BOOT"
UF2_DRIVE_TIMEOUT = 30  # seconds to wait for drive to appear


def set_volume_label(label):
    """Override the default volume label for UF2 drive detection."""
    global UF2_VOLUME_LABEL
    UF2_VOLUME_LABEL = label


def _find_uf2_drive_linux():
    """Find UF2 drive on Linux using /dev/disk/by-label or lsblk."""
    # Method 1: /dev/disk/by-label/
    by_label = "/dev/disk/by-label"
    if os.path.isdir(by_label):
        for entry in os.listdir(by_label):
            if entry.upper() == UF2_VOLUME_LABEL.upper():
                dev = os.path.realpath(os.path.join(by_label, entry))
                # Find mount point
                try:
                    result = subprocess.run(
                        ["lsblk", "-no", "MOUNTPOINT", dev],
                        capture_output=True, text=True, timeout=5
                    )
                    mount = result.stdout.strip()
                    if mount and os.path.isdir(mount):
                        return mount
                except Exception:
                    pass
                # Try to mount
                mnt = "/mnt/uf2_upload"
                try:
                    os.makedirs(mnt, exist_ok=True)
                    subprocess.run(["mount", dev, mnt], capture_output=True, timeout=10)
                    time.sleep(0.5)
                    if os.path.ismount(mnt):
                        return mnt
                except Exception:
                    pass

    # Method 2: lsblk
    try:
        result = subprocess.run(
            ["lsblk", "-no", "LABEL,MOUNTPOINT", "-l"],
            capture_output=True, text=True, timeout=5
        )
        for line in result.stdout.strip().split("\n"):
            parts = line.strip().split(None, 1)
            if len(parts) == 2 and parts[0].upper() == UF2_VOLUME_LABEL.upper():
                mount = parts[1].strip()
                if mount and os.path.isdir(mount):
                    return mount
    except Exception:
        pass

    return None


def _find_uf2_drive_windows():
    """Find UF2 drive on Windows using wmic or powershell."""
    try:
        result = subprocess.run(
            ["powershell", "-Command",
             "Get-WmiObject Win32_LogicalDisk | "
             "Where-Object { $_.VolumeName -eq '" + UF2_VOLUME_LABEL + "' } | "
             "Select-Object -ExpandProperty DeviceID"],
            capture_output=True, text=True, timeout=10
        )
        drive = result.stdout.strip()
        if drive and os.path.isdir(drive):
            return drive
    except Exception:
        pass

    try:
        result = subprocess.run(
            ["wmic", "logicaldisk", "get", "volumename,name",
             "/format:list"],
            capture_output=True, text=True, timeout=10
        )
        lines = result.stdout.strip().split("\n")
        current_name = None
        for line in lines:
            line = line.strip()
            if line.startswith("Name="):
                current_name = line[5:]
            elif line.startswith("VolumeName="):
                volname = line[11:]
                if volname.upper() == UF2_VOLUME_LABEL.upper() and current_name:
                    drive = current_name
                    if os.path.isdir(drive):
                        return drive
                current_name = None
    except Exception:
        pass

    return None


def _find_uf2_drive_macos():
    """Find UF2 drive on macOS."""
    mnt = "/Volumes/" + UF2_VOLUME_LABEL
    if os.path.isdir(mnt):
        return mnt
    return None


def find_uf2_drive():
    """Find the UF2 mass storage device by volume label."""
    system = platform.system()
    if system == "Linux":
        return _find_uf2_drive_linux()
    elif system == "Windows":
        return _find_uf2_drive_windows()
    elif system == "Darwin":
        return _find_uf2_drive_macos()
    return None


def wait_for_uf2_drive(timeout=UF2_DRIVE_TIMEOUT):
    """Wait for UF2 drive to appear, return path or None."""
    start = time.time()
    dots = 0
    while time.time() - start < timeout:
        drive = find_uf2_drive()
        if drive:
            return drive
        time.sleep(0.5)
        dots += 1
        if dots % 4 == 0:
            sys.stdout.write(".\n")
            sys.stdout.flush()
        else:
            sys.stdout.write(".")
            sys.stdout.flush()
    return None


# ---------------------------------------------------------------------------
# Bootloader trigger
# ---------------------------------------------------------------------------

def trigger_bootloader_serial(port, baud=1200):
    """Try to trigger bootloader mode via serial DTR toggle.

    This simulates a double-tap reset by toggling DTR twice with a short
    delay, matching TinyUF2's double-tap detection window (500ms).
    """
    try:
        import serial
    except ImportError:
        print("  pyserial not installed, skipping serial trigger")
        return False

    try:
        s = serial.Serial(port, baud)
        # First reset: toggle DTR
        s.dtr = False
        time.sleep(0.05)
        s.dtr = True
        # Wait within the 500ms double-tap window
        time.sleep(0.2)
        # Second reset: toggle DTR again (double-tap)
        s.dtr = False
        time.sleep(0.05)
        s.dtr = True
        time.sleep(0.1)
        s.close()
        return True
    except Exception as e:
        print(f"  Serial trigger failed: {e}")
        return False


# ---------------------------------------------------------------------------
# UF2 file copy
# ---------------------------------------------------------------------------

def copy_uf2_to_drive(uf2_path, drive_path):
    """Copy UF2 file to the mass storage device."""
    dest = os.path.join(drive_path, os.path.basename(uf2_path))
    # On some systems, the drive might have a specific expected filename
    # TinyUF2 accepts any .uf2 file copied to the root
    try:
        shutil.copy2(uf2_path, dest)
        # Force sync to ensure data is flushed before drive disconnects
        if platform.system() == "Linux":
            try:
                subprocess.run(["sync"], timeout=10)
            except Exception:
                pass
        time.sleep(1)
        return True
    except Exception as e:
        print(f"  Copy failed: {e}")
        return False


# ---------------------------------------------------------------------------
# Main upload flow
# ---------------------------------------------------------------------------

def upload_uf2(uf2_path, serial_port=None, timeout=UF2_DRIVE_TIMEOUT):
    """Complete UF2 upload flow.

    Args:
        uf2_path: path to .uf2 firmware file
        serial_port: optional serial port for bootloader trigger
        timeout: seconds to wait for UF2 drive

    Returns:
        True on success, False on failure
    """
    if not os.path.isfile(uf2_path):
        print(f"Error: UF2 file not found: {uf2_path}")
        return False

    print(f"UF2 Upload: {uf2_path}")
    print(f"  Volume label: {UF2_VOLUME_LABEL}")
    print(f"  Firmware size: {os.path.getsize(uf2_path)} bytes")

    # Step 1: Check if drive already exists
    drive = find_uf2_drive()
    if drive:
        print(f"  Found UF2 drive: {drive}")
    else:
        # Step 2: Try serial trigger
        if serial_port:
            print(f"  Triggering bootloader via {serial_port}...")
            trigger_bootloader_serial(serial_port)
        else:
            print("  No serial port specified, attempting auto-detect...")
            # Try common serial ports
            for candidate in _detect_serial_ports():
                print(f"  Trying {candidate}...")
                trigger_bootloader_serial(candidate)

        # Step 3: Wait for drive
        print(f"  Waiting for UF2 drive (double-tap RESET if needed, "
              f"timeout {timeout}s)...", end="")
        sys.stdout.flush()

        drive = wait_for_uf2_drive(timeout)
        if not drive:
            print("\nError: UF2 drive not found.")
            print(f"  Please double-tap the RESET button on your board "
                  f"to enter bootloader mode.")
            print(f"  The drive should appear as '{UF2_VOLUME_LABEL}'.")
            return False
        print(f"\n  Found UF2 drive: {drive}")

    # Step 4: Copy .uf2 file
    print("  Copying firmware...")
    if copy_uf2_to_drive(uf2_path, drive):
        print("  Upload complete! Board will restart automatically.")
        return True
    else:
        print("  Error: Failed to copy UF2 file.")
        return False


def _detect_serial_ports():
    """Auto-detect available serial ports."""
    ports = []
    try:
        import serial.tools.list_ports
        for info in serial.tools.list_ports.comports():
            ports.append(info.device)
    except Exception:
        # Fallback: scan common paths
        if platform.system() == "Linux":
            for p in ["/dev/ttyACM0", "/dev/ttyACM1",
                      "/dev/ttyUSB0", "/dev/ttyUSB1"]:
                if os.path.exists(p):
                    ports.append(p)
        elif platform.system() == "Windows":
            for i in range(10):
                ports.append(f"COM{i}")
    return ports


def main():
    parser = argparse.ArgumentParser(
        description="Upload UF2 firmware to TinyUF2 bootloader"
    )
    parser.add_argument(
        "uf2_file",
        help="Path to .uf2 firmware file"
    )
    parser.add_argument(
        "-p", "--port", default=None,
        help="Serial port for bootloader trigger"
    )
    parser.add_argument(
        "--label", default=None,
        help="Volume label of UF2 drive (default: XIAOC5BOOT)"
    )
    parser.add_argument(
        "-t", "--timeout", type=int, default=UF2_DRIVE_TIMEOUT,
        help=f"Timeout in seconds (default: {UF2_DRIVE_TIMEOUT})"
    )
    parser.add_argument(
        "--trigger-only", action="store_true",
        help="Only trigger bootloader, do not upload"
    )

    args = parser.parse_args()

    # Override volume label if specified
    if args.label:
        set_volume_label(args.label)

    if args.trigger_only:
        if args.port:
            print(f"Triggering bootloader via {args.port}...")
            trigger_bootloader_serial(args.port)
        else:
            for port in _detect_serial_ports():
                print(f"Trying {port}...")
                trigger_bootloader_serial(port)
        return

    success = upload_uf2(args.uf2_file, args.port, args.timeout)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
