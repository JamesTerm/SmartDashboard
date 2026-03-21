"""
Native Link SHM live telemetry verification.

Starts DriverStation_TransportSmoke + SmartDashboardApp (headless), waits for
the dashboard UI log to show live updates for keys that were previously silently
dropped (Velocity, X_ft, Heading, wheel velocities).  Passes only if:

  1. All REQUIRED_KEYS appear in the log (proves auto-register delivers them).
  2. At least one REQUIRED_LIVE_KEYS shows >=2 distinct values (proves they
     are live updates, not a single retained snapshot).

Usage:
    python tools/native_link_live_telemetry_verify.py

Exit codes:
    0  - pass
    1  - fail (missing keys or no live change)
    2  - setup error (binary not found, transport never started)
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(r"D:\code\SmartDashboard")
APP = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
AUTH = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation_TransportSmoke.exe")
DEBUG_DIR = ROOT / ".debug"
REGKEY = r"HKCU\Software\SmartDashboard\SmartDashboardApp\connection"
TAG = "telemetry-verify"

# Keys that must appear at all AND are logged by IsHarnessFocusKey in main_window.cpp.
# Heading/Travel_Heading are delivered to the dashboard (tiles are created) but are
# intentionally not in the harness log allowlist, so we don't assert on them here.
# The sequence-gap analysis in the log confirms they are received (the ~16 gaps per
# cycle exactly account for the ~16 non-logged auto-registered topics per sim loop).
#
# Ian: If TeleAutonV2 adds new keys that should be validated here, add them to
# both this list AND IsHarnessFocusKey in main_window.cpp.  A key missing from
# IsHarnessFocusKey will show zero log lines in this script even if the transport
# is delivering it correctly.  The root cause of the original live-telemetry gap
# (Velocity/wheel velocities silently dropped) was NativeLink.cpp's FindTopic()
# gate rejecting un-registered topics in Core::PublishInternal — fixed by
# auto-registering on server-originated writes when the topic is unknown.
REQUIRED_KEYS = [
    "Velocity",
    "X_ft",
    "Y_ft",
    "Wheel_fl_Velocity",
    "Wheel_fr_Velocity",
    "Wheel_rl_Velocity",
    "Wheel_rr_Velocity",
]

# Subset of the above that must show >=2 distinct numeric values
# (proves the live stream is actually changing, not a frozen snapshot).
REQUIRED_LIVE_KEYS = [
    "Velocity",
    "Y_ft",
    "Wheel_fl_Velocity",
]

# How long the authority runs per cycle (ms).  Long enough that the sim loop
# produces many updates but short enough for the test to finish quickly.
AUTHORITY_RUN_MS = "8000"

# How long to wait for the transport to start after launching.
TRANSPORT_START_TIMEOUT = 15.0

# How long to collect live updates after transport_start.
COLLECTION_SECONDS = 10.0


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
        ("ntClientName", "REG_SZ", f"{TAG}-dashboard"),
        (
            "pluginSettingsJson",
            "REG_SZ",
            f'{{"carrier":"shm","channel_id":"native-link-default","client_name":"{TAG}-dashboard"}}',
        ),
    ]
    for name, reg_type, value in settings:
        result = run(["reg", "add", REGKEY, "/v", name, "/t", reg_type, "/d", value, "/f"])
        if result.returncode != 0:
            raise RuntimeError(f"failed_to_configure={name}: {result.stderr.strip()}")


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


def all_values_for_key(log_text: str, key: str) -> list[str]:
    """Return every value string logged for the given key."""
    return re.findall(
        rf"update key={re.escape(key)} value=([^\r\n]+?) seq=",
        log_text,
    )


def main() -> int:
    if not APP.exists():
        print(f"missing_app={APP}")
        return 2
    if not AUTH.exists():
        print(f"missing_authority={AUTH}")
        return 2

    DEBUG_DIR.mkdir(parents=True, exist_ok=True)
    ui_log = DEBUG_DIR / f"native_link_ui_{TAG}.log"
    startup_log = DEBUG_DIR / f"native_link_startup_{TAG}.log"

    stop_processes()
    configure_registry()
    for path in (ui_log, startup_log):
        if path.exists():
            path.unlink()

    auth_env = os.environ.copy()
    auth_env["NATIVE_LINK_CARRIER"] = "shm"
    auth_env["NATIVE_LINK_CHANNEL_ID"] = "native-link-default"

    dash_env = os.environ.copy()
    dash_env["SMARTDASHBOARD_WORKSPACE_ROOT"] = str(ROOT)
    dash_env["SMARTDASHBOARD_INSTANCE_TAG"] = TAG

    authority = subprocess.Popen(
        [str(AUTH), AUTHORITY_RUN_MS],
        cwd=r"D:\code\Robot_Simulation",
        env=auth_env,
    )
    time.sleep(0.5)

    dashboard = subprocess.Popen(
        [str(APP), "--instance-tag", TAG],
        cwd=str(ROOT),
        env=dash_env,
    )
    dash_pid = dashboard.pid
    print(f"authority_pid={authority.pid} dashboard_pid={dash_pid}")

    try:
        # Wait for transport to come up.
        if not wait_for_log(startup_log, "startup_transport=native-link", TRANSPORT_START_TIMEOUT):
            print("FAIL: startup_transport=native-link never appeared")
            print(read_text(startup_log).strip())
            return 2
        if not wait_for_log(ui_log, "transport_start_result=ok", TRANSPORT_START_TIMEOUT):
            print("FAIL: transport_start_result=ok never appeared")
            print(read_text(ui_log).strip())
            return 2

        print(f"transport_up — collecting live updates for {COLLECTION_SECONDS}s ...")
        time.sleep(COLLECTION_SECONDS)

        log_text = read_text(ui_log)

        # ── Check 1: all required keys must appear at least once ──────────────
        missing = []
        for key in REQUIRED_KEYS:
            vals = all_values_for_key(log_text, key)
            if not vals:
                missing.append(key)
            else:
                print(f"  present key={key} count={len(vals)} sample={vals[0]}")

        if missing:
            print(f"FAIL: keys_never_delivered={missing}")
            # Show the full log tail for diagnosis.
            lines = log_text.splitlines()
            print("--- log tail (last 40 lines) ---")
            for line in lines[-40:]:
                print(line)
            return 1

        # ── Check 2: live keys must have >=2 distinct values ──────────────────
        frozen = []
        for key in REQUIRED_LIVE_KEYS:
            vals = all_values_for_key(log_text, key)
            distinct = set(vals)
            if len(distinct) < 2:
                frozen.append((key, vals[0] if vals else "?"))
            else:
                print(f"  live    key={key} distinct_values={len(distinct)}")

        if frozen:
            print(f"FAIL: keys_appear_frozen={[(k, v) for k, v in frozen]}")
            print("      (single value seen — snapshot only, no live updates)")
            # Show relevant lines for diagnosis.
            for key, _ in frozen:
                pattern = re.compile(rf"update key={re.escape(key)} .*")
                matches = pattern.findall(log_text)
                print(f"  all log lines for {key}:")
                for m in matches[:20]:
                    print(f"    {m}")
            return 1

        print("native_link_live_telemetry_verify=ok")
        return 0

    finally:
        stop_processes()


if __name__ == "__main__":
    raise SystemExit(main())
