import os
import pathlib
import socket
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools" / "run-client.sh"


class RunClientTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory(prefix="tgmuos-launcher-")
        self.root = pathlib.Path(self.temporary_directory.name)
        self.app = self.root / "app"
        self.config = self.root / "config"
        (self.app / "libexec").mkdir(parents=True)
        (self.app / "tools").mkdir()
        self.output = self.root / "launched"
        with socket.socket() as available_port:
            available_port.bind(("127.0.0.1", 0))
            self.proxy_port = available_port.getsockname()[1]
        (self.app / "tgmuos").write_text(
            '#!/bin/sh\nprintf "%s\\n" "$TGMUOS_PROXY_CONFIG" >"$TGMUOS_TEST_OUTPUT"\n'
        )
        (self.app / "tgmuos").chmod(0o755)
        self.engine = self.app / "libexec" / "sing-box"
        self.engine.write_text(
            "#!/bin/sh\n"
            "case \"$1\" in\n"
            "  check) exit \"${TGMUOS_TEST_CHECK_STATUS:-0}\" ;;\n"
            "  run)\n"
            "    if [ \"$TGMUOS_TEST_ENGINE_MODE\" = crash ]; then exit 1; fi\n"
            "    exec python3 -c 'import os, socket\n"
            "server = socket.socket()\n"
            "server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)\n"
            "server.bind((\"127.0.0.1\", int(os.environ[\"TGMUOS_PROXY_PORT\"])))\n"
            "server.listen()\n"
            "while True:\n"
            "    connection, _ = server.accept()\n"
            "    connection.close()' ;;\n"
            "  *) exit 2 ;;\n"
            "esac\n"
        )
        self.engine.chmod(0o755)
        (self.app / "tools" / "import_vless_subscription.py").write_text(
            "import os, pathlib, sys\n"
            "if os.environ.get('TGMUOS_TEST_IMPORT_FAIL'):\n"
            "    sys.exit(1)\n"
            "pathlib.Path(os.environ['TGMUOS_GENERATED_PROFILE']).write_text('{}')\n"
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def launch(self, check_status=0, engine_mode="listen", import_fail=False):
        environment = os.environ.copy()
        environment.update({
            "TGMUOS_CONFIG_DIR": str(self.config),
            "TGMUOS_TEST_OUTPUT": str(self.output),
            "TGMUOS_TEST_CHECK_STATUS": str(check_status),
            "TGMUOS_TEST_ENGINE_MODE": engine_mode,
            "TGMUOS_GENERATED_PROFILE": str(self.config / "sing-box.json"),
            "TGMUOS_PROXY_WAIT_ATTEMPTS": "2",
            "TGMUOS_PROXY_PORT": str(self.proxy_port),
        })
        if import_fail:
            environment["TGMUOS_TEST_IMPORT_FAIL"] = "1"
        return subprocess.run(
            ["sh", str(LAUNCHER), str(self.app)],
            env=environment, capture_output=True, text=True, timeout=10, check=False,
        )

    def assert_proxy_port_released(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", self.proxy_port))

    def request_subscription(self):
        self.config.mkdir(exist_ok=True)
        (self.config / "subscription.conf").write_text("url=https://example.invalid/subscription\n")

    def test_direct_connection_requires_no_proxy_configuration(self):
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_text().strip(), str(self.config / "proxy.conf"))

    def test_missing_engine_blocks_requested_proxy(self):
        self.request_subscription()
        self.engine.unlink()
        result = self.launch()
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertIn("proxy is required", result.stderr)

    def test_invalid_manual_profile_does_not_fall_back_to_subscription(self):
        self.request_subscription()
        (self.config / "manual-sing-box.json").write_text("{}")
        (self.config / "sing-box.json").write_text("{}")
        result = self.launch(check_status=1)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertIn("manual proxy profile", result.stderr)

    def test_failed_import_without_valid_cache_blocks_client(self):
        self.request_subscription()
        result = self.launch(import_fail=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())

    def test_engine_crash_blocks_client_instead_of_using_direct_connection(self):
        self.config.mkdir()
        (self.config / "manual-sing-box.json").write_text("{}")
        result = self.launch(engine_mode="crash")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertIn("did not start its local proxy", result.stderr)

    def test_valid_manual_profile_starts_client_with_local_proxy(self):
        self.config.mkdir()
        (self.config / "manual-sing-box.json").write_text("{}")
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.output.read_text().strip(), str(self.config / "subscription-proxy.conf")
        )
        self.assertIn(f"port={self.proxy_port}",
                      (self.config / "subscription-proxy.conf").read_text())
        self.assertEqual((self.config.stat().st_mode & 0o777), 0o700)
        self.assert_proxy_port_released()

    def test_valid_cached_subscription_allows_background_refresh_failure(self):
        self.request_subscription()
        (self.config / "sing-box.json").write_text("{}")
        result = self.launch(import_fail=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self.output.read_text().strip(), str(self.config / "subscription-proxy.conf")
        )
        self.assert_proxy_port_released()

    def test_imported_profile_that_fails_engine_check_blocks_client(self):
        self.request_subscription()
        result = self.launch(check_status=1)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertIn("rejected the proxy profile", result.stderr)


if __name__ == "__main__":
    unittest.main()
