# ADR 0004: muVPN as a separate muOS application with TUN routing

- **Status:** Proposed
- **Date:** 2026-10-26

## Context

TGMUOS (ADR 0003) routes only its own Telegram traffic through a local
sing-box SOCKS5 proxy. The user also wants system-wide VPN coverage for the
device — all applications, not just TGMUOS — using the same private VLESS
subscription.

Extending TGMUOS itself was rejected: a per-app Telegram client should not
own system routing, and muOS applications are easier to manage as separate
installable units with their own launcher entries, glyphs, and lifecycle.

The scaffold lives in `muvpn/`: a controller-driven UI (`muvpn/src/main.c`),
a service manager (`muvpn/tools/muvpn.py`), packaging
(`muvpn/package.sh`), tests (`muvpn/tests/test_service.py`), and muOS
launcher resources. It is not yet packaged or installed on the device.

## Decision

Build muVPN as a standalone muOS application in `muvpn/`, sharing the
subscription importer with TGMUOS but running sing-box in TUN mode:

- **Shared subscription, separate state.** muVPN reads the subscription from
  the TGMUOS location (`/root/.config/tgmuos/subscription.conf`) but writes
  its own generated profile, node list, logs, and service files under
  `/root/.config/muvpn/` with mode 700/600. The importer is reused via
  `TGMUOS_CONFIG_DIR` / `TGMUOS_SINGBOX_MODE=tun` environment overrides.
- **TUN mode.** sing-box creates a TUN interface and adds device routes, so
  all device traffic — not only Telegram — traverses the selected VLESS
  node. DNS uses the current sing-box DNS-server format routed through the
  proxy (an earlier legacy DNS format failed `sing-box check`; see
  Validation).
- **Service lifecycle.** `muvpn.py` manages sing-box as a persistent service
  with a PID file and lock file. The VPN service keeps running after the
  muVPN UI exits; it is stopped from the muVPN interface or by rebooting.
- **Same rendering stack.** The UI reuses the TGMUOS architecture: direct
  `/dev/fb0` output, SDL2 for input, SDL_ttf for text (see ADR 0002), and a
  settings screen with language and animation preferences stored privately.
- **Packaging.** `muvpn/package.sh` assembles the muOS application directory
  (binary, launcher scripts, tools, font, glyph, sing-box license). The
  AArch64 sing-box binary must be placed at `muvpn/libexec/sing-box` before
  packaging; subscription URLs, UUIDs, and node configuration are never part
  of the package.

## Consequences

- One subscription serves two apps with independent state and lifecycles.
- TUN mode changes system routes while connected and affects all device
  networking; a crashed or forgotten service can leave the device routed
  through the tunnel.
- sing-box GPL-3.0 obligations apply to both apps' redistribution.
- The scaffold duplicates some TGMUOS concerns (renderer, packaging, service
  management) to keep the apps independent; shared code stays limited to the
  importer and font resources.

## Validation required before release

1. `sing-box check` accepts the generated TUN profile on the device (the
   legacy DNS format previously failed; the current DNS-server format is
   implemented but not yet revalidated on-device).
2. Starting the service creates the TUN interface, routes device traffic,
   and restores routes on stop.
3. The service survives UI exit, is stoppable from the UI, and cleans up on
   device reboot without manual steps.
4. `muvpn/package.sh` output installs as a muOS application under
   `/mnt/mmc/MUOS/application/muVPN` and launches from the muOS menu.
5. Tests pass: `muvpn/tests/test_service.py` and the shared importer suite.

## Related

- ADR 0001: TDLib JSON as the Telegram runtime
- ADR 0002: SDL2 for input, direct framebuffer for video
- ADR 0003: VLESS subscription via local sing-box SOCKS5 for TDLib
