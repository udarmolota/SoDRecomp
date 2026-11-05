"""Phase 5b/6: records what limits State of Decay on the phone while the user plays.

    phone_sample.py <seconds> [interval]       (SOD_INSTANCE picks the instance, as phone_preset.py)

Every <interval> seconds (default 3): the GPU's busy percentage and clock (kgsl), the game's
busiest threads (top -H), the skin temperature and Android's thermal status. At the end it pulls
the instance's sod.log (VdSwap lines for fps; with DAO_GPU_STATS=1 in the instance's env.txt (the shared Xenia's name), the
Vulkan backend's per-frame counters every 2 s) and writes everything to
analysis/phone_samples/<time>/. Changes nothing on the phone.
"""
import datetime
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import phone_preset  # noqa: E402  (adb(), instance())

ROOT = Path(__file__).resolve().parents[1]
KGSL = "/sys/class/kgsl/kgsl-3d0"


def shell(command):
    try:
        return phone_preset.adb("shell", command).strip()
    except subprocess.CalledProcessError as e:
        return f"(failed: {e.returncode})"


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    seconds = int(sys.argv[1])
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
    path = phone_preset.instance()
    out = ROOT / "analysis" / "phone_samples" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    start = time.time()
    with open(out / "samples.txt", "w", encoding="utf-8") as f:
        while time.time() - start < seconds:
            t = time.time() - start
            pid = shell("pidof com.daodroid")
            gpu = shell(f"cat {KGSL}/gpu_busy_percentage {KGSL}/devfreq/cur_freq 2>/dev/null").split()
            threads = shell(f"top -H -b -n 1 -p {pid} | grep com.daodroid | head -6") if pid.isdigit() else ""
            busiest = " | ".join(f"{w[8]}% {' '.join(w[11:-1])}" for w in (l.split() for l in threads.splitlines()) if len(w) > 12)
            thermal = shell("dumpsys thermalservice | grep -m1 'Thermal Status'")
            skin = shell("dumpsys thermalservice | grep -o 'mValue=[0-9.]*, mType=3, mName=SKIN' | head -1")
            line = (f"{datetime.datetime.now():%H:%M:%S} +{t:5.0f}s gpu {' '.join(gpu) or '?'} | {busiest} | "
                    f"{thermal.replace('Thermal Status: ', 'thermal ')} {skin.replace('mValue=', 'skin ').split(',')[0]}")
            print(line, flush=True)
            f.write(line + "\n")
            f.flush()
            time.sleep(max(0.0, interval - (time.time() - start - t)))
    try:
        phone_preset.adb("pull", f"{path}/userdata/sod.log", str(out / "sod.log"))
    except subprocess.CalledProcessError:
        print("Couldn't pull sod.log (is the phone still connected?)")
    print(f"Saved to {out}")


if __name__ == "__main__":
    main()
