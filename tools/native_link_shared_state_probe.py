import re
import subprocess
import sys
import time
from pathlib import Path


SMOKE_HELPER = Path(r"D:\code\SmartDashboard\tools\native_link_multi_instance_smoke.py")
DEBUG_DIR = Path(r"D:\code\SmartDashboard\.debug")


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


def main() -> int:
    first_log = DEBUG_DIR / "native_link_ui_dashboard-a.log"
    second_log = DEBUG_DIR / "native_link_ui_dashboard-b.log"

    # Ian: Delete stale probe artifacts up front. A false pass here would be
    # worse than a false failure because it would tell us two real dashboards
    # shared state when only old logs did.
    for path in (first_log, second_log):
        if path.exists():
            path.unlink()

    result = subprocess.run(
        [sys.executable, str(SMOKE_HELPER), "--linger-seconds", "5"],
        check=False,
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        print(result.stdout.strip())
        print(result.stderr.strip())
        return result.returncode

    print(result.stdout.strip())

    # Ian: The dashboards are real GUI processes, so log file creation can lag
    # slightly behind the launcher returning. Give the per-instance logs a short
    # settle window so the probe checks actual shared-state behavior rather than
    # filesystem timing luck.
    log_a = wait_for_log_content(first_log, 3.0)
    log_b = wait_for_log_content(second_log, 3.0)
    if not log_a or not log_b:
        print(f"log_a_exists={first_log.exists()} log_b_exists={second_log.exists()}")
        print("known_limitation=second_dashboard_can_start_via_native_link_multi_instance_gate_but_ui_log_capture_is_not_yet_confirming_both_instances")
        print("missing_ui_logs")
        return 1

    required_patterns = [
        r"transport_start id=native-link",
        r"update key=Test/Auton_Selection/AutoChooser/selected value=Do Nothing",
        r"update key=TestMove value=0",
    ]

    # Ian: Keep the first shared-state proof intentionally small and explicit.
    # We only need enough retained topics to prove that both real dashboard
    # processes are observing the same Native Link startup truth before we add
    # more stressful cross-process write/read scenarios.
    for pattern in required_patterns:
        if re.search(pattern, log_a) is None:
            print(f"dashboard_a_missing={pattern}")
            return 1
        if re.search(pattern, log_b) is None:
            print(f"dashboard_b_missing={pattern}")
            return 1

    # Ian: The next threshold after shared startup state is a real cross-process
    # write/read proof. One dashboard process republishes remembered TestMove on
    # startup; the other dashboard must observe that same updated value through
    # the shared Native Link authority instead of staying on its own private
    # default.
    if re.search(r"update key=TestMove value=3\.5", log_a) is None:
        print("dashboard_a_missing_cross_process_testmove_update")
        return 1
    if re.search(r"update key=TestMove value=3\.5", log_b) is None:
        print("dashboard_b_missing_cross_process_testmove_update")
        return 1

    print("native_link_shared_state_probe=ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
