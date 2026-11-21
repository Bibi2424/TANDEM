"""Host-side Windows HIL verification script for T.A.N.D.E.M. USB HID Joystick.

Uses Win32 winmm.dll (joyGetDevCapsW / joyGetPosEx) with zero external packages.
Example:
  python tools/hil_usb_hid_check.py --duration 4.0
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import sys
import time


class JOYCAPSW(ctypes.Structure):
    _fields_ = [
        ("wMid", wintypes.WORD),
        ("wPid", wintypes.WORD),
        ("szPname", wintypes.WCHAR * 32),
        ("wXmin", wintypes.UINT),
        ("wXmax", wintypes.UINT),
        ("wYmin", wintypes.UINT),
        ("wYmax", wintypes.UINT),
        ("wZmin", wintypes.UINT),
        ("wZmax", wintypes.UINT),
        ("wNumButtons", wintypes.UINT),
        ("wPeriodMin", wintypes.UINT),
        ("wPeriodMax", wintypes.UINT),
        ("wRmin", wintypes.UINT),
        ("wRmax", wintypes.UINT),
        ("wUmin", wintypes.UINT),
        ("wUmax", wintypes.UINT),
        ("wVmin", wintypes.UINT),
        ("wVmax", wintypes.UINT),
        ("wCaps", wintypes.UINT),
        ("wMaxAxes", wintypes.UINT),
        ("wNumAxes", wintypes.UINT),
        ("wMaxButtons", wintypes.UINT),
        ("szRegKey", wintypes.WCHAR * 32),
        ("szOEMVxD", wintypes.WCHAR * 260),
    ]


class JOYINFOEX(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("dwXpos", wintypes.DWORD),
        ("dwYpos", wintypes.DWORD),
        ("dwZpos", wintypes.DWORD),
        ("dwRpos", wintypes.DWORD),
        ("dwUpos", wintypes.DWORD),
        ("dwVpos", wintypes.DWORD),
        ("dwButtons", wintypes.DWORD),
        ("dwButtonNumber", wintypes.DWORD),
        ("dwPOV", wintypes.DWORD),
        ("dwReserved1", wintypes.DWORD),
        ("dwReserved2", wintypes.DWORD),
    ]


def find_joystick(winmm: ctypes.WinDLL, vid: int, pid: int) -> tuple[int, JOYCAPSW] | None:
    for joy_id in range(winmm.joyGetNumDevs()):
        caps = JOYCAPSW()
        if winmm.joyGetDevCapsW(joy_id, ctypes.byref(caps), ctypes.sizeof(caps)) == 0:
            if caps.wMid == vid and caps.wPid == pid:
                return joy_id, caps
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify T.A.N.D.E.M. USB HID joystick on Windows")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x303A)
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x80C2)
    parser.add_argument("--duration", type=float, default=3.0, help="Sample window in seconds")
    parser.add_argument("--interval", type=float, default=0.25, help="Print interval in seconds")
    args = parser.parse_args()

    if sys.platform != "win32":
        print("This script uses Win32 winmm.dll and is intended for Windows hosts.", file=sys.stderr)
        return 2

    winmm = ctypes.WinDLL("winmm")
    found = find_joystick(winmm, args.vid, args.pid)
    if found is None:
        print(f"No joystick found matching VID=0x{args.vid:04X} PID=0x{args.pid:04X}", file=sys.stderr)
        return 1

    joy_id, caps = found
    print(
        f"Found Joy {joy_id}: VID=0x{caps.wMid:04X} PID=0x{caps.wPid:04X} "
        f"Axes={caps.wNumAxes} Btns={caps.wNumButtons} X=[{caps.wXmin},{caps.wXmax}]"
    )
    if caps.wNumButtons != 32:
        print(f"Expected 32 buttons, got {caps.wNumButtons}", file=sys.stderr)
        return 1

    info = JOYINFOEX(dwSize=ctypes.sizeof(JOYINFOEX), dwFlags=0xFF)
    t_start = time.monotonic()
    seen_btns: set[int] = set()
    min_axes = [65535] * 6
    max_axes = [0] * 6

    while time.monotonic() - t_start < args.duration:
        rc = winmm.joyGetPosEx(joy_id, ctypes.byref(info))
        if rc != 0:
            print(f"joyGetPosEx failed with code {rc}", file=sys.stderr)
            return 1
        vals = [info.dwXpos, info.dwYpos, info.dwZpos, info.dwRpos, info.dwUpos, info.dwVpos]
        for idx, val in enumerate(vals):
            min_axes[idx] = min(min_axes[idx], val)
            max_axes[idx] = max(max_axes[idx], val)
        seen_btns.add(info.dwButtons)
        print(
            f"  X={info.dwXpos:5d} Y={info.dwYpos:5d} Z={info.dwZpos:5d} "
            f"R={info.dwRpos:5d} U={info.dwUpos:5d} V={info.dwVpos:5d} "
            f"Btns=0x{info.dwButtons:08X} POV={info.dwPOV:5d}"
        )
        time.sleep(args.interval)

    print("Summary:")
    print(f"  Axis min: {min_axes}")
    print(f"  Axis max: {max_axes}")
    print(f"  Observed button masks: {[f'0x{b:08X}' for b in sorted(seen_btns)]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
