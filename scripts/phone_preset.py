"""Phase 5: graphics presets for State of Decay on the phone (PHASE5_LOG.md section 7).

    phone_preset.py set <preset|none>        presets/<preset>.cfg -> the instance's game/systemoverride.cfg
    phone_preset.py measure <label> [seconds]  fps from the instance's sod.log over the next N seconds

The instance is the newest one under the launcher's instances folder unless SOD_INSTANCE names
one. The engine reads systemoverride.cfg at boot only, so restart the game after `set`.
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADB = os.environ.get("ADB", str(Path.home() / "AppData/Local/Android/Sdk/platform-tools/adb.exe"))
INSTANCES = "/sdcard/Android/data/com.daodroid/files/dao/instances"


def adb(*args, capture=True):
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    return subprocess.run([ADB, *args], capture_output=capture, text=True, env=env, check=True).stdout


def instance():
    if os.environ.get("SOD_INSTANCE"):
        return f"{INSTANCES}/{os.environ['SOD_INSTANCE']}"
    names = adb("shell", f"ls -t {INSTANCES}").split()
    for name in names:  # newest first; SoD's has the SoD title in instance.json
        if "584111E8" in adb("shell", f"cat {INSTANCES}/{name}/instance.json"):
            return f"{INSTANCES}/{name}"
    sys.exit("No State of Decay instance on the phone")


def swaps(path):
    """(seconds, swap number) for each VdSwap line of the instance's sod.log."""
    text = adb("shell", f"grep 'VdSwap #' {path}/userdata/sod.log")
    return [(float(m.group(1)), int(m.group(2))) for m in re.finditer(r"\[\s*([\d.]+)\].*VdSwap #(\d+)", text)]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    cmd = sys.argv[1]
    path = instance()
    if cmd == "set":
        preset = sys.argv[2]
        target = f"{path}/game/systemoverride.cfg"
        if preset == "none":
            adb("shell", f"rm -f {target}")
        else:
            adb("push", str(ROOT / "presets" / f"{preset}.cfg"), target)
        print(f"{preset} -> {target}; restart the game")
    elif cmd == "measure":
        label, seconds = sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 120
        before = swaps(path)
        time.sleep(seconds)
        after = [s for s in swaps(path) if not before or s[1] > before[-1][1]]
        window = (before[-1:] + after) if before else after
        if len(window) < 2:
            sys.exit("Not enough swap lines in the window (600 swaps per line): measure longer")
        (t0, n0), (t1, n1) = window[0], window[-1]
        print(f"{label}: {(n1 - n0) / (t1 - t0):.1f} fps ({n1 - n0} swaps over {t1 - t0:.0f} s)")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
