import importlib.util
import os
import pathlib
import tempfile
import unittest


SERVICE_PATH = pathlib.Path(__file__).resolve().parents[1] / "tools" / "muvpn.py"
SPEC = importlib.util.spec_from_file_location("muvpn_service", SERVICE_PATH)
service = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(service)


class MuvpnServiceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self.temporary.name)
        service.CONFIG_DIR = self.directory / "muvpn"
        service.SUBSCRIPTION_CONFIG = self.directory / "subscription.conf"
        service.SERVER_LIST = service.CONFIG_DIR / "servers.tsv"
        service.SERVER_LIST.parent.mkdir(mode=0o700)
        self.server_id = "abcdef0123456789"
        service.SERVER_LIST.write_text(
            f"{self.server_id}\t42\tNode One\n",
            encoding="utf-8",
        )
        service.SUBSCRIPTION_CONFIG.write_text(
            "enabled=1\nurl=https://subscription.example/secret\nselected=oldid\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def test_select_saves_valid_server_and_keeps_url_private(self):
        service.select(self.server_id)
        contents = service.SUBSCRIPTION_CONFIG.read_text(encoding="utf-8")
        self.assertIn("url=https://subscription.example/secret\n", contents)
        self.assertIn(f"selected={self.server_id}\n", contents)
        self.assertEqual(
            service.SUBSCRIPTION_CONFIG.stat().st_mode & 0o777,
            0o600,
        )

    def test_automatic_selection_removes_saved_node(self):
        service.select("auto")
        contents = service.SUBSCRIPTION_CONFIG.read_text(encoding="utf-8")
        self.assertNotIn("selected=", contents)
        self.assertIn("url=https://subscription.example/secret\n", contents)

    def test_select_rejects_nodes_missing_from_subscription(self):
        with self.assertRaises(ValueError):
            service.select("0123456789abcdef")

    def test_private_environment_uses_separate_output_directory(self):
        environment = service.private_environment()
        self.assertEqual(environment["TGMUOS_CONFIG_DIR"], str(service.CONFIG_DIR))
        self.assertEqual(
            environment["TGMUOS_SUBSCRIPTION_CONFIG"],
            str(service.SUBSCRIPTION_CONFIG),
        )
        self.assertEqual(environment["TGMUOS_SINGBOX_MODE"], "tun")

    def test_config_directory_is_private(self):
        service.ensure_config_dir()
        self.assertEqual(service.CONFIG_DIR.stat().st_mode & 0o777, 0o700)


if __name__ == "__main__":
    unittest.main()
