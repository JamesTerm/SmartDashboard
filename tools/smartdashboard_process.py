import ctypes
import os
import subprocess
import sys
import time


MUTEX_NAME = "Local\\SmartDashboard.SingleInstance"
PROCESS_NAME = "SmartDashboardApp.exe"
APP_PATH = r"D:\code\SmartDashboard\build\SmartDashboard\Debug\SmartDashboardApp.exe"
SYNCHRONIZE = 0x00100000


def is_running():
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenMutexW(SYNCHRONIZE, False, MUTEX_NAME)
    if handle:
        kernel32.CloseHandle(handle)
        return True
    return False


def launch():
    if is_running():
        print("already_running")
        return 0
    env = os.environ.copy()
    env["SMARTDASHBOARD_WORKSPACE_ROOT"] = r"D:\code\SmartDashboard"
    subprocess.Popen([APP_PATH], creationflags=0x00000008, env=env)
    deadline = time.time() + 10.0
    while time.time() < deadline:
        if is_running():
            print("launched")
            return 0
        time.sleep(0.1)
    print("launch_timeout")
    return 1


def close():
    subprocess.run([
        "taskkill",
        "/IM",
        PROCESS_NAME,
        "/F",
    ], check=False, capture_output=True, text=True)
    deadline = time.time() + 10.0
    while time.time() < deadline:
        if not is_running():
            print("closed")
            return 0
        time.sleep(0.1)
    print("close_timeout")
    return 1


def status():
    print("running" if is_running() else "not_running")
    return 0


def main():
    if len(sys.argv) < 2:
        print("usage: smartdashboard_process.py [status|launch|close|restart]")
        return 2

    command = sys.argv[1].lower()
    if command == "status":
        return status()
    if command == "launch":
        return launch()
    if command == "close":
        return close()
    if command == "restart":
        code = close()
        if code != 0:
            return code
        return launch()

    print(f"unknown_command={command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
