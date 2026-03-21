"""
Native Link SHM disconnect/reconnect stress test.

Uses the debug command channel (QLocalServer named pipe) added to SmartDashboardApp
in Debug builds to reliably trigger connect/disconnect without keyboard injection.

Usage:
    python tools/native_link_shm_disconnect_stress.py [cycles] [pause_ms]

Exit codes:
    0 – all cycles passed (no crash)
    1 – crash/disappearance detected (or setup failure)
    2 – required binaries missing
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(r"D:\code\SmartDashboard")
APP = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
AUTH = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")
REGKEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"
TAG = "shm-stress"

# Win32 constants for named pipe access
GENERIC_READ_WRITE = 0xC0000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

kernel32 = ctypes.windll.kernel32


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kwargs)


def stop_processes() -> None:
    run(["taskkill", "/IM", "SmartDashboardApp.exe", "/F"])
    run(["taskkill", "/IM", "DriverStation_TransportSmoke.exe", "/F"])
    time.sleep(1.0)


def configure_registry() -> None:
    settings = [
        ("transportKind", "REG_DWORD", "1"),
        ("transportId", "REG_SZ", "native-link"),
        ("ntClientName", "REG_SZ", "shm-stress-dashboard"),
        (
            "pluginSettingsJson",
            "REG_SZ",
            '{"carrier":"shm","channel_id":"native-link-default","client_name":"shm-stress-dashboard"}',
        ),
    ]
    for name, reg_type, value in settings:
        result = run(["reg", "add", REGKEY, "/v", name, "/t", reg_type, "/d", value, "/f"])
        if result.returncode != 0:
            raise RuntimeError(f"failed_to_configure_setting={name}: {result.stderr.strip()}")


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for_log(path: Path, needle: str, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if needle in read_text(path):
            return True
        time.sleep(0.2)
    return False


def wait_for_log_count(path: Path, needle: str, min_count: int, timeout_s: float) -> bool:
    """Wait until `needle` appears at least `min_count` times in the log.

    Ian: Use occurrence-count rather than just presence because the UI log is
    append-only across the whole run.  Counting occurrences of
    "connection_state=Connected" lets each cycle wait for its own Nth Connected
    event without being fooled by earlier cycles' entries still in the log.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        text = read_text(path)
        if text.count(needle) >= min_count:
            return True
        time.sleep(0.2)
    return False


def process_alive(pid: int) -> bool:
    result = run(["tasklist", "/FI", f"PID eq {pid}"])
    return str(pid) in result.stdout


def send_debug_command(pid: int, cmd: str, timeout_s: float = 3.0) -> bool:
    """
    Send a command to the SmartDashboardApp debug named-pipe channel using Win32 APIs.
    QLocalServer on Windows creates: \\\\.\pipe\\<server_name>
    Returns True if the server acknowledged with 'ok'.
    """
    pipe_name = f"\\\\.\\pipe\\SmartDashboardApp_DebugCmd_{pid}"
    deadline = time.time() + timeout_s
    last_err = 0

    while time.time() < deadline:
        handle = kernel32.CreateFileW(
            pipe_name,
            GENERIC_READ_WRITE,
            0,        # no sharing
            None,     # default security
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            None,
        )
        if handle == INVALID_HANDLE_VALUE:
            last_err = kernel32.GetLastError()
            # ERROR_PIPE_BUSY (231) = all pipe instances busy; wait and retry
            # ERROR_FILE_NOT_FOUND (2) = server not yet listening; retry
            time.sleep(0.1)
            continue

        try:
            payload = f"{cmd}\n".encode("utf-8")
            bytes_written = wt.DWORD(0)
            ok = kernel32.WriteFile(handle, payload, len(payload), ctypes.byref(bytes_written), None)
            if not ok:
                last_err = kernel32.GetLastError()
                return False

            # Read the "ok\n" response
            buf = ctypes.create_string_buffer(64)
            bytes_read = wt.DWORD(0)
            kernel32.ReadFile(handle, buf, 63, ctypes.byref(bytes_read), None)
            response = buf.raw[: bytes_read.value].decode("utf-8", errors="replace").strip()
            return response == "ok"
        finally:
            kernel32.CloseHandle(handle)

    print(f"  send_debug_command failed cmd={cmd} pipe={pipe_name} last_err={last_err}")
    return False


