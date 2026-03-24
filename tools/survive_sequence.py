import subprocess
import sys
import time
from pathlib import Path


SMARTDASHBOARD_HELPER = Path(r"D:\code\SmartDashboard\tools\smartdashboard_process.py")
STRESS_HARNESS = Path(r"D:\code\SmartDashboard\tools\direct_stress_harness.py")
SMARTDASHBOARD_PROBE = Path(r"D:\code\SmartDashboard\build\ClientInterface_direct\Debug\DirectStateProbeCli.exe")
DRIVERSTATION_EXE = Path(r"D:\code\Robot_Simulation\build-vcpkg\bin\Debug\DriverStation.exe")


def run(args, timeout=None, check=True):
    return subprocess.run(args, timeout=timeout, check=check, capture_output=True, text=True)


def close_all():
    subprocess.run([sys.executable, str(SMARTDASHBOARD_HELPER), "close"], check=False, capture_output=True, text=True)
    subprocess.run([
        "powershell",
        "-Command",
        "Get-Process SmartDashboardApp,DriverStation -ErrorAction SilentlyContinue | Stop-Process -Force",
    ], check=False, capture_output=True, text=True)


def main():
    close_all()

    print("phase=dashboard_survive_start")
    print(run([sys.executable, str(SMARTDASHBOARD_HELPER), "launch"], check=False).stdout.strip())
    time.sleep(2.0)
    print(run([str(SMARTDASHBOARD_PROBE), "2000", "--seed", "--chooser", "--seed-ms", "2500"], check=False).stdout.strip())
    time.sleep(2.0)
    print(run([sys.executable, str(SMARTDASHBOARD_HELPER), "restart"], check=False).stdout.strip())
    time.sleep(3.0)
    reprobe = run([str(SMARTDASHBOARD_PROBE), "2000", "--chooser"], check=False)
    print(f"phase=dashboard_survive_probe_rc={reprobe.returncode}")
    if reprobe.stdout:
        print(reprobe.stdout.strip())

    print("phase=robot_survive_start")
    stress = run([sys.executable, str(STRESS_HARNESS), "1", "4", "--chooser"], timeout=300000, check=False)
    print(f"phase=robot_survive_rc={stress.returncode}")
    if stress.stdout:
        print(stress.stdout.strip())
    if stress.stderr:
        print(stress.stderr.strip())

    close_all()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
