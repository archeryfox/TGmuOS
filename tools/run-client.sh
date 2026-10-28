#!/bin/sh

umask 077

BASE_DIR=$1
shift
CONFIG_DIR=${TGMUOS_CONFIG_DIR:-/root/.config/tgmuos}
PROXY_PORT=${TGMUOS_PROXY_PORT:-10808}
SUBSCRIPTION_CONFIG="$CONFIG_DIR/subscription.conf"
SUBSCRIPTION_SOURCES="$CONFIG_DIR/subscription-sources.txt"
SINGBOX="$BASE_DIR/libexec/sing-box"
IMPORTER="$BASE_DIR/tools/import_vless_subscription.py"
MANUAL_PROFILE="$CONFIG_DIR/manual-sing-box.json"
PROFILE_CONFIG="$CONFIG_DIR/sing-box.json"
USE_MANUAL_PROFILE=0

mkdir -p "$CONFIG_DIR" || exit 1
chmod 700 "$CONFIG_DIR" || exit 1
export TGMUOS_SUBSCRIPTION_CONFIG="$SUBSCRIPTION_CONFIG"
export TGMUOS_SUBSCRIPTION_SOURCES="$SUBSCRIPTION_SOURCES"
export TGMUOS_MANUAL_PROFILE="$MANUAL_PROFILE"
export TGMUOS_SERVERS_FILE="$CONFIG_DIR/servers.tsv"

PROXY_WANTED=0
if [ -s "$MANUAL_PROFILE" ] ||
   grep -q '^url=https://' "$SUBSCRIPTION_CONFIG" 2>/dev/null; then
    PROXY_WANTED=1
fi

if [ "$PROXY_WANTED" -eq 1 ]; then
    if [ ! -x "$SINGBOX" ]; then
        echo "sing-box not found; proxy is required" >&2
        exit 1
    fi

    # A configured manual profile takes priority and must itself be usable.
    if [ -s "$MANUAL_PROFILE" ]; then
        if ! "$SINGBOX" check -c "$MANUAL_PROFILE" >/dev/null 2>&1; then
            echo "sing-box rejected the manual proxy profile; proxy is required" >&2
            exit 1
        fi
        PROFILE_CONFIG="$MANUAL_PROFILE"
        USE_MANUAL_PROFILE=1
    else
        if [ ! -r "$IMPORTER" ]; then
            echo "VLESS subscription importer missing; proxy is required" >&2
            exit 1
        fi
        if [ ! -s "$PROFILE_CONFIG" ] ||
           ! "$SINGBOX" check -c "$PROFILE_CONFIG" >/dev/null 2>&1; then
            if ! python3 "$IMPORTER"; then
                echo "VLESS subscription update failed; proxy is required" >&2
                exit 1
            fi
        else
            (
                if ! python3 "$IMPORTER" >>"$CONFIG_DIR/import.log" 2>&1; then
                    printf '%s\n' "Subscription refresh failed; keeping validated cached profile" \
                        >>"$CONFIG_DIR/import.log"
                fi
            ) &
        fi
    fi

    if [ ! -s "$PROFILE_CONFIG" ] ||
       ! "$SINGBOX" check -c "$PROFILE_CONFIG" >/dev/null 2>&1; then
        echo "sing-box rejected the proxy profile; proxy is required" >&2
        exit 1
    fi

    "$SINGBOX" run -c "$PROFILE_CONFIG" \
        >"$CONFIG_DIR/sing-box.log" 2>&1 &
    SINGBOX_PID=$!
    chmod 600 "$CONFIG_DIR/sing-box.log" 2>/dev/null || true
    stop_singbox() {
        kill "$SINGBOX_PID" 2>/dev/null
        wait "$SINGBOX_PID" 2>/dev/null
    }
    trap stop_singbox EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM

    if ! python3 -c '
import os, socket, sys, time
port = int(os.environ.get("TGMUOS_PROXY_PORT", "10808"))
if not 1 <= port <= 65535:
    sys.exit(1)
attempts = int(os.environ.get("TGMUOS_PROXY_WAIT_ATTEMPTS", "50"))
for _ in range(max(1, attempts)):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
'; then
        echo "sing-box did not start its local proxy; proxy is required" >&2
        exit 1
    fi

    SUBSCRIPTION_ENABLED=0
    if [ "$USE_MANUAL_PROFILE" -eq 1 ] ||
       grep -q '^enabled=1$' "$SUBSCRIPTION_CONFIG" 2>/dev/null; then
        SUBSCRIPTION_ENABLED=1
    fi
    printf '%s\n' "enabled=$SUBSCRIPTION_ENABLED" 'server=127.0.0.1' "port=$PROXY_PORT" \
        'username=' 'password=' >"$CONFIG_DIR/subscription-proxy.conf"
    chmod 600 "$CONFIG_DIR/subscription-proxy.conf"
    export TGMUOS_PROXY_CONFIG="$CONFIG_DIR/subscription-proxy.conf"
    "$BASE_DIR/tgmuos" "$@" &
    CLIENT_PID=$!
    wait "$CLIENT_PID"
    CLIENT_STATUS=$?
    exit "$CLIENT_STATUS"
fi

# A direct connection is allowed only when no proxy was configured.
export TGMUOS_PROXY_CONFIG="$CONFIG_DIR/proxy.conf"
exec "$BASE_DIR/tgmuos" "$@"