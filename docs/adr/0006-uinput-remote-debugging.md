# ADR 0006: Short-lived uinput input and framebuffer screenshots

- **Status:** Accepted
- **Date:** 2026-10-28

## Context

Debugging controller navigation on muOS otherwise requires physical button
presses and a camera. A second framebuffer page can also contain the
application while the first page contains a stale muOS launcher image, making
a fixed-offset screenshot misleading.

SSH is normally available; adb is a secondary transport when the device is
connected and authorized. The debugging tool must not leave a background
service or persistent input device behind.

## Decision

Provide `tools/anbernic-virtual-gamepad.py`, a host-side Python 3 tool with no
third-party packages. It sends itself over SSH or adb to the device's Python
interpreter.

For `press` and `combo`, create a virtual Linux keyboard named
`tgmuos-remote` through `/dev/uinput`. Use the kernel's `UI_DEV_SETUP` layout
(`input_id`, device name, then force-feedback count), advertise the keyboard
capabilities expected by `keyboard.c`, and wait for the application's device
rescan. Destroy the device after the command, including error paths. The
application maps X, Y, Start, and Select from this named device to
controller-style SDL commands and suppresses their text input.

For `screenshot`, query `FBIOGET_FSCREENINFO` and `FBIOGET_VSCREENINFO` rather
than assuming a fixed stride or pixel layout. Honor the current x/y offset by
default. `--page 0` and `--page 1` explicitly select a framebuffer page; page
bounds are checked against `yres_virtual`. Convert the selected pixels to PNG
on the device and transfer base64 over the existing transport.

## Consequences

- Navigation can be exercised and verified with before/after screenshots.
- No persistent daemon, virtual input device, or Python package is installed.
- The device must expose `/dev/uinput` for buttons and `/dev/fb0` for images;
  SSH/adb credentials and authorization remain prerequisites.
- The correct framebuffer page is device-state-dependent and must be checked,
  not inferred from the existence of a running process.
- adb command construction is unit-tested, but a disconnected adb device cannot
  provide an end-to-end adb validation.

## Validation

- Python tests cover the ioctl setup layout, key events, cleanup on error,
  SSH/adb command construction, framebuffer metadata, and explicit page 1.
- On `192.168.0.151`, page 0 showed muOS Wi-Fi and page 1 showed TGMUOS.
- On that device, `press select` visibly opened Settings, and the temporary
  input device disappeared afterwards.
- A live adb transport test remains pending until adb reports a device.

## Related

- ADR 0002: SDL2 input and direct framebuffer rendering
- ADR 0005: On-demand Telegram chat folders and archive isolation
