# ADR 0001: TDLib JSON as the Telegram runtime

- **Status:** Accepted
- **Date:** 2026-10-26

## Context

TGMUOS is a controller-driven Telegram client for muOS on AArch64. It needs a
supported client API for account authorization, chat updates, message history,
and sending messages. Telegram's Bot API is not suitable for signing in as a
user. The app already uses TDLib's JSON C interface and loads the implementation
at runtime.

The target image does not provide TDLib. The library must be shipped with the
app and match the device architecture and runtime ABI. API credentials and the
user's encrypted TDLib database are local device data, not build artifacts.

## Decision

Use the official TDLib JSON interface (`td_json_client`) as the Telegram
backend. Build the first muOS candidate from the official
[TDLib v1.8.0 source archive](https://github.com/tdlib/td/archive/refs/tags/v1.8.0.tar.gz)
for AArch64 Linux. Package the resulting `libtdjson.so` in the application's
`lib/` directory; do not link the app to a host or x86_64 TDLib build.

Build TDLib with the target AArch64 C++ toolchain and compatible OpenSSL and
zlib dependencies. Prefer static linkage of those dependencies into
`libtdjson.so` where the license and target toolchain permit it, minimizing
additional runtime libraries required on muOS.

Keep per-installation configuration and account data outside the binary:

- `telegram.conf` contains `api_id` and `api_hash`, is excluded from version
  control, and must be stored with restrictive local file permissions. On muOS,
  the launcher prefers `/root/.config/tgmuos/telegram.conf` because the app
  partition is exFAT and cannot enforce Unix file permissions.
- TDLib's encrypted database and its generated key remain under `data/`; users
  must back them up together and only while the app is stopped.
- Never place API credentials, login codes, or account passwords in source,
  package metadata, logs, or this ADR.

## Consequences

- Users can authorize their own Telegram account and use the client API,
  including phone/code/password and QR authorization.
- The application can continue to start without TDLib installed and report
  setup status instead of fabricating chats or successful sends.
- Releases must provide an AArch64 library and verify its shared-library
  dependencies against the supported muOS runtime.
- TDLib v1.8.0 is the initial build target, not a claim that this version has
  been built, tested on-device, or is the preferred version for future releases.
  Revisit the version before release if the Telegram API, security maintenance,
  or muOS ABI requires it.

## Validation required before release

1. Confirm `libtdjson.so` is ELF64 for AArch64 and inspect all `DT_NEEDED`
   entries.
2. Confirm the library loads through the app's runtime loader on the target
   muOS device.
3. Exercise authorization, QR login, chat loading, message history, and sending
   with a test account; confirm credentials and login codes are not logged.
4. Record the source tag and build toolchain used for the release artifact.
