#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST=${1:?usage: package.sh APP_DESTINATION}
MUVPN_BINARY=${MUVPN_BINARY:-$ROOT/build-muos/muvpn}

mkdir -p "$DEST/tools" "$DEST/libexec" "$DEST/res/fonts" \
    "$DEST/res/glyph/muxapp" "$DEST/docs/licenses"
cp "$MUVPN_BINARY" "$DEST/muvpn"
cp "$APP_DIR/libexec/sing-box" "$DEST/libexec/sing-box"
cp "$ROOT/tools/import_vless_subscription.py" "$DEST/tools/import_vless_subscription.py"
cp "$APP_DIR/tools/muvpn.py" "$DEST/tools/muvpn.py"
cp "$APP_DIR/mux_launch.sh" "$DEST/mux_launch.sh"
cp "$APP_DIR/muVPN.sh" "$DEST/muVPN.sh"
cp "$APP_DIR/muvpn.gptk" "$DEST/muvpn.gptk"
cp "$ROOT/res/fonts/AdwaitaSans-Regular.ttf" "$DEST/res/fonts/AdwaitaSans-Regular.ttf"
cp "$APP_DIR/res/glyph/muxapp/muvpn.png" "$DEST/res/glyph/muxapp/muvpn.png"
cp "$ROOT/docs/licenses/sing-box-v1.14.4.LICENSE" "$DEST/docs/licenses/"
chmod 755 "$DEST/mux_launch.sh" "$DEST/muVPN.sh"
chmod 755 "$DEST/tools/muvpn.py" "$DEST/tools/import_vless_subscription.py"
chmod 755 "$DEST/muvpn" "$DEST/libexec/sing-box"
