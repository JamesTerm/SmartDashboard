import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path


SMARTDASHBOARD_HELPER = Path(r"D:\code\SmartDashboard\tools\smartdashboard_process.py")
SMARTDASHBOARD_PROBE = Path(r"D:\code\SmartDashboard\build\ClientInterface_direct\Debug\DirectStateProbeCli.exe")
SMARTDASHBOARD_WATCH = Path(r"D:\code\SmartDashboard\build\ClientInterface_direct\Debug\DirectWatchCli.exe")
DRIVERSTATION_EXE = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation.exe")
ROBOT_LOG = Path(r"D:\code\Robot_Simulation\.debug\direct_transport_debug_log.txt")
AUTON_CHAIN_LOG = Path(r"D:\code\Robot_Simulation\.debug\direct_auton_chain_log.txt")
UI_LOG = Path(r"D:\code\SmartDashboard\.debug\direct_ui_debug_log.txt")
WATCH_LOG = Path(r"D:\code\SmartDashboard\.debug\direct_stress_watch.log")
WINDOW_TITLE = "DS Simulation"

IDC_TELE = 1002
IDC_AUTON = 1003
IDC_STOP = 1005
IDC_START = 1006

BM_CLICK = 0x00F5
WM_CLOSE = 0x0010


def run(args, timeout=None, check=True):
    return subprocess.run(args, timeout=timeout, check=check, capture_output=True, text=True)


def helper(command):
    return run([sys.executable, str(SMARTDASHBOARD_HELPER), command])


def kill_process_by_name(image_name):
    subprocess.run(["taskkill", "/IM", image_name, "/F"], check=False, capture_output=True, text=True)


def wait_for_window(title, timeout_s=12.0):
    user32 = ctypes.windll.user32
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        hwnd = user32.FindWindowW(None, title)
        if hwnd:
            return hwnd
        time.sleep(0.1)
    return 0


def click(hwnd, control_id):
    user32 = ctypes.windll.user32
    child = user32.GetDlgItem(hwnd, control_id)
    if not child:
        raise RuntimeError(f"control_not_found={control_id}")
    user32.SendMessageW(child, BM_CLICK, 0, 0)


def close_window(hwnd):
    ctypes.windll.user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)


def truncate_logs():
    for path in (ROBOT_LOG, AUTON_CHAIN_LOG, UI_LOG, WATCH_LOG):
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            try:
                path.unlink()
            except PermissionError:
                pass


def tail(path, lines=40):
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return text[-lines:]


def parse_last_numeric(log_lines, prefix):
    value = None
    for line in log_lines:
        marker = f"{prefix}="
        if marker in line:
            try:
                value = float(line.split(marker, 1)[1].strip())
            except ValueError:
                pass
    return value


def parse_probe_numeric(output, key):
    marker = f"{key}="
    for line in output.splitlines():
        if line.startswith(marker):
            try:
                return float(line.split("=", 1)[1].strip())
            except ValueError:
                return None
    return None


def log_contains(path, needle):
    if not path.exists():
        return False
    return needle in path.read_text(encoding="utf-8", errors="replace")


def log_count(path, needle):
    if not path.exists():
        return 0
    return path.read_text(encoding="utf-8", errors="replace").count(needle)


def make_probe_args(timeout_ms, use_chooser, seed=False, seed_ms=None):
    args = [str(SMARTDASHBOARD_PROBE), str(timeout_ms)]
    if seed:
        args.append("--seed")
        if seed_ms is not None:
            args.extend(["--seed-ms", str(seed_ms)])
    if use_chooser:
        args.append("--chooser")
    return args


