import os
import subprocess
import time
from pathlib import Path


ROOT = Path(r"D:\code\SmartDashboard")
APP = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
AUTH = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")
REGKEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"
TAG = "manual-local-watch"


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kwargs)


def stop_processes() -> None:
    run([
        "powershell",
        "-NoProfile",
        "-Command",
        "Get-Process SmartDashboardApp,DriverStation_TransportSmoke -ErrorAction SilentlyContinue | Stop-Process -Force",
    ])
    time.sleep(1.0)


def configure_registry() -> None:
    settings = [
        ("transportKind", "REG_DWORD", "1"),
        ("transportId", "REG_SZ", "native-link"),
        ("ntClientName", "REG_SZ", "local-watch-dashboard"),
        ("ntHost", "REG_SZ", "127.0.0.1"),
        ("ntUseTeam", "REG_DWORD", "0"),
        (
            "pluginSettingsJson",
            "REG_SZ",
            '{"carrier":"tcp","host":"127.0.0.1","port":5810,"channel_id":"native-link-default"}',
        ),
    ]
    for name, reg_type, value in settings:
        result = run(["reg", "add", REGKEY, "/v", name, "/t", reg_type, "/d", value, "/f"])
        if result.returncode != 0:
            raise RuntimeError(f"failed_registry_{name}: {result.stderr.strip()}")


def remove_logs() -> None:
    for path in [
        ROOT / ".debug" / f"native_link_ui_{TAG}.log",
        ROOT / ".debug" / f"native_link_startup_{TAG}.log",
    ]:
        if path.exists():
            path.unlink()


def sample_title() -> str:
    result = run([
        "powershell",
        "-NoProfile",
        "-Command",
        "Get-Process SmartDashboardApp -ErrorAction SilentlyContinue | Select-Object -ExpandProperty MainWindowTitle",
    ])
    return result.stdout.strip()


def read_log(name: str) -> str:
    path = ROOT / ".debug" / name
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def launch_pair() -> tuple[subprocess.Popen, subprocess.Popen]:
    auth_env = os.environ.copy()
    auth_env.update({
        "NATIVE_LINK_CARRIER": "tcp",
        "NATIVE_LINK_HOST": "127.0.0.1",
        "NATIVE_LINK_PORT": "5810",
        "NATIVE_LINK_CHANNEL_ID": "native-link-default",
    })
    dash_env = os.environ.copy()
    dash_env.update({
        "SMARTDASHBOARD_WORKSPACE_ROOT": str(ROOT),
        "SMARTDASHBOARD_INSTANCE_TAG": TAG,
    })
    auth = subprocess.Popen([str(AUTH), "30000"], cwd=r"D:\code\Robot_Simulation", env=auth_env)
    dash = subprocess.Popen([str(APP), "--instance-tag", TAG], cwd=str(ROOT), env=dash_env)
    return auth, dash


def attempt() -> bool:
    stop_processes()
    configure_registry()
    remove_logs()
    auth, dash = launch_pair()
    print(f"auth_pid={auth.pid} dash_pid={dash.pid}")

    titles = []
    try:
        for i in range(10):
            time.sleep(1.0)
            title = sample_title()
            titles.append(title)
            print(f"title_sample_{i + 1}={title}")

        ui_log = read_log(f"native_link_ui_{TAG}.log")
        startup_log = read_log(f"native_link_startup_{TAG}.log")
        print("ui_log=")
        print(ui_log.strip())
        print("startup_log=")
        print(startup_log.strip())

        success = any("Connected" in title for title in titles)
        print(f"local_watch_success={success}")
        return success
    finally:
        stop_processes()


def main() -> int:
    for iteration in range(1, 4):
        print(f"iteration={iteration}")
        if attempt():
            return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
