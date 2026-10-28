#!/usr/bin/env python3
"""Send a short-lived virtual keyboard to muOS over SSH or adb.

Examples:
  setsid -w python3 tools/anbernic-virtual-gamepad.py press right A
  python3 tools/anbernic-virtual-gamepad.py --transport adb --serial DEVICE press B
  python3 tools/anbernic-virtual-gamepad.py --page 1 screenshot /tmp/tgmuos.png

SSH_ASKPASS, SSH_ASKPASS_REQUIRE=force and DISPLAY can supply SSH credentials
without putting them on the command line. The device needs Python 3 and
/dev/uinput for button commands; screenshots only require /dev/fb0.
"""

import argparse
import base64
import fcntl
import os
import pathlib
import struct
import subprocess
import sys
import time
import zlib

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_DEV_SETUP = 0x405C5503
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
EV_SYN = 0
EV_KEY = 1
UINPUT = "/dev/uinput"

KEYMAP = {
    "A": 28,       # Enter: activate
    "B": 14,       # Backspace: back
    "X": 45,
    "Y": 21,
    "START": 31,   # S
    "SELECT": 20,  # T
    "MENU": 1,     # Escape
    "UP": 103,
    "DOWN": 108,
    "LEFT": 105,
    "RIGHT": 106,
    "SPACE": 57,
    "ENTER": 28,
    "BACKSPACE": 14,
}
KEYCAPS = frozenset(KEYMAP.values()) | {30, 44}  # KEY_A and KEY_Z for keyboard detection


def key_code(name):
    try:
        return KEYMAP[name.upper()]
    except KeyError:
        raise ValueError("unknown button: " + name) from None


