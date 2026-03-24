from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path


ROOT = Path(r"D:\code\SmartDashboard")
APP = ROOT / "build" / "SmartDashboard" / "Debug" / "SmartDashboardApp.exe"
DS = Path(r"D:\code\Robot_Simulation\Source\Application\DriverStation\x64\Debug\DriverStation.exe")
DEBUG_DIR = ROOT / ".debug"
UI_LOG = DEBUG_DIR / "native_link_ui_ds-final.log"
STARTUP_LOG = DEBUG_DIR / "native_link_startup_ds-final.log"


def cleanup() -> None:
    subprocess.run(["taskkill", "/IM", "SmartDashboardApp.exe", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["taskkill", "/IM", "DriverStation.exe", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    cleanup()
    UI_LOG.unlink(missing_ok=True)
    STARTUP_LOG.unlink(missing_ok=True)

    env = os.environ.copy()
    env["SMARTDASHBOARD_WORKSPACE_ROOT"] = str(ROOT)
    env["SMARTDASHBOARD_INSTANCE_TAG"] = "ds-final"

    ds_env = os.environ.copy()
    ds_env["NATIVE_LINK_CARRIER"] = "tcp"
    ds_env["NATIVE_LINK_HOST"] = "127.0.0.1"
    ds_env["NATIVE_LINK_PORT"] = "5810"

    dashboard = subprocess.Popen([str(APP), "--instance-tag", "ds-final"], env=env)
    try:
        time.sleep(2.0)
        driver_station = subprocess.Popen([str(DS), "conn=native mode=auton autostart"], env=ds_env)
        try:
            time.sleep(10.0)
        finally:
            driver_station.terminate()
            try:
                driver_station.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                driver_station.kill()
                driver_station.wait(timeout=5.0)
    finally:
        dashboard.terminate()
        try:
            dashboard.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            dashboard.kill()
            dashboard.wait(timeout=5.0)
        cleanup()

    print(UI_LOG)
    print(STARTUP_LOG)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
