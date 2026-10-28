import importlib.util
import io
import pathlib
import tempfile
import unittest.mock
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "import_vless_subscription", ROOT / "tools" / "import_vless_subscription.py"
)
subscription = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(subscription)


class VlessSubscriptionTests(unittest.TestCase):
    def test_parses_tls_vless_uri(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=tls&type=tcp&sni=front.example"
            "&fp=chrome&alpn=h2%2Chttp%2F1.1#node"
        )
        node = subscription.parse_vless(uri, 1)
        self.assertEqual(node["type"], "vless")
        self.assertEqual(node["server"], "example.test")
        self.assertEqual(node["tls"]["server_name"], "front.example")
        self.assertEqual(node["tls"]["alpn"], ["h2", "http/1.1"])

    def test_parses_reality_vless_uri(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=reality&type=tcp&sni=front.example"
            "&fp=firefox&pbk=public-key&sid=1234&flow=xtls-rprx-vision#node"
        )
        node = subscription.parse_vless(uri, 2)
        self.assertEqual(node["tls"]["reality"]["public_key"], "public-key")
        self.assertEqual(node["tls"]["reality"]["short_id"], "1234")
        self.assertEqual(node["flow"], "xtls-rprx-vision")

    def test_builds_auto_select_and_skips_unsupported_entries(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=tls&type=tcp"
        )
        config, supported, skipped, servers = subscription.build_config(
            [uri, "ss://unsupported"]
        )
        self.assertEqual((supported, skipped), (1, 1))
        self.assertEqual(config["inbounds"][0]["listen"], "127.0.0.1")
        self.assertEqual(config["inbounds"][0]["listen_port"], 10808)
        self.assertEqual(config["outbounds"][1]["type"], "urltest")
        self.assertEqual(config["outbounds"][1]["url"], "https://core.telegram.org/")
        self.assertEqual(config["route"]["final"], "proxy")
        self.assertEqual(servers[0]["_name"], "example.test")

    def test_uses_selected_server_in_route(self):
        first = (
            "vless://11111111-1111-4111-8111-111111111111@first.example:443"
            "?encryption=none&security=tls&type=tcp#First"
        )
        second = (
            "vless://11111111-1111-4111-8111-111111111111@second.example:443"
            "?encryption=none&security=tls&type=tcp#Second"
        )
        nodes = [
            subscription.parse_vless(first, 1),
            subscription.parse_vless(second, 2),
        ]
        config, supported, skipped, _ = subscription.build_config(
            [first, second], nodes[1]["_id"]
        )
        self.assertEqual((supported, skipped), (2, 0))
        self.assertEqual(config["route"]["final"], nodes[1]["tag"])

    def test_builds_system_tun_config_for_muvpn(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=tls&type=tcp"
        )
        config, supported, skipped, _ = subscription.build_config(
            [uri], tun_mode=True
        )
        self.assertEqual((supported, skipped), (1, 0))
        self.assertEqual(config["inbounds"][0]["type"], "tun")
        self.assertTrue(config["inbounds"][0]["auto_route"])
        self.assertTrue(config["inbounds"][0]["strict_route"])
        self.assertEqual(config["route"]["rules"][0]["action"], "hijack-dns")
        self.assertEqual(config["dns"]["servers"][0]["type"], "local")
        self.assertEqual(config["dns"]["servers"][1]["type"], "https")
        self.assertEqual(config["dns"]["servers"][1]["server"], "1.1.1.1")
        self.assertEqual(config["dns"]["servers"][1]["detour"], "proxy")
        self.assertEqual(config["route"]["default_domain_resolver"], "bootstrap")
        self.assertEqual(config["outbounds"][0]["domain_resolver"], "bootstrap")

    def test_sorts_servers_by_tcp_ping_with_unreachable_last(self):
        nodes = [
            {"_id": "a" * 16, "_name": "slow", "server": "slow", "server_port": 443,
             "tag": "node-slow"},
            {"_id": "b" * 16, "_name": "fast", "server": "fast", "server_port": 443,
             "tag": "node-fast"},
            {"_id": "c" * 16, "_name": "offline", "server": "offline", "server_port": 443,
             "tag": "node-offline"},
        ]
        latencies = {"slow": 80, "fast": 12, "offline": None}
        with unittest.mock.patch.object(
            subscription, "probe_server", side_effect=lambda node: latencies[node["_name"]]
        ):
            servers = subscription.sort_servers_by_ping(nodes)
        self.assertEqual([server["name"] for server in servers],
                         ["fast", "slow", "offline"])
        self.assertEqual([server["ping"] for server in servers], [12, 80, None])

    def test_writes_private_server_list(self):
        server = {
            "id": "a" * 16,
            "name": "Node One",
            "ping": 24,
            "host": "node.example.test",
            "port": 443,
        }
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "servers.tsv"
            original_path = subscription.SERVER_LIST
            subscription.SERVER_LIST = str(path)
            try:
                subscription.write_server_list([server])
                self.assertEqual(path.read_text(encoding="utf-8"),
                                 "{}\t24\tNode One\tnode.example.test\t443\n"
                                 .format("a" * 16))
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            finally:
                subscription.SERVER_LIST = original_path

    def test_vless_name_and_id_are_stable(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=tls&type=tcp#%F0%9F%87%B3%F0%9F%87%B1%20Node"
        )
        first = subscription.parse_vless(uri, 1)
        second = subscription.parse_vless(uri, 9)
        self.assertEqual(first["_id"], second["_id"])
        self.assertEqual(first["_name"], "🇳🇱 Node")

    def test_rejects_non_tcp_transport(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=tls&type=ws"
        )
        with self.assertRaises(ValueError):
            subscription.parse_vless(uri, 1)

    def test_rejects_reality_without_public_key(self):
        uri = (
            "vless://11111111-1111-4111-8111-111111111111@example.test:443"
            "?encryption=none&security=reality&type=tcp"
        )
        with self.assertRaises(ValueError):
            subscription.parse_vless(uri, 1)

    def test_subscription_config_requires_https(self):
        with tempfile.TemporaryDirectory() as directory:
            config_path = pathlib.Path(directory) / "subscription.conf"
            config_path.write_text("url=http://example.test/sub\n", encoding="utf-8")
            original_path = subscription.SUBSCRIPTION_FILE
            subscription.SUBSCRIPTION_FILE = str(config_path)
            try:
                with self.assertRaises(ValueError):
                    subscription.read_subscription_url()
            finally:
                subscription.SUBSCRIPTION_FILE = original_path

    def test_retries_temporary_subscription_transport_error(self):
        class Response:
            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def read(self, _limit):
                return b"vless://example"

        class Opener:
            def __init__(self):
                self.calls = 0

            def open(self, _request, timeout):
                self.calls += 1
                self_timeout = timeout
                if self_timeout != 20:
                    raise AssertionError("unexpected subscription timeout")
                if self.calls == 1:
                    raise subscription.urllib.error.URLError(
                        OSError("temporary DNS failure")
                    )
                return Response()

        opener = Opener()
        with unittest.mock.patch.object(
            subscription.urllib.request, "build_opener", return_value=opener
        ), unittest.mock.patch.object(subscription.time, "sleep") as sleep:
            self.assertEqual(
                subscription.read_subscription("https://example.test/sub"),
                ["vless://example"],
            )
        self.assertEqual(opener.calls, 2)
        sleep.assert_called_once_with(1)

    def test_does_not_retry_subscription_http_errors(self):
        opener = unittest.mock.Mock()
        opener.open.side_effect = subscription.urllib.error.HTTPError(
            "https://example.test/sub", 403, "Forbidden", {}, None
        )
        with unittest.mock.patch.object(
            subscription.urllib.request, "build_opener", return_value=opener
        ):
            with self.assertRaises(subscription.urllib.error.HTTPError):
                subscription.read_subscription("https://example.test/sub")
        opener.open.assert_called_once()

    def test_rejects_subscription_redirect_to_http(self):
        handler = subscription.HTTPSOnlyRedirectHandler()
        request = subscription.urllib.request.Request("https://example.test/sub")
        with self.assertRaises(subscription.urllib.error.URLError):
            handler.redirect_request(
                request, io.BytesIO(), 302, "Found", {}, "http://example.test/sub"
            )


if __name__ == "__main__":
    unittest.main()
