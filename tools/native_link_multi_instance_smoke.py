import os
import re
import subprocess
import sys
import time
from pathlib import Path


APP_PATH = Path(r"D:\code\SmartDashboard\build\SmartDashboard\Debug\SmartDashboardApp.exe")
PROCESS_NAME = "SmartDashboardApp.exe"
REGISTRY_KEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"
DEBUG_DIR = Path(r"D:\code\SmartDashboard\.debug")


def read_log(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for_log_pattern(path: Path, pattern: str, timeout_seconds: float) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        contents = read_log(path)
        if contents and re.search(pattern, contents) is not None:
            return True
        time.sleep(0.2)
    return False


def configure_native_link_settings(client_name: str) -> None:
    # Ian: This smoke helper is supposed to exercise Native Link startup, not
    # whatever transport the last manual run happened to leave in settings.
    # Force the persisted selection first so the result means something.
    settings = [
        ("transportKind", "REG_DWORD", "1"),
        ("transportId", "REG_SZ", "native-link"),
        ("ntClientName", "REG_SZ", client_name),
        ("pluginSettingsJson", "REG_SZ", '{"carrier":"shm","channel_id":"native-link-default","client_name":"' + client_name + '"}'),
    ]

    for name, reg_type, value in settings:
        result = subprocess.run(
            ["reg", "add", REGISTRY_KEY, "/v", name, "/t", reg_type, "/d", value, "/f"],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"failed_to_configure_setting={name}: {result.stderr.strip()}")


def run_powershell(command: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        check=False,
        capture_output=True,
        text=True,
    )


def close_all() -> None:
    run_powershell(f"Get-Process {PROCESS_NAME[:-4]} -ErrorAction SilentlyContinue | Stop-Process -Force")
    time.sleep(1.0)


def launch_instance(name: str, allow_multi_instance: bool) -> subprocess.Popen:
    env = os.environ.copy()
    env["SMARTDASHBOARD_WORKSPACE_ROOT"] = r"D:\code\SmartDashboard"
    env["SMARTDASHBOARD_INSTANCE_TAG"] = name
    args = [str(APP_PATH)]
    # Ian: Pass the instance tag on argv as well as through the environment
    # path in the app. That makes the per-process log naming resilient even if
    # one propagation path changes later.
    args.extend(["--instance-tag", name])
    if allow_multi_instance:
        args.append("--allow-multi-instance")

    return subprocess.Popen(args, env=env, creationflags=0x00000008)


def count_instances() -> int:
    result = run_powershell(f"@(Get-Process {PROCESS_NAME[:-4]} -ErrorAction SilentlyContinue).Count")
    if result.returncode != 0:
        return 0

    text = result.stdout.strip()
    try:
        return int(text) if text else 0
    except ValueError:
        return 0


def wait_for_instance_count(expected: int, timeout_seconds: float) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if count_instances() >= expected:
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    linger_seconds = 0.0
    leave_running = False
    for i in range(1, len(sys.argv)):
        if sys.argv[i] == "--linger-seconds" and i + 1 < len(sys.argv):
            linger_seconds = float(sys.argv[i + 1])
        if sys.argv[i] == "--leave-running":
            leave_running = True

    if not APP_PATH.exists():
        print(f"missing_app={APP_PATH}")
        return 2

    # Ian: Clean up any old helper leftovers first. The process-count checks in
    # this smoke are only meaningful when they describe the instances launched by
    # the current run rather than dashboards left behind by a previous probe.
    close_all()
    try:
        configure_native_link_settings("dashboard-a")
    except RuntimeError as exc:
        print(str(exc))
        return 1

    first = launch_instance("dashboard-a", allow_multi_instance=False)
    if not wait_for_instance_count(1, 10.0):
        print("first_launch_failed")
        close_all()
        return 1

    first_log = DEBUG_DIR / "native_link_ui_dashboard-a.log"
    if not wait_for_log_pattern(
        first_log,
        r"update key=Test/Auton_Selection/AutoChooser/selected value=Just Move Forward",
        8.0,
    ):
        # Ian: Launch the second real dashboard only after the first one proves
        # it crossed the retained snapshot boundary. That keeps this smoke aimed
        # at true multi-instance behavior instead of measuring arbitrary startup
        # overlap between two fresh GUI processes.
        print("first_instance_retained_startup_failed")
        first.poll()
        close_all()
        return 1

    try:
        # Ian: Real Native Link is multi-client, not multi-window-on-one-client.
        # Give each dashboard process its own logical client name before launch
        # or the simulator authority can legitimately collapse them into one
        # client identity and the probe stops proving true fan-out.
        configure_native_link_settings("dashboard-b")
    except RuntimeError as exc:
        print(str(exc))
        first.poll()
        close_all()
        return 1

    second = launch_instance("dashboard-b", allow_multi_instance=True)
    if not wait_for_instance_count(2, 10.0):
        print("second_launch_failed")
        first.poll()
        second.poll()
        close_all()
        return 1

    print("native_link_multi_instance_smoke=ok")
    print(f"instance_count={count_instances()}")
    print("transport_id=native-link")
    dump = run_powershell("Get-CimInstance Win32_Process -Filter \"Name='SmartDashboardApp.exe'\" | Select-Object ProcessId,CommandLine | Format-Table -HideTableHeaders")
    if dump.stdout.strip():
        print(dump.stdout.strip())

    if linger_seconds > 0.0:
        # Ian: The shared-state probe reads per-process UI logs after this
        # helper returns. Keep the dashboards alive briefly so queued retained
        # updates have a stable chance to reach the UI thread before teardown.
        print(f"linger_seconds={linger_seconds}")
        time.sleep(linger_seconds)

    if leave_running:
        # Ian: Returning while the dashboards are still alive gives the parent
        # probe a deterministic window to inspect real-process UI logs before a
        # forced cleanup can race the last retained updates.
        return 0

    first.poll()
    second.poll()
    close_all()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
