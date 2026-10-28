# TGMUOS

Controller-driven Telegram client for muOS, designed around a 640x480 screen.
The display is drawn directly to the Linux framebuffer; it does not depend on an
SDL video backend being available in the muOS system libraries.

The application uses TDLib's C/JSON interface for Telegram authorization,
chat lists, text-message history, and sending text messages. It is a small,
controller-driven client; media, calls, secret chats, and several advanced
Telegram features are not implemented.

The TDLib runtime choice and muOS packaging requirements are recorded in
[ADR 0001](docs/adr/0001-tdlib-json-for-muos.md), the rendering architecture
in [ADR 0002](docs/adr/0002-sdl2-input-direct-framebuffer.md), the VLESS
proxy integration in
[ADR 0003](docs/adr/0003-vless-sing-box-proxy-for-tdlib.md), the muVPN
scaffold in [ADR 0004](docs/adr/0004-muvpn-tun-vpn-app.md), on-demand folders
in [ADR 0005](docs/adr/0005-on-demand-chat-folders.md), remote debugging in
[ADR 0006](docs/adr/0006-uinput-remote-debugging.md), and proxy startup in
[ADR 0007](docs/adr/0007-proxy-startup-and-engine-integrity.md).

## Build on a host

Install SDL2 and SDL2_ttf development packages, then run:

```sh
cmake -S . -B build
cmake --build build
./build/tgmuos --smoke-test
```

On muOS, run without test flags; the app opens `/dev/fb0` directly.

## Cross-compile for muOS

Use an AArch64 toolchain and SDL2/SDL2_ttf development headers. Link against
muOS-compatible SDL2 and SDL2_ttf shared libraries. Point pkg-config at those
target `.pc` files, then configure CMake with the cross compiler:

```sh
export PKG_CONFIG_LIBDIR=/path/to/aarch64-sdl/lib/pkgconfig
cmake -S . -B build-muos \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc
cmake --build build-muos
```

## Configure Telegram

