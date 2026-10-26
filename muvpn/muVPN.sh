#!/bin/bash
# HELP: muVPN - system-wide VLESS VPN for muOS

BASE_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
exec "$BASE_DIR/mux_launch.sh" "$BASE_DIR"
