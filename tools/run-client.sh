#!/bin/sh

umask 077

BASE_DIR=$1
shift
CONFIG_DIR=/root/.config/tgmuos
SUBSCRIPTION_CONFIG="$CONFIG_DIR/subscription.conf"
SINGBOX="$BASE_DIR/libexec/sing-box"
IMPORTER="$BASE_DIR/tools/import_vless_subscription.py"

mkdir -p "$CONFIG_DIR" || exit 1
chmod 700 "$CONFIG_DIR" || exit 1
export TGMUOS_SUBSCRIPTION_CONFIG="$SUBSCRIPTION_CONFIG"
export TGMUOS_SERVERS_FILE="$CONFIG_DIR/servers.tsv"

if grep -q '^url=https://' "$SUBSCRIPTION_CONFIG" 2>/dev/null; then
    if [ ! -x "$SINGBOX" ] || [ ! -r "$IMPORTER" ]; then
        echo "VLESS subscription is enabled, but sing-box support is missing"
        exit 1
    fi
    if [ ! -s "$CONFIG_DIR/sing-box.json" ] ||
       ! "$SINGBOX" check -c "$CONFIG_DIR/sing-box.json" >/dev/null 2>&1; then
        if ! python3 "$IMPORTER"; then
            echo "VLESS subscription update failed; refusing a direct connection"
            exit 1
        fi
        if ! "$SINGBOX" check -c "$CONFIG_DIR/sing-box.json" >/dev/null 2>&1; then
            echo "sing-box rejected the subscription configuration"
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

    "$SINGBOX" run -c "$CONFIG_DIR/sing-box.json" \
        >"$CONFIG_DIR/sing-box.log" 2>&1 &
    SINGBOX_PID=$!
    chmod 600 "$CONFIG_DIR/sing-box.log" 2>/dev/null || true
    stop_singbox() {
        kill "$SINGBOX_PID" 2>/dev/null
        wait "$SINGBOX_PID" 2>/dev/null
    }
    trap stop_singbox EXIT HUP INT TERM

    if ! python3 -c '
import socket, sys, time
for _ in range(50):
    try:
        with socket.create_connection(("127.0.0.1", 10808), timeout=0.2):
            sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
'; then
        echo "sing-box did not start its local proxy"
        exit 1
    fi

    SUBSCRIPTION_ENABLED=0
    if grep -q '^enabled=1$' "$SUBSCRIPTION_CONFIG" 2>/dev/null; then
        SUBSCRIPTION_ENABLED=1
    fi
    printf '%s\n' "enabled=$SUBSCRIPTION_ENABLED" 'server=127.0.0.1' 'port=10808' \
        'username=' 'password=' >"$CONFIG_DIR/subscription-proxy.conf"
    chmod 600 "$CONFIG_DIR/subscription-proxy.conf"
    export TGMUOS_PROXY_CONFIG="$CONFIG_DIR/subscription-proxy.conf"
    "$BASE_DIR/tgmuos" "$@"
    APP_RESULT=$?
    exit "$APP_RESULT"
fi

export TGMUOS_PROXY_CONFIG="$CONFIG_DIR/proxy.conf"
exec "$BASE_DIR/tgmuos" "$@"