def main():
    cycles = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    enable_seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    use_chooser = any(arg == "--chooser" for arg in sys.argv[3:])

    truncate_logs()
    helper("restart")

    # Ian: Numeric and chooser runs intentionally share one harness, so every
    # probe invocation must carry the mode flag explicitly. Otherwise a chooser
    # test can look like a numeric regression, or vice versa.
    watch = subprocess.Popen([str(SMARTDASHBOARD_WATCH), str(int((cycles * (enable_seconds + 3) + 10) * 1000)), str(WATCH_LOG)])
    ds = subprocess.Popen([str(DRIVERSTATION_EXE), "direct"])

    try:
        hwnd = wait_for_window(WINDOW_TITLE)
        if not hwnd:
            raise RuntimeError("driverstation_window_not_found")

        seed_duration_ms = max(2000, int((enable_seconds + 3.0) * 1000))
        seed = run(make_probe_args(2000, use_chooser, seed=True, seed_ms=seed_duration_ms), check=False)
        print(f"seed_probe_rc={seed.returncode}")
        if seed.stdout:
            print(seed.stdout.strip())
        if seed.stderr:
            print(seed.stderr.strip())

        click(hwnd, IDC_AUTON)
        time.sleep(0.5)

        for cycle in range(1, cycles + 1):
            click(hwnd, IDC_STOP)
            time.sleep(0.75)

            pre_probe = run(make_probe_args(2000, use_chooser), check=False)
            pre_output = pre_probe.stdout or ""
            print(f"cycle={cycle} pre_probe_rc={pre_probe.returncode}")
            if pre_output:
                print(pre_output.strip())
            if use_chooser:
                if "chooser.selected=Just Move Forward" not in pre_output:
                    print(f"cycle={cycle} chooser_selection_missing")
                    break
                if "AutonTest=1" in pre_output or "AutonTest=1.0" in pre_output:
                    print(f"cycle={cycle} chooser_seed_leaked_numeric_autontest")
                    break
            else:
                if "AutonTest=1" not in pre_output and "AutonTest=1.0" not in pre_output:
                    print(f"cycle={cycle} auton_selection_missing")
                    break
            if "TestMove=3.5" not in pre_output:
                print(f"cycle={cycle} precondition_failed")
                break

            stable_probe = run(make_probe_args(2000, use_chooser), check=False)
            stable_output = stable_probe.stdout or ""
            print(f"cycle={cycle} stable_probe_rc={stable_probe.returncode}")
            if stable_output:
                print(stable_output.strip())

            pre_timer = parse_probe_numeric(stable_output, "Timer")
            pre_y = parse_probe_numeric(stable_output, "Y_ft")
            pre_moveforward_count = log_count(AUTON_CHAIN_LOG, "[MoveForward] Activate TestMove=3.5")
            pre_drive_count = log_count(AUTON_CHAIN_LOG, "[TeleAutonV2] DriveToLocation")
            print(f"cycle={cycle} pre_timer={pre_timer} pre_y_ft={pre_y}")

            click(hwnd, IDC_START)
            print(f"cycle={cycle} action=enable")
            time.sleep(enable_seconds)

            probe = run(make_probe_args(2000, use_chooser), check=False)
            print(f"cycle={cycle} probe_rc={probe.returncode}")
            if probe.stdout:
                print(probe.stdout.strip())
            if probe.stderr:
                print(probe.stderr.strip())

            post_output = probe.stdout or ""
            post_timer = parse_probe_numeric(post_output, "Timer")
            post_y = parse_probe_numeric(post_output, "Y_ft")
            print(f"cycle={cycle} post_timer={post_timer} post_y_ft={post_y}")

            post_moveforward_count = log_count(AUTON_CHAIN_LOG, "[MoveForward] Activate TestMove=3.5")
            post_drive_count = log_count(AUTON_CHAIN_LOG, "[TeleAutonV2] DriveToLocation")
            moveforward_seen = post_moveforward_count > pre_moveforward_count
            drive_seen = post_drive_count > pre_drive_count
            motion_seen = (pre_y is not None and post_y is not None and abs(post_y - pre_y) >= 0.25)

            # Ian: Raw `Y_ft` snapshots can lag or reflect previous pose state,
            # but a fresh MoveForward/DriveToLocation pair proves the robot saw
            # the auton command chain for this cycle. Treat command-chain evidence
            # as the primary truth and `Y_ft` as supporting telemetry.
            if not moveforward_seen or not drive_seen:
                print(f"cycle={cycle} move_command_missing")
                break

            if not motion_seen:
                print(f"cycle={cycle} motion_snapshot_small but command_chain_ok=1")

            if pre_y is None or post_y is None:
                print(f"cycle={cycle} y_ft_unavailable")
                break

            if pre_timer is not None and post_timer is not None:
                timer_delta = post_timer - pre_timer
                print(f"cycle={cycle} timer_delta={timer_delta}")
            else:
                print(f"cycle={cycle} timer_unavailable")

            print(f"cycle={cycle} action=wait_complete")
            click(hwnd, IDC_STOP)
            print(f"cycle={cycle} action=disable")
            time.sleep(1.5)

        else:
            print("stress_result=pass")

        print("robot_log_tail:")
        for line in tail(ROBOT_LOG):
            print(line)
        print("auton_chain_log_tail:")
        for line in tail(AUTON_CHAIN_LOG):
            print(line)
        print("ui_log_tail:")
        for line in tail(UI_LOG):
            print(line)

    finally:
        try:
            hwnd = wait_for_window(WINDOW_TITLE, timeout_s=1.0)
            if hwnd:
                close_window(hwnd)
        except Exception:
            pass
        try:
            ds.wait(timeout=5)
        except Exception:
            ds.kill()
        kill_process_by_name("DriverStation.exe")
        helper("close")
        try:
            watch.wait(timeout=5)
        except Exception:
            watch.kill()
        kill_process_by_name("DirectWatchCli.exe")


if __name__ == "__main__":
    main()
