#!/bin/bash
# HELP: muVPN - system-wide VLESS VPN for muOS
# ICON: muvpn
# GRID: muVPN

. /opt/muos/script/var/func.sh

echo app >/tmp/act_go

export SCREEN_WIDTH=$(GET_VAR device mux/width)
export SCREEN_HEIGHT=$(GET_VAR device mux/height)
export SCREEN_RESOLUTION="${SCREEN_WIDTH}x${SCREEN_HEIGHT}"
SETUP_SDL_ENVIRONMENT

BASE_DIR="$1"
cd "$BASE_DIR" || exit 1
export LD_LIBRARY_PATH="$BASE_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MUVPN_CONFIG_DIR=/root/.config/muvpn
export TGMUOS_SUBSCRIPTION_CONFIG=/root/.config/tgmuos/subscription.conf
export TGMUOS_SERVERS_FILE=/root/.config/muvpn/servers.tsv
SET_VAR "system" "foreground_process" "muvpn"

exec > "$BASE_DIR/log.txt" 2>&1
exec "$BASE_DIR/muvpn"
