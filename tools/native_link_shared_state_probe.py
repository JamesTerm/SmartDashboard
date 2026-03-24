import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional


SMOKE_HELPER = Path(r"D:\code\SmartDashboard\tools\native_link_multi_instance_smoke.py")
DEBUG_DIR = Path(r"D:\code\SmartDashboard\.debug")
DEFAULT_AUTHORITY_EXE = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")


def read_log(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def wait_for_log_content(path: Path, timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        contents = read_log(path)
        if contents:
            return contents
        time.sleep(0.2)
    return ""


def wait_for_log_pattern(path: Path, pattern: str, timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    latest = ""
    while time.time() < deadline:
        latest = read_log(path)
        if latest and re.search(pattern, latest) is not None:
            return latest
        time.sleep(0.2)
    return latest


def latest_value_for_key(log_text: str, key: str) -> Optional[str]:
    matches = re.findall(rf"update key={re.escape(key)} value=([^\r\n]+?) seq=", log_text)
    if not matches:
        return None
    return matches[-1]


def launch_authority_if_available() -> Optional[subprocess.Popen]:
    if not DEFAULT_AUTHORITY_EXE.exists():
        return None

    # Ian: The real IPC dashboards are clients only. If this probe does not
    # start a temporary simulator-owned authority (or the caller does not start
    # one separately), the dashboards can launch and still never receive the
    # retained snapshot we are trying to validate.
    process = subprocess.Popen(
        [str(DEFAULT_AUTHORITY_EXE), "12000"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=build_authority_env(),
    )
    time.sleep(1.0)
    return process


def build_authority_env() -> dict[str, str]:
	env = os.environ.copy()
	env.setdefault("NATIVE_LINK_CARRIER", "shm")
	env.setdefault("NATIVE_LINK_CHANNEL_ID", "native-link-default")
	return env


def close_dashboards() -> None:
    subprocess.run(
        ["powershell", "-NoProfile", "-Command", "Get-Process SmartDashboardApp -ErrorAction SilentlyContinue | Stop-Process -Force"],
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    first_log = DEBUG_DIR / "native_link_ui_dashboard-a.log"
    second_log = DEBUG_DIR / "native_link_ui_dashboard-b.log"
    first_startup_log = DEBUG_DIR / "native_link_startup_dashboard-a.log"
    second_startup_log = DEBUG_DIR / "native_link_startup_dashboard-b.log"

    # Ian: Delete stale probe artifacts up front. A false pass here would be
    # worse than a false failure because it would tell us two real dashboards
    # shared state when only old logs did.
    close_dashboards()

    for path in (first_log, second_log, first_startup_log, second_startup_log):
        if path.exists():
            path.unlink()

    authority = launch_authority_if_available()

    try:
        result = subprocess.run(
            [sys.executable, str(SMOKE_HELPER), "--linger-seconds", "5", "--leave-running"],
            check=False,
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            print(result.stdout.strip())
            print(result.stderr.strip())
            return result.returncode

        print(result.stdout.strip())

        # Ian: Keep the authority alive while we wait for the retained markers.
        # The launcher helper returning only means both dashboards started; the
        # second process can still be draining its initial retained snapshot.
        # Tearing the authority down earlier makes the probe race its own server.
        log_a = wait_for_log_content(first_log, 3.0)
        log_b = wait_for_log_content(second_log, 3.0)
        if not log_a or not log_b:
            print(f"log_a_exists={first_log.exists()} log_b_exists={second_log.exists()}")
            print("known_limitation=second_dashboard_can_start_via_native_link_multi_instance_gate_but_ui_log_capture_is_not_yet_confirming_both_instances")
            print("missing_ui_logs")
            return 1

        # Ian: The UI log files can appear before the queued retained updates finish
        # draining through the main-window thread, especially for the second process.
        # Wait for the authority-seeded retained markers themselves instead of treating
        # early file creation as proof that the dashboard already finished startup.
        retained_anchor = r"update key=Test/Auton_Selection/AutoChooser/selected value=Just Move Forward"
        log_a = wait_for_log_pattern(first_log, retained_anchor, 6.0)
        log_b = wait_for_log_pattern(second_log, retained_anchor, 6.0)

        # Ian: The old SmartDashboard-owned scaffold started from dashboard-local
        # defaults like `Do Nothing` / `TestMove=0`. The real simulator-owned IPC
        # path should assert against authority-seeded values instead, or this probe
        # will keep flagging a false failure even when both dashboards observed the
        # same correct retained snapshot from Robot_Simulation.
        required_patterns = [
            r"transport_start id=native-link",
            r"update key=Test/Auton_Selection/AutoChooser/selected value=Just Move Forward",
            r"update key=TestMove value=",
        ]

        # Ian: Keep the first shared-state proof intentionally small and explicit.
        # We only need enough retained topics to prove that both real dashboard
        # processes are observing the same Native Link startup truth before we add
        # more stressful cross-process write/read scenarios.
        for pattern in required_patterns:
            if re.search(pattern, log_a) is None:
                print(f"dashboard_a_missing={pattern}")
                startup_a = read_log(first_startup_log)
                if startup_a:
                    print("startup_dashboard_a=")
                    print(startup_a.strip())
                return 1
            if re.search(pattern, log_b) is None:
                print(f"dashboard_b_missing={pattern}")
                startup_b = read_log(second_startup_log)
                if startup_b:
                    print("startup_dashboard_b=")
                    print(startup_b.strip())
                return 1

        latest_a = latest_value_for_key(log_a, "TestMove")
        latest_b = latest_value_for_key(log_b, "TestMove")
        if latest_a is None or latest_b is None:
            print(f"latest_testmove_dashboard_a={latest_a}")
            print(f"latest_testmove_dashboard_b={latest_b}")
            return 1
        if latest_a != latest_b:
            print(f"testmove_mismatch dashboard_a={latest_a} dashboard_b={latest_b}")
            return 1

        # Ian: The next threshold after shared startup state is a real cross-process
        # write/read proof. The remembered TestMove value can legitimately differ
        # from the simulator's initial seed, but both real dashboard processes must
        # converge on the same final value through the shared Native Link authority
        # instead of keeping private per-process control state.
        print(f"shared_testmove_value={latest_a}")
        print("native_link_shared_state_probe=ok")
        return 0
    finally:
        close_dashboards()
        if authority is not None:
            authority.terminate()
            try:
                authority.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                authority.kill()
                authority.wait(timeout=5.0)


if __name__ == "__main__":
    raise SystemExit(main())