Register your own `api_id` and `api_hash` at
[my.telegram.org/apps](https://my.telegram.org/apps). Install a TDLib build
for AArch64 as `lib/libtdjson.so`, including its runtime dependencies. The
launcher adds this directory to the shared-library search path.

Create `telegram.conf` beside the executable, containing:

```ini
api_id=123456
api_hash=your_api_hash
```

Keep this file private (`chmod 600 telegram.conf`) and do not publish it.
On muOS, the launcher prefers `/root/.config/tgmuos/telegram.conf` when it
exists. Store credentials there rather than on the exFAT application partition,
which does not enforce Unix file permissions. `TGMUOS_CONFIG` can also point
the client to a different private config file.
TDLib's account database is stored under `data/tdlib/` and uses a generated
local key in `data/tdlib.key`; back up both together. On first launch enter
your phone number, the Telegram login code, and your two-step verification
password if enabled. The login screen also offers QR authorization; scan it
from Telegram on another device. The login code is requested through Telegram's
normal authorization flow; never share it with anyone.

On muOS, the Settings menu can enable or disable the VLESS proxy. The primary
subscription stays in `subscription.conf`; extra HTTPS subscription URLs are
kept one per line in the private `subscription-sources.txt`. The importer also
adds the private manual VLESS profile as a selectable server and keeps its DNS
and direct-domain bypass rules. URLs and imported node details stay in
`/root/.config/tgmuos/` with private permissions. The bundled sing-box engine
creates a local SOCKS5 connection for TDLib; it carries Telegram-client traffic
only, not traffic from other applications. The cached tunnel starts before
the Telegram UI; subscription refresh runs in the background when a valid
cached profile is available. If a profile is missing or invalid, startup
requires a successful refresh and never falls back to a direct connection.
The subscription toggle and **Apply connection** action update TDLib's proxy
without restarting TGMUOS. Changing the selected server saves the choice for
the next sing-box profile refresh.

Use **Choose server** in Settings to browse subscription nodes sorted by TCP
connection latency; unreachable nodes appear last. Selecting a node saves it
privately for the next sing-box profile refresh. Leave the setting on
**Automatic** to let sing-box choose a responsive node. Inside the server
list, press **X** to re-measure latency for every node (TCP connect ping,
1.5 s timeout each); the list is re-sorted by the fresh results and saved
privately. Node addresses are stored next to the list so the app can ping
without a subscription refresh.

Subscription URLs and node credentials are installed on the device, not in
the source tree. The importer accepts VLESS links using TCP with TLS or
Reality; sing-box periodically tests supported nodes against Telegram and
automatically selects a responsive node when the server list is set to
**Automatic**. sing-box v1.14.4 is built for AArch64 from the
[official source](https://github.com/SagerNet/sing-box/tree/v1.14.4); retain
its GPL-3.0 license and source availability when redistributing the binary.

## muVPN

`muvpn/` is a separate muOS application that uses the same private VLESS
subscription as TGMUOS and routes device traffic through a sing-box TUN
interface. It includes a controller-driven connection screen, automatic or
manual server selection, and DNS-over-HTTPS routed through the proxy. The VPN
service remains active when the muVPN interface exits; disconnect it in muVPN
or reboot the device. This changes system routes while connected and may affect
all device networking. The TGMUOS subscription remains in
`/root/.config/tgmuos/subscription.conf`; generated profiles, logs, and the
selected-server list are private under `/root/.config/muvpn/`.

Build the `muvpn` CMake target alongside TGMUOS. Package the executable and
muOS resources with `muvpn/package.sh`; place the AArch64 sing-box binary at
`muvpn/libexec/sing-box` before packaging. Do not include subscription URLs,
UUIDs, or node configuration in the application package.

The interface starts in Russian. Change interface language and toggle
animations in Settings; both preferences are saved in the private
`settings.conf` file. The chat and login keyboards each have a local RU/EN
layout toggle, independent of the interface language. A physical keyboard can
switch its own layout with Ctrl+Space. X hides or restores the chat keyboard.
Use D-pad up/down or page keys to scroll message history; older messages load
as you reach the beginning of the conversation.

Emoji and flags in chat and server names render through a bundled fallback
font (`res/fonts/NotoEmoji-Regular.ttf`, SIL Open Font License): glyphs
missing in the interface font are drawn from the emoji font automatically.

MTProto proxies (for example `t.me/proxy?server=…&secret=…` links) can be
listed in
`/root/.config/tgmuos/mtproto.conf`, one per line:
`name<TAB>server<TAB>port<TAB>secret` (secret in the base64url form from the
link). The app offers all of them to TDLib after initialization; the first
entry is enabled automatically only when the SOCKS subscription proxy is not
active. The file is private (600) and secrets are never logged.
**Note:** `t.me/webproxy` links are Telegram *Web* proxies (browser access
via `host/token` paths) — TDLib cannot use them; only classic MTProto
proxies work here.

Without both TDLib and the credentials file, the app displays setup status
instead of showing fake chats or pretending messages were sent. `--smoke-test`
and `--telegram-json-test` do not require TDLib or credentials.

## Install resources and run

The application searches for its bundled font in `res/fonts/` first, then
`assets/fonts/`, and finally uses the muOS system font. Keep `res/fonts` in the
installed application directory so the app works with the standard muOS
application layout. The application-menu glyph is in
`res/glyph/muxapp/telegram.png`; install it as
`glyph/muxapp/telegram.png` in the active muOS theme so the launcher's
`ICON: telegram` entry can display it. It opens `/dev/fb0` directly and uses
SDL2 only for input and SDL_ttf font rasterization. Back up or remove the TDLib
database and its key only while the application is stopped.

The launcher expects to be started from the application directory and uses
`/dev/fb0`. For an installed app, include the binary, both launch scripts,
`tools/run-client.sh`, `tools/import_vless_subscription.py`,
`libexec/sing-box`, `res/fonts/AdwaitaSans-Regular.ttf`, and `lib/libtdjson.so`.
The launcher writes diagnostics to `log.txt` beside the binary.

## Controls

| Button | Action |
| --- | --- |
| D-pad | Navigate chats and the on-screen keyboard; scroll history with keyboard hidden |
| A | Open/select |
| B | Back |
| X | Hide/show the chat keyboard |
| Y | Toggle letter case |
| `123` / `ABC` key | Switch between letters and symbols |
| START | Send a message |
| SELECT | Open settings (also available before sign-in) |
| Hotkey + A | Exit |

Connect a USB or Bluetooth keyboard to type directly into the message draft.
The on-screen keyboard hides when typing starts; Enter sends, Shift+Enter adds a
line break, and Backspace deletes a character (or returns to chats when the
draft is empty). Letter layout follows the language selected in Settings.

On the sign-in keypad, select `QR` to display the Telegram device-linking code.

## Chat folders and archive

The tab strip contains **All**, the custom folders reported by TDLib, and
**Archive** last. Press Left/Right in the chat list to change folders. The All
list contains non-archived `chatListMain` chats; custom tabs use their own
`chatListFolder` ID.

Archive is loaded only after it is selected. Before that, archived chat updates
do not trigger avatar, user, history, or media downloads. Changing folders
clears the visible list and resets message history and pagination, without
deleting previously downloaded files. See
[ADR 0005](docs/adr/0005-on-demand-chat-folders.md).

## Remote debugging

`tools/anbernic-virtual-gamepad.py` uses the device's Python 3 interpreter over
SSH (default) or adb; it installs no packages or persistent service. Set
`SSH_ASKPASS`, `SSH_ASKPASS_REQUIRE=force`, and `DISPLAY` if the SSH connection
needs an askpass helper. Do not put passwords in the command line.

```sh
python3 tools/anbernic-virtual-gamepad.py --host 192.168.0.151 press right A
python3 tools/anbernic-virtual-gamepad.py --transport adb --serial DEVICE press B
python3 tools/anbernic-virtual-gamepad.py --page 0 screenshot /tmp/tgmuos-page0.png
python3 tools/anbernic-virtual-gamepad.py --page 1 screenshot /tmp/tgmuos-page1.png
```

A button command creates a short-lived `/dev/uinput` keyboard named
`tgmuos-remote` and destroys it afterwards. Screenshots read the framebuffer's
actual pixel layout. Omit `--page` to capture its current offset; inspect both
pages if the application is running but the image still shows the muOS launcher.
adb must list an authorized device before it can be used. See
[ADR 0006](docs/adr/0006-uinput-remote-debugging.md).

Run the Python regression suite with:

```sh
python3 -m unittest discover -s tests
```

## Project layout

```text
include/   app, screen, input, and UI declarations
src/       SDL2 input, TDLib client, framebuffer renderer, and screens
res/       muOS application font resource
assets/    host-run font fallback
```
