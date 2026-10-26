# ADR 0003: VLESS subscription via local sing-box SOCKS5 for TDLib

- **Status:** Accepted
- **Date:** 2026-10-26

## Context

TGMUOS runs on muOS where Telegram connectivity may require a private VLESS
subscription. TDLib supports SOCKS5 proxies natively, but it has no concept of
VLESS, subscriptions, or node latency. The app must therefore bridge the two:
import a subscription, run a local proxy engine, and point TDLib at it.

Two startup problems shaped the design:

1. Refreshing the subscription synchronously before the UI added many seconds
   to every cold start.
2. An early integration sent TDLib requests that did not match the TDLib
   JSON schema (e.g. flat `setTdlibParameters` without the nested
   `parameters` object), which the on-device TDLib build answered with
   `400: Parameters aren't specified`, and responses were not correlated
   with their requests.

The subscription URL, node credentials, and the user's Telegram traffic are
private data; the application partition is exFAT and cannot enforce Unix file
permissions.

## Decision

Use sing-box as the proxy engine and TDLib's built-in proxy support as the
client side:

- **Import:** `tools/import_vless_subscription.py` fetches the subscription
  over HTTPS only (redirects to non-HTTPS are rejected), parses VLESS links
  with TCP+TLS or TCP+Reality transports, measures TCP connect latency per
  node, sorts nodes by latency, and writes a validated sing-box profile to
  `/root/.config/tgmuos/sing-box.json` plus a node list to `servers.tsv`.
  sing-box is built for AArch64 from the official v1.14.4 source and carries
  GPL-3.0 obligations on redistribution.
- **Engine:** the launcher (`tools/run-client.sh`) starts sing-box in SOCKS5
  mode on `127.0.0.1:10808`, waits for the port to accept connections, and
  exposes the endpoint to the app via a private `subscription-proxy.conf`.
  sing-box carries Telegram client traffic only.
- **Startup:** a validated cached profile is used immediately; subscription
  refresh runs in the background. If the cache is missing or fails
  `sing-box check`, a fresh import must succeed before the app starts, and
  there is no direct-connection fallback.
- **TDLib protocol:** `src/telegram.c` sends `setTdlibParameters` with its
  fields nested in a `parameters` object, correlates responses with request
  methods via `@extra`, reports errors per request, and treats proxy
  acknowledgements (`ok`) separately from data responses.
- **Live switching:** toggling the subscription or choosing **Apply
  connection** sends `addProxy` / `disableProxy` to the running TDLib client
  (`tg_apply_subscription()`), so the change takes effect without restarting
  TGMUOS. Changing the selected VLESS node is persisted for the next
  sing-box profile refresh; live sing-box reconfiguration on node change is
  not implemented.
- **Privacy:** all subscription data, profiles, logs, and proxy configuration
  live under `/root/.config/tgmuos/` with mode 700/600. The subscription URL
  and node details are device-local and must never appear in the source
  tree, package metadata, logs, or this ADR.

## Consequences

- Cold start is fast: the UI appears behind an already-validated cached
  tunnel instead of waiting for a network refresh.
- Telegram traffic flows through the selected VLESS node; nothing else on
  the device is routed.
- A failed or missing subscription blocks startup by design; the launcher
  refuses to fall back to a direct connection.
- TDLib schema mismatches surface as informative per-request errors instead
  of silent stalls or misleading `400`s.
- Changing the VLESS node does not reconfigure sing-box until the next
  profile refresh (restart or background update), which may surprise users.

## Validation required before release

1. `sing-box check` accepts the generated profile on the device.
2. The launcher starts sing-box and the SOCKS5 port accepts connections
   before the UI starts.
3. Startup with a valid cache does not block on network refresh; startup
   with a missing/invalid cache fails closed (no direct connection).
4. Toggling the subscription and **Apply connection** changes TDLib's active
   proxy without restarting the app, and the preference survives restart.
5. No subscription URL, UUID, or node detail appears in source, packages, or
   log output.

## Related

- ADR 0001: TDLib JSON as the Telegram runtime
- ADR 0004: muVPN as a separate system-wide VPN app
