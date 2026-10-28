# ADR 0007: Fail-closed proxy startup and sing-box integrity

- **Status:** Accepted
- **Date:** 2026-10-28

## Context

A launcher workaround silently started Telegram without a proxy when sing-box
was missing, rejected its configuration, or failed to open the local SOCKS5
port. This contradicted ADR 0003 and could disclose the user's real network
path. It also made a broken engine look like a Telegram connectivity problem.

The installed sing-box on the device exited with signal 11 even for
`sing-box version`. ELF inspection showed loadable and dynamic data extending
well beyond the 43,607,040-byte file: it was truncated, not merely rejecting a
private VLESS profile. Separately, replacing the launcher shell with `exec`
discarded its cleanup trap and could leave a proxy running after the UI exited.

## Decision

Preserve ADR 0003's fail-closed contract in `tools/run-client.sh`:

- A manual profile or an HTTPS subscription requests a proxy. A missing engine
  or importer, rejected profile, failed required refresh, or unavailable local
  SOCKS endpoint aborts startup with a diagnostic instead of starting TDLib.
- A valid cached subscription may start immediately while its refresh runs in
  the background. Direct mode is permitted only when no proxy is configured.
- Run the client as a child process, wait for it, preserve its exit status, and
  stop sing-box on every launcher exit. Trap HUP, INT, and TERM explicitly.
- Keep the existing private configuration location by default, but permit
  `TGMUOS_CONFIG_DIR` for isolated tests. `TGMUOS_PROXY_PORT` permits an
  isolated local test endpoint and defaults to 10808; generated configuration
  and readiness checks must use the same port.

Rebuild the engine from the official sing-box `v1.14.4` source, commit
`c005f35300ce76427dc285481ec5695f5b179cd2`, for `linux/arm64` with
`CGO_ENABLED=0`, `GOARM64=v8.0`, the release build tags, and Go 1.26.5. Retain
the GPL-3.0 license and the corresponding-source offer required for a
redistributed executable. Test the rebuilt binary before replacing the broken
one, and retain a timestamped backup during installation.

Never include subscription URLs, UUIDs, private profiles, or credentials in
build output, tests, commits, release notes, or this ADR.

## Consequences

- A proxy outage produces a visible startup failure rather than a silent direct
  Telegram connection.
- Tests cannot be made successful by an unrelated service already listening on
  the host's standard proxy port.
- Closing TGMUOS also closes its per-client proxy engine; muVPN has a separate
  lifecycle and is unaffected.
- The repaired engine still needs real profile and Telegram checks. Restoring
  the executable alone does not prove that a remote VLESS node is reachable.

## Resolution (same day, 23:3x)

The device-side sing-box binary never bound its SOCKS5 port — rebuilt from
source with `with_utls`, it still produced only `WARN missing route.default_domain_resolver`
and never opened 10808. Rather than continue fighting the aarch64 build, the
device now points at a proxy that already works: the host (`192.168.0.178`)
runs Happ, which starts its own `sing-box` listening on `10808` (SOCKS5). The
device reaches it (`curl -x socks5://192.168.0.178:10808 https://telegram.org`
returns 200) and direct egress to Telegram is blocked by the provider, so the
host proxy is the only viable path today.

`/root/.config/tgmuos/proxy.conf` now reads `enabled=1, server=192.168.0.178,
port=10808`. With it, `tgmuos` loads TDLib, passes `setTdlibParameters`, and
reaches `authorizationStateWaitPhoneNumber`. The fail-closed contract in
`tools/run-client.sh` is unchanged — it still aborts when the configured proxy
endpoint is unavailable; it just no longer depends on the device-local engine.

## Validation

- `python3 -m unittest discover -s tests` covers direct opt-in, all fail-closed
  startup stages, valid cache behavior, child proxy cleanup, and private test
  configuration.
- Before installation: verify source revision, AArch64 ELF format, and
  `sing-box version` on the target.
- During deployment: validate the existing profile without printing its
  contents, start through the launcher, and confirm the SOCKS endpoint.
- After connection: inspect `pingProxy` and Telegram authorization state without
  logging proxy credentials.

## Related

- ADR 0003: VLESS subscription via local sing-box SOCKS5 for TDLib
- ADR 0004: muVPN as a separate application with an independent proxy lifecycle
