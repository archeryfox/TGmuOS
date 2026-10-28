import base64
import importlib.util
import pathlib
import struct
import subprocess
import tempfile
import unittest
import unittest.mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "anbernic_virtual_gamepad", ROOT / "tools" / "anbernic-virtual-gamepad.py"
)
gamepad = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gamepad)


class VirtualGamepadTests(unittest.TestCase):
    def test_keyboard_navigation_uses_linux_key_codes(self):
        self.assertEqual(gamepad.KEYMAP["UP"], 103)
        self.assertEqual(gamepad.KEYMAP["DOWN"], 108)
        self.assertEqual(gamepad.KEYMAP["LEFT"], 105)
        self.assertEqual(gamepad.KEYMAP["RIGHT"], 106)
        self.assertEqual(gamepad.KEYMAP["A"], 28)
        self.assertEqual(gamepad.KEYMAP["B"], 14)
        self.assertEqual(gamepad.KEYMAP["START"], 31)
        self.assertEqual(gamepad.KEYMAP["SELECT"], 20)
        for code in (30, 44, 57, 28):
            self.assertIn(code, gamepad.KEYCAPS)

    def test_press_creates_keyboard_emits_press_release_and_destroys_it(self):
        with unittest.mock.patch.object(gamepad.os, "open", return_value=7), \
                unittest.mock.patch.object(gamepad.os, "write", return_value=24) as write, \
                unittest.mock.patch.object(gamepad.os, "close") as close, \
                unittest.mock.patch.object(gamepad.fcntl, "ioctl") as ioctl, \
                unittest.mock.patch.object(gamepad.time, "sleep") as sleep:
            gamepad.remote_press(["right"])
        calls = ioctl.call_args_list
        self.assertEqual(calls[0].args[:2], (7, gamepad.UI_SET_EVBIT))
        self.assertEqual(calls[1].args[:2], (7, gamepad.UI_SET_KEYBIT))
        setup = next(call.args[2] for call in calls
                     if call.args[1] == gamepad.UI_DEV_SETUP)
        self.assertEqual(len(setup), 92)
        self.assertEqual(struct.unpack("<HHHH80sI", setup),
                         (3, 1, 1, 1, b"tgmuos-remote" + bytes(67), 0))
        self.assertEqual(calls[-2].args[:2], (7, gamepad.UI_DEV_CREATE))
        self.assertEqual(calls[-1].args[:2], (7, gamepad.UI_DEV_DESTROY))
        events = [struct.unpack("<qqHHi", call.args[1])
                  for call in write.call_args_list]
        self.assertEqual([(event[2], event[3], event[4]) for event in events], [
            (gamepad.EV_KEY, 106, 1), (gamepad.EV_SYN, 0, 0),
            (gamepad.EV_KEY, 106, 0), (gamepad.EV_SYN, 0, 0),
        ])
        sleep.assert_any_call(2.5)
        close.assert_called_once_with(7)

    def test_failed_write_still_destroys_virtual_keyboard(self):
        with unittest.mock.patch.object(gamepad.os, "open", return_value=7), \
                unittest.mock.patch.object(gamepad.os, "write", side_effect=OSError("gone")), \
                unittest.mock.patch.object(gamepad.os, "close"), \
                unittest.mock.patch.object(gamepad.fcntl, "ioctl") as ioctl, \
                unittest.mock.patch.object(gamepad.time, "sleep"):
            with self.assertRaises(OSError):
                gamepad.remote_press(["A"])
        self.assertEqual(ioctl.call_args_list[-1].args[:2],
                         (7, gamepad.UI_DEV_DESTROY))

    def test_ssh_transport_sends_script_to_remote_stdin(self):
        completed = subprocess.CompletedProcess([], 0, stdout=b"", stderr=b"")
        with unittest.mock.patch.object(gamepad.subprocess, "run",
                                        return_value=completed) as run:
            gamepad.Device("ssh", host="192.0.2.10").run("press", ["right"])
        command = run.call_args.args[0]
        self.assertEqual(command[-6:],
                         ["root@192.0.2.10", "python3", "-", "--remote", "press", "right"])
        self.assertIn(b"UI_DEV_CREATE", run.call_args.kwargs["input"])

    def test_adb_transport_can_select_usb_device(self):
        completed = subprocess.CompletedProcess([], 0, stdout=b"", stderr=b"")
        with unittest.mock.patch.object(gamepad.subprocess, "run",
                                        return_value=completed) as run:
            gamepad.Device("adb", serial="device-1").run("press", ["A"])
        self.assertEqual(run.call_args.args[0][:3], ["adb", "-s", "device-1"])

    def test_framebuffer_conversion_preserves_bgra_pixel(self):
        png = gamepad.framebuffer_png(bytes([10, 20, 30, 255]), 1, 1, 4)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        compressed_length = struct.unpack(">I", png[33:37])[0]
        self.assertEqual(png[37:41], b"IDAT")
        image_data = __import__("zlib").decompress(png[41:41 + compressed_length])
        self.assertEqual(image_data, b"\x00\x1e\x14\x0a\xff")

    def test_screenshot_writes_decoded_remote_png(self):
        png = gamepad.framebuffer_png(bytes([0, 0, 255, 255]), 1, 1, 4)
        completed = subprocess.CompletedProcess(
            [], 0, stdout=base64.b64encode(png) + b"\n", stderr=b""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "screen.png"
            with unittest.mock.patch.object(gamepad.subprocess, "run",
                                            return_value=completed):
                gamepad.Device("ssh").screenshot(output)
            self.assertEqual(output.read_bytes(), png)

    def test_remote_argument_is_not_forwarded_as_a_button(self):
        arguments = gamepad.parse_args(["--remote", "press", "right"])
        self.assertTrue(arguments.remote)
        self.assertEqual(arguments.buttons, ["right"])

    def test_remote_screenshot_does_not_require_a_local_path(self):
        arguments = gamepad.parse_args(["--remote", "screenshot"])
        self.assertTrue(arguments.remote)
        self.assertEqual(arguments.buttons, [])

    def test_remote_screenshot_uses_kernel_framebuffer_layout(self, page=None):
        fixed = bytearray(80)
        struct.pack_into("<I", fixed, 48, 4)
        variable = bytearray(160)
        struct.pack_into("<7I", variable, 0, 1, 1, 1, 2, 0, 2, 32)
        for index, channel in enumerate(((16, 8, 0), (8, 8, 0), (0, 8, 0), (24, 8, 0))):
            struct.pack_into("<3I", variable, 32 + 12 * index, *channel)
        frame = bytes((10, 20, 30, 255))
        expected = base64.b64encode(gamepad.framebuffer_png(frame, 1, 1, 4)).decode() + "\n"
        def read_fb_info(_fd, request, buffer, _mutate):
            buffer[:] = fixed if request == 0x4602 else variable

        with unittest.mock.patch("builtins.open",
                                 unittest.mock.mock_open(read_data=frame)), \
                unittest.mock.patch.object(gamepad.fcntl, "ioctl",
                                           side_effect=read_fb_info) as ioctl, \
                unittest.mock.patch.object(gamepad.os, "lseek", return_value=8) as seek, \
                unittest.mock.patch.object(gamepad.sys.stdout, "write") as write:
            gamepad.remote_screenshot(page)
        self.assertEqual(ioctl.call_args_list[0].args[2], fixed)
        self.assertEqual(ioctl.call_args_list[1].args[2], variable)
        expected_offset = 8 if page is None else page * 4
        seek.assert_called_once_with(unittest.mock.ANY, expected_offset, gamepad.os.SEEK_SET)
        write.assert_called_once_with(expected)

    def test_remote_screenshot_can_read_second_framebuffer_page(self):
        self.test_remote_screenshot_uses_kernel_framebuffer_layout(page=1)

    def test_page_is_forwarded_before_remote_screenshot_action(self):
        png = gamepad.framebuffer_png(bytes([0, 0, 255, 255]), 1, 1, 4)
        completed = subprocess.CompletedProcess(
            [], 0, stdout=base64.b64encode(png), stderr=b""
        )
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "screen.png"
            with unittest.mock.patch.object(gamepad.subprocess, "run",
                                            return_value=completed) as run:
                gamepad.Device("ssh").screenshot(output, page=1)
        self.assertEqual(run.call_args.args[0][-4:],
                         ["--remote", "--page", "1", "screenshot"])
        arguments = gamepad.parse_args(["--page", "1", "screenshot", "out.png"])
        self.assertEqual(arguments.page, 1)


if __name__ == "__main__":
    unittest.main()
