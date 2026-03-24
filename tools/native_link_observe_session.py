import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(r"D:\code\SmartDashboard")
APP = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
AUTH = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")
REGKEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"
TAG = "observe"


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kwargs)


def stop_processes() -> None:
    run(["taskkill", "/IM", "SmartDashboardApp.exe", "/F"])
    run(["taskkill", "/IM", "DriverStation_TransportSmoke.exe", "/F"])
    time.sleep(1.0)


def configure_native_link_registry() -> None:
    settings = [
        ("transportKind", "REG_DWORD", "1"),
        ("transportId", "REG_SZ", "native-link"),
        ("ntClientName", "REG_SZ", "observe-dashboard"),
        (
            "pluginSettingsJson",
            "REG_SZ",
            '{"carrier":"shm","channel_id":"native-link-default","client_name":"observe-dashboard"}',
        ),
    ]
    for name, reg_type, value in settings:
        result = run(["reg", "add", REGKEY, "/v", name, "/t", reg_type, "/d", value, "/f"])
        if result.returncode != 0:
            raise RuntimeError(f"failed_to_configure_setting={name}: {result.stderr.strip()}")


def clear_logs() -> None:
    for path in [
        ROOT / ".debug" / f"native_link_ui_{TAG}.log",
        ROOT / ".debug" / f"native_link_startup_{TAG}.log",
    ]:
        if path.exists():
            path.unlink()


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for_log_line(path: Path, needle: str, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if needle in read_text(path):
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    settle_seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    test_move = sys.argv[2] if len(sys.argv) > 2 else "10"
    run_ms = sys.argv[3] if len(sys.argv) > 3 else "10000"

    ui_log = ROOT / ".debug" / f"native_link_ui_{TAG}.log"
    startup_log = ROOT / ".debug" / f"native_link_startup_{TAG}.log"

    stop_processes()
    configure_native_link_registry()
    clear_logs()

    dash_env = os.environ.copy()
    dash_env["SMARTDASHBOARD_WORKSPACE_ROOT"] = str(ROOT)
    dash_env["SMARTDASHBOARD_INSTANCE_TAG"] = TAG

    auth_env = os.environ.copy()
    auth_env["NATIVE_LINK_CARRIER"] = "shm"
    auth_env["NATIVE_LINK_CHANNEL_ID"] = "native-link-default"

    authority = subprocess.Popen(
        [str(AUTH), str(run_ms), "--startup-delay-ms", str(settle_seconds * 1000), "--test-move", str(test_move)],
        cwd=r"D:\code\Robot_Simulation",
        env=auth_env,
    )
    dashboard = None
    try:
        time.sleep(1.0)
        dashboard = subprocess.Popen([str(APP), "--instance-tag", TAG], cwd=str(ROOT), env=dash_env)

        if not wait_for_log_line(startup_log, "startup_transport=native-link", 10.0):
            print("missing_startup_transport_native_link")
            print(read_text(startup_log).strip())
            return 1

        if not wait_for_log_line(ui_log, "transport_start_result=ok", 10.0):
            print("dashboard_transport_failed")
            print(read_text(ui_log).strip())
            return 1

        if not wait_for_log_line(ui_log, "connection_state=Connected", 12.0):
            print("dashboard_never_connected")
            print(read_text(ui_log).strip())
            return 1

        print(f"dashboard_settle_seconds={settle_seconds}")
        print(f"authority_started test_move={test_move} run_ms={run_ms}")
        authority.wait(timeout=(int(run_ms) / 1000.0) + 10.0)
        time.sleep(1.0)

        print("ui_log=")
        print(read_text(ui_log).strip())
        return 0
    finally:
        stop_processes()


if __name__ == "__main__":
    raise SystemExit(main())
