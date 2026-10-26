#!/bin/bash
# HELP: tgmuos - Telegram client for RG35XX SP (muOS)
# Direct launch script (fallback without PortMaster)

. /opt/muos/script/var/func.sh

echo app >/tmp/act_go

# Get screen dimensions from muOS device config
export SCREEN_WIDTH=$(GET_VAR device mux/width)
export SCREEN_HEIGHT=$(GET_VAR device mux/height)
export SCREEN_RESOLUTION="${SCREEN_WIDTH}x${SCREEN_HEIGHT}"

# Setup muOS input/controller environment.
SETUP_SDL_ENVIRONMENT

# Application paths
BASE_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
cd "$BASE_DIR" || exit
export LD_LIBRARY_PATH="$BASE_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p /root/.config/tgmuos
chmod 700 /root/.config/tgmuos
if [ -r /root/.config/tgmuos/telegram.conf ]; then
    export TGMUOS_CONFIG=/root/.config/tgmuos/telegram.conf
fi
export TGMUOS_PROXY_CONFIG=/root/.config/tgmuos/proxy.conf

# Set foreground process
SET_VAR "system" "foreground_process" "tgmuos"

# Keep diagnostics beside the installed application.
exec > "$BASE_DIR/log.txt" 2>&1

# Run the application
sh "$BASE_DIR/tools/run-client.sh" "$BASE_DIR"
