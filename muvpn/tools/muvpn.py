#!/usr/bin/env python3

import fcntl
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


APP_DIR = Path(__file__).resolve().parent.parent
CONFIG_DIR = Path(os.environ.get("MUVPN_CONFIG_DIR", "/root/.config/muvpn"))
SUBSCRIPTION_CONFIG = Path(
    os.environ.get(
        "TGMUOS_SUBSCRIPTION_CONFIG",
        "/root/.config/tgmuos/subscription.conf",
    )
)
SERVER_LIST = CONFIG_DIR / "servers.tsv"
SINGBOX_CONFIG = CONFIG_DIR / "sing-box.json"
PID_FILE = CONFIG_DIR / "sing-box.pid"
LOCK_FILE = CONFIG_DIR / "service.lock"
LOG_FILE = CONFIG_DIR / "sing-box.log"
SINGBOX = APP_DIR / "libexec" / "sing-box"
IMPORTER = APP_DIR / "tools" / "import_vless_subscription.py"


def ensure_config_dir():
    CONFIG_DIR.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(CONFIG_DIR, 0o700)


def private_environment():
    environment = os.environ.copy()
    environment["TGMUOS_CONFIG_DIR"] = str(CONFIG_DIR)
    environment["TGMUOS_SUBSCRIPTION_CONFIG"] = str(SUBSCRIPTION_CONFIG)
    environment["TGMUOS_SERVERS_FILE"] = str(SERVER_LIST)
    environment["TGMUOS_SINGBOX_MODE"] = "tun"
    return environment


def run_importer():
    ensure_config_dir()
    if not SUBSCRIPTION_CONFIG.is_file():
        raise RuntimeError("VLESS subscription is not installed")
    result = subprocess.run(
        [sys.executable, str(IMPORTER)],
        env=private_environment(),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("VLESS subscription refresh failed")


def config_is_valid():
    result = subprocess.run(
        [str(SINGBOX), "check", "-c", str(SINGBOX_CONFIG)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("sing-box rejected the VPN configuration")


def read_pid():
    try:
        pid = int(PID_FILE.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return None
    return pid if pid > 1 else None


def process_matches(pid):
    try:
        executable = Path(f"/proc/{pid}/exe").resolve()
        command = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
    except (OSError, RuntimeError):
        return False
    return (
        executable == SINGBOX.resolve()
        and b"run" in command
        and str(SINGBOX_CONFIG).encode() in command
    )


def is_connected():
    pid = read_pid()
    if pid is None:
        return False
    if process_matches(pid):
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            pass
    try:
        PID_FILE.unlink()
    except FileNotFoundError:
        pass
    return False


def write_pid(pid):
    ensure_config_dir()
    fd, temporary = tempfile.mkstemp(prefix=".sing-box.pid.", dir=CONFIG_DIR)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="ascii") as output:
            output.write(f"{pid}\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, PID_FILE)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def start():
    ensure_config_dir()
    with open(LOCK_FILE, "a", encoding="ascii") as lock:
        os.chmod(LOCK_FILE, 0o600)
        fcntl.flock(lock, fcntl.LOCK_EX)
        if is_connected():
            print("VPN is already connected")
            return
        if not SINGBOX.is_file() or not os.access(SINGBOX, os.X_OK):
            raise RuntimeError("bundled sing-box engine is missing")
        run_importer()
        config_is_valid()
        log_fd = os.open(
            LOG_FILE,
            os.O_WRONLY | os.O_CREAT | os.O_APPEND,
            0o600,
        )
        try:
            os.fchmod(log_fd, 0o600)
            with os.fdopen(log_fd, "ab", buffering=0) as log:
                process = subprocess.Popen(
                    [str(SINGBOX), "run", "-c", str(SINGBOX_CONFIG)],
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    close_fds=True,
                    start_new_session=True,
                )
        except BaseException:
            try:
                os.close(log_fd)
            except OSError:
                pass
            raise
        write_pid(process.pid)
        time.sleep(0.3)
        if not process_matches(process.pid):
            try:
                PID_FILE.unlink()
            except FileNotFoundError:
                pass
            raise RuntimeError("VPN engine failed to start; see private log")
        print("VPN connection started")


def stop():
    pid = read_pid()
    if pid is None or not process_matches(pid):
        try:
            PID_FILE.unlink()
        except FileNotFoundError:
            pass
        print("VPN is already disconnected")
        return

    os.kill(pid, signal.SIGTERM)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if not process_matches(pid):
            break
        time.sleep(0.1)
    if process_matches(pid):
        os.kill(pid, signal.SIGKILL)
        for _ in range(20):
            if not process_matches(pid):
                break
            time.sleep(0.1)
    try:
        PID_FILE.unlink()
    except FileNotFoundError:
        pass
    print("VPN disconnected")


def read_servers():
    rows = []
    with open(SERVER_LIST, encoding="utf-8") as source:
        for line in source:
            fields = line.rstrip("\r\n").split("\t")
            if len(fields) >= 3 and len(fields[0]) == 16:
                rows.append(fields[0])
    return set(rows)


def select(server_id):
    ensure_config_dir()
    if server_id != "auto":
        if len(server_id) != 16 or any(c not in "0123456789abcdef" for c in server_id):
            raise ValueError("invalid server identifier")
        if server_id not in read_servers():
            raise ValueError("server is not in the current subscription")
    if not SUBSCRIPTION_CONFIG.is_file():
        raise RuntimeError("VLESS subscription is not installed")

    lines = SUBSCRIPTION_CONFIG.read_text(encoding="utf-8").splitlines()
    lines = [line for line in lines if not line.startswith("selected=")]
    if server_id != "auto":
        lines.append(f"selected={server_id}")
    fd, temporary = tempfile.mkstemp(
        prefix=".subscription.",
        dir=SUBSCRIPTION_CONFIG.parent,
    )
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            output.write("\n".join(lines) + "\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, SUBSCRIPTION_CONFIG)
        os.chmod(SUBSCRIPTION_CONFIG, 0o600)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise
    print("Server selection saved")


def main(arguments):
    if len(arguments) != 1 and not (
        len(arguments) == 2 and arguments[0] == "select"
    ):
        raise ValueError("usage: muvpn.py start|stop|status|refresh|select ID")
    action = arguments[0]
    if action == "start" and len(arguments) == 1:
        start()
    elif action == "stop" and len(arguments) == 1:
        stop()
    elif action == "status" and len(arguments) == 1:
        print("connected" if is_connected() else "disconnected")
    elif action == "refresh" and len(arguments) == 1:
        run_importer()
        config_is_valid()
        print("Subscription refreshed")
    elif action == "select" and len(arguments) == 2:
        select(arguments[1])
    else:
        raise ValueError("unknown muVPN action")


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"muVPN: {error}", file=sys.stderr)
        sys.exit(1)