def emit(fd, event_type, code, value):
    now = time.time_ns()
    event = struct.pack("<qqHHi", now // 1_000_000_000,
                        now % 1_000_000_000 // 1000, event_type, code, value)
    written = os.write(fd, event)
    if written != len(event):
        raise OSError("short write to virtual keyboard")


def sync(fd):
    emit(fd, EV_SYN, 0, 0)


def press_key(fd, code, duration=0.06):
    emit(fd, EV_KEY, code, 1)
    sync(fd)
    time.sleep(duration)
    emit(fd, EV_KEY, code, 0)
    sync(fd)


def create_keyboard():
    fd = os.open(UINPUT, os.O_WRONLY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
        for code in sorted(KEYCAPS):
            fcntl.ioctl(fd, UI_SET_KEYBIT, code)
        setup = struct.pack("<HHHH80sI", 3, 1, 1, 1, b"tgmuos-remote", 0)
        fcntl.ioctl(fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(fd, UI_DEV_CREATE)
        return fd
    except Exception:
        os.close(fd)
        raise


def remote_press(buttons, combo=False):
    if not buttons:
        raise ValueError("at least one button is required")
    fd = create_keyboard()
    try:
        time.sleep(2.5)
        if combo:
            codes = [key_code(button) for group in buttons for button in group.split("+")]
            for code in codes:
                emit(fd, EV_KEY, code, 1)
            sync(fd)
            time.sleep(0.15)
            for code in reversed(codes):
                emit(fd, EV_KEY, code, 0)
            sync(fd)
        else:
            for button in buttons:
                press_key(fd, key_code(button))
                time.sleep(0.12)
    finally:
        try:
            fcntl.ioctl(fd, UI_DEV_DESTROY)
        finally:
            os.close(fd)


def png_chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def framebuffer_png(frame, width=640, height=480, stride=2560,
                    bits_per_pixel=32,
                    channels=((16, 8), (8, 8), (0, 8), (24, 8))):
    bytes_per_pixel = bits_per_pixel // 8
    if (bits_per_pixel not in (16, 24, 32) or width < 1 or height < 1 or
            stride < width * bytes_per_pixel or len(frame) < stride * height):
        raise ValueError("invalid framebuffer dimensions or data length")
    scanlines = bytearray()
    for y in range(height):
        row = frame[y * stride:y * stride + width * bytes_per_pixel]
        scanlines.append(0)
        for x in range(0, len(row), bytes_per_pixel):
            pixel = int.from_bytes(row[x:x + bytes_per_pixel], "little")
            for offset, length in channels:
                value = (pixel >> offset) & ((1 << length) - 1) if length else 0
                maximum = (1 << length) - 1
                scanlines.append(value * 255 // maximum if maximum else 255)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 6)) +
            png_chunk(b"IEND", b""))


def remote_screenshot(page=None):
    with open("/dev/fb0", "rb", buffering=0) as framebuffer:
        fixed = bytearray(80)
        variable = bytearray(160)
        fcntl.ioctl(framebuffer.fileno(), 0x4602, fixed, True)
        fcntl.ioctl(framebuffer.fileno(), 0x4600, variable, True)
        width, height, _, virtual_height, xoffset, yoffset, bits_per_pixel = (
            struct.unpack_from("<7I", variable))
        stride = struct.unpack_from("<I", fixed, 48)[0]
        channels = [struct.unpack_from("<2I", variable, 32 + 12 * index)
                    for index in range(4)]
        if page is not None:
            if (page + 1) * height > virtual_height:
                raise ValueError("framebuffer page is outside virtual screen")
            yoffset = page * height
        page_offset = yoffset * stride + xoffset * (bits_per_pixel // 8)
        os.lseek(framebuffer.fileno(), page_offset, os.SEEK_SET)
        frame = framebuffer.read(stride * height)
    if len(frame) != stride * height:
        raise RuntimeError("short framebuffer read")
    image = framebuffer_png(frame, width, height, stride, bits_per_pixel, channels)
    sys.stdout.write(base64.b64encode(image).decode("ascii") + "\n")


class Device:
    def __init__(self, transport="ssh", host="192.168.0.151", serial=None):
        if transport not in ("ssh", "adb"):
            raise ValueError("transport must be ssh or adb")
        self.transport = transport
        self.host = host
        self.serial = serial

    def run(self, action, arguments, options=()):
        if self.transport == "ssh":
            command = ["ssh", "-o", "ConnectTimeout=8", "root@" + self.host]
        else:
            command = ["adb"]
            if self.serial:
                command.extend(["-s", self.serial])
            command.append("shell")
        command.extend(["python3", "-", "--remote", *options, action, *arguments])
        source = pathlib.Path(__file__).read_bytes()
        result = subprocess.run(command, input=source, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=90, check=False)
        if result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", "replace").strip() or
                               "remote command failed")
        return result.stdout

    def screenshot(self, output, page=None):
        options = () if page is None else ("--page", str(page))
        data = base64.b64decode(self.run("screenshot", [], options).strip(), validate=True)
        pathlib.Path(output).write_bytes(data)


def remote_main(arguments):
    if arguments.action == "screenshot":
        remote_screenshot(arguments.page)
    else:
        remote_press(arguments.buttons, arguments.action == "combo")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transport", choices=("ssh", "adb"), default="ssh")
    parser.add_argument("--host", default="192.168.0.151")
    parser.add_argument("--serial")
    parser.add_argument("--page", type=int, choices=(0, 1),
                        help="framebuffer page for screenshot (default: current)")
    parser.add_argument("--remote", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("action", choices=("press", "combo", "screenshot"))
    parser.add_argument("buttons", nargs="*")
    arguments = parser.parse_args(argv)
    if arguments.action != "screenshot" and not arguments.buttons:
        parser.error("press/combo requires at least one button")
    if (arguments.action == "screenshot" and
            len(arguments.buttons) != (0 if arguments.remote else 1)):
        parser.error("screenshot requires one local output path")
    return arguments


def main(argv=None):
    arguments = parse_args(argv)
    if arguments.remote:
        remote_main(arguments)
    elif arguments.action == "screenshot":
        Device(arguments.transport, arguments.host, arguments.serial).screenshot(
            arguments.buttons[0], arguments.page)
    else:
        Device(arguments.transport, arguments.host, arguments.serial).run(
            arguments.action, arguments.buttons)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print("anbernic-virtual-gamepad: " + str(error), file=sys.stderr)
        sys.exit(1)