def wait_for_title_contains(pid: int, needle: str, timeout_s: float) -> bool:
    """Poll the process's main window title for the expected substring."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        result = run([
            "powershell", "-NoProfile", "-Command",
            f"(Get-Process -Id {pid} -ErrorAction SilentlyContinue).MainWindowTitle",
        ])
        if needle in result.stdout:
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    cycles = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    pause_ms = int(sys.argv[2]) if len(sys.argv) > 2 else 800

    if not APP.exists():
        print(f"missing_app={APP}")
        return 2
    if not AUTH.exists():
        print(f"missing_authority={AUTH}")
        return 2

    ui_log = ROOT / ".debug" / f"native_link_ui_{TAG}.log"
    startup_log = ROOT / ".debug" / f"native_link_startup_{TAG}.log"

    stop_processes()
    configure_registry()
    for path in (ui_log, startup_log):
        if path.exists():
            path.unlink()

    dash_env = os.environ.copy()
    dash_env["SMARTDASHBOARD_WORKSPACE_ROOT"] = str(ROOT)
    dash_env["SMARTDASHBOARD_INSTANCE_TAG"] = TAG

    auth_env = os.environ.copy()
    auth_env["NATIVE_LINK_CARRIER"] = "shm"
    auth_env["NATIVE_LINK_CHANNEL_ID"] = "native-link-default"

    # Compute how long the authority needs to run:
    # Each cycle does: wait-for-Disconnected (up to 5s) + pause_ms + wait-for-Connected
    # (up to 10s) + pause_ms.  Budget generously: (cycles * (pause_ms*2 + 15000)) ms,
    # plus 20s startup headroom.  Minimum 60s.
    # Ian: The original hardcoded 60000 ms expired before 50 cycles completed,
    # producing authority timeouts on cycles 44-50.  This formula keeps the
    # authority alive for the full stress run regardless of cycle count.
    authority_run_ms = max(60_000, cycles * (pause_ms * 2 + 15_000) + 20_000)

    # Authority must start first; dashboard second.
    authority = subprocess.Popen(
        [str(AUTH), str(authority_run_ms)],
        cwd=r"D:\code\Robot_Simulation",
        env=auth_env,
    )
    time.sleep(0.5)
    dashboard = subprocess.Popen([str(APP), "--instance-tag", TAG], cwd=str(ROOT), env=dash_env)
    dash_pid = dashboard.pid
    print(f"dashboard_pid={dash_pid} authority_run_ms={authority_run_ms}")

    try:
        if not wait_for_log(startup_log, "startup_transport=native-link", 10.0):
            print("missing_startup_transport_native_link")
            print(read_text(startup_log).strip())
            return 1
        if not wait_for_log(ui_log, "transport_start_result=ok", 10.0):
            print("initial_transport_start_failed")
            print(read_text(ui_log).strip())
            return 1

        # Verify debug command channel is listening
        if not wait_for_log(ui_log, "debug_cmd_server=listening", 5.0):
            print("debug_cmd_server_not_listening - is this a Debug build?")
            print(read_text(ui_log).strip())
            return 1

        # Wait for the initial Connected state before starting cycles.
        # Without this, cycle 1's disconnect fires while the dashboard is still
        # draining the initial SHM snapshot (in "Connecting" state).
        # Ian: The SHM snapshot can take several seconds to drain on first connect
        # because the dashboard must process the full descriptor + state replay
        # before the authority advances to "live" delivery.  Firing disconnect
        # before that completes produces a spurious use-after-free scenario that
        # was the original motivation for the alive guard in dashboard_transport.cpp.
        if not wait_for_log_count(ui_log, "connection_state=Connected", 1, 15.0):
            print("initial_connected_state_never_reached")
            print(read_text(ui_log).strip())
            return 1

        print(f"setup_ok cycles={cycles} pause_ms={pause_ms}")

        # Track how many times we've seen each state, so we can wait for the
        # Nth occurrence that belongs to the current cycle (log is append-only).
        connected_count = 1  # we already saw 1 from initial connect above

        for cycle in range(1, cycles + 1):
            # --- Disconnect ---
            if not send_debug_command(dash_pid, "disconnect"):
                if not process_alive(dash_pid):
                    print(f"dashboard_disappeared_sending_disconnect cycle={cycle}")
                    print(read_text(ui_log).strip())
                    return 1
                print(f"disconnect_command_failed cycle={cycle}")
                return 1

            # Wait for the dashboard to actually reach Disconnected state
            # (title is the most reliable indicator — it's set on the main thread).
            if not wait_for_title_contains(dash_pid, "Disconnected", 5.0):
                if not process_alive(dash_pid):
                    print(f"dashboard_disappeared_on_disconnect cycle={cycle}")
                    print(read_text(ui_log).strip())
                    return 1
                title_result = run([
                    "powershell", "-NoProfile", "-Command",
                    f"(Get-Process -Id {dash_pid} -ErrorAction SilentlyContinue).MainWindowTitle",
                ])
                print(f"  warning: title_not_disconnected cycle={cycle} title={title_result.stdout.strip()!r}")

            time.sleep(pause_ms / 1000.0)

            if not process_alive(dash_pid):
                print(f"dashboard_disappeared_after_disconnect_pause cycle={cycle}")
                print(read_text(ui_log).strip())
                return 1

            # --- Reconnect ---
            if not send_debug_command(dash_pid, "connect"):
                if not process_alive(dash_pid):
                    print(f"dashboard_disappeared_sending_connect cycle={cycle}")
                    print(read_text(ui_log).strip())
                    return 1
                print(f"connect_command_failed cycle={cycle}")
                return 1

            # Wait for the Nth Connected state to appear in the log, proving
            # the snapshot was fully delivered before we fire the next disconnect.
            connected_count += 1
            if not wait_for_log_count(ui_log, "connection_state=Connected", connected_count, 10.0):
                if not process_alive(dash_pid):
                    print(f"dashboard_disappeared_waiting_connected cycle={cycle}")
                    print(read_text(ui_log).strip())
                    return 1
                print(f"  warning: connected_state_timeout cycle={cycle} expected_count={connected_count}")

            time.sleep(pause_ms / 1000.0)

            if not process_alive(dash_pid):
                print(f"dashboard_disappeared_on_reconnect cycle={cycle}")
                print(read_text(ui_log).strip())
                return 1

            print(f"cycle={cycle} alive=1")

        print("native_link_shm_disconnect_stress=ok")
        return 0
    finally:
        stop_processes()


if __name__ == "__main__":
    raise SystemExit(main())
