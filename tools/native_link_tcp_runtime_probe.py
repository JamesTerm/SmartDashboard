import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(r"D:\code\SmartDashboard")
AUTHORITY_EXE = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")
APP_EXE = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
DEBUG_DIR = ROOT / ".debug"
REGISTRY_KEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def stop_processes() -> None:
    run(["powershell", "-NoProfile", "-Command", "Get-Process SmartDashboardApp,DriverStation_TransportSmoke -ErrorAction SilentlyContinue | Stop-Process -Force"])
    time.sleep(1.0)


def configure_dashboard_tcp() -> None:
    settings = [
        ("transportKind", "REG_DWORD", "1"),
        ("transportId", "REG_SZ", "native-link"),
        ("ntClientName", "REG_SZ", "tcp-probe-dashboard"),
        ("pluginSettingsJson", "REG_SZ", '{"carrier":"tcp","host":"127.0.0.1","port":5810,"channel_id":"native-link-default"}'),
    ]
    for name, reg_type, value in settings:
        result = run(["reg", "add", REGISTRY_KEY, "/v", name, "/t", reg_type, "/d", value, "/f"])
        if result.returncode != 0:
            raise RuntimeError(f"failed_to_configure_setting={name}: {result.stderr.strip()}")


def read_log(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for_pattern(path: Path, pattern: str, timeout_seconds: float) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if pattern in read_log(path):
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    ui_log = DEBUG_DIR / "native_link_ui_tcp-runtime.log"
    startup_log = DEBUG_DIR / "native_link_startup_tcp-runtime.log"

    stop_processes()
    for path in (ui_log, startup_log):
        if path.exists():
            path.unlink()

    configure_dashboard_tcp()

    authority_env = os.environ.copy()
    authority_env["NATIVE_LINK_CARRIER"] = "tcp"
    authority_env["NATIVE_LINK_HOST"] = "127.0.0.1"
    authority_env["NATIVE_LINK_PORT"] = "5810"
    authority_env["NATIVE_LINK_CHANNEL_ID"] = "native-link-default"

    dashboard_env = os.environ.copy()
    dashboard_env["SMARTDASHBOARD_WORKSPACE_ROOT"] = str(ROOT)
    dashboard_env["SMARTDASHBOARD_INSTANCE_TAG"] = "tcp-runtime"

    authority = subprocess.Popen([str(AUTHORITY_EXE), "12000"], env=authority_env)
    time.sleep(1.0)
    dashboard = subprocess.Popen([str(APP_EXE), "--instance-tag", "tcp-runtime"], env=dashboard_env, creationflags=0x00000008)

    try:
        if not wait_for_pattern(ui_log, "transport_start id=native-link", 10.0):
            print("missing_transport_start")
            print(read_log(startup_log).strip())
            return 1
        if not wait_for_pattern(ui_log, "update key=Test/Auton_Selection/AutoChooser/selected value=Just Move Forward", 10.0):
            print("missing_retained_selected")
            print(read_log(ui_log).strip())
            return 1
        if not wait_for_pattern(ui_log, "update key=TestMove value=3.5", 10.0):
            print("missing_retained_testmove")
            print(read_log(ui_log).strip())
            return 1

        print("native_link_tcp_runtime_probe=ok")
        return 0
    finally:
        dashboard.poll()
        authority.poll()
        stop_processes()


if __name__ == "__main__":
    raise SystemExit(main())
