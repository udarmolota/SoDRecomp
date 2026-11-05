"""Stage 0 of RENDERER_PLAN.md: a benchmark for the phone, for both games. It reads and records;
it never starts the game (the user does) and never touches the renderer.

    phone_bench.py freeze <name>                 keep the instance's saves as a frozen save
    phone_bench.py prep [--save <name>] [--env KEY=VALUE ...] [--preset <id>]
                                                 restore the frozen save (the current saves are
                                                 backed up first), write env.txt and the preset
    phone_bench.py record <label> [--seconds 300] [--interval 3]
                                                 record now: the user has the game in the scene
    phone_bench.py report <run dir>              the summary again (also from any saved game log)

The instance: --instance <id>, or SOD_INSTANCE, or the newest one with --title-id (default
State of Decay's 584111E8). Everything lands in analysis/bench/<label>-<time>/: samples.csv, the
game's log, screenshots and summary.md.

Metrics, for RENDERER_BASELINE.md:
- fps every 2 s and the per-frame counters from the log's "GPU stats" lines (DAO_GPU_STATS=1 in the
  instance's env.txt; `prep` writes it). Without them, fps per 600 swaps from the VdSwap lines.
- GPU busy % and clock (kgsl). At the game's cap fps can't rise, so busy × clock (the GPU's work
  rate) is what shows a gain; the uncapped pass reads the headroom directly (SoD:
  --env SOD_FRAME_RATE=60).
- The time until the GPU's clock limit first falls: thermal_pwrlevel, as soon as it leaves the
  level it had in the first 30 s; failing that, the first sustained drop of the busiest clock.
- Skin temperature, thermal status, and the busiest threads.
- Power: the battery's current and voltage, as far as they are readable. With USB charging they
  don't show what the game draws, so they are recorded but not summarized.
"""
import argparse
import csv
import datetime
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import phone_preset  # noqa: E402  (adb(), INSTANCES)

KGSL = "/sys/class/kgsl/kgsl-3d0"
BENCH = ROOT / "analysis" / "bench"
SAVES = BENCH / "saves"
SOD_TITLE_ID = "584111E8"
LOG_NAMES = ("sod.log", "dao.log")

STATS = re.compile(
    r"\[\s*([\d.]+)\].*GPU stats: (\d+) frames in ([\d.]+) s, per frame: draws (\d+), passes (\d+), "
    r"resolves ([\d.]+) \(([\d.]+) MP, ([\d.]+) with clear\), transfers ([\d.]+), "
    r"texture loads ([\d.]+) \(([\d.]+) MB, ([\d.]+) of GPU-written memory\), uploads ([\d.]+) \(([\d.]+) MB\), "
    r"readbacks ([\d.]+) \(([\d.]+) MB\), submissions ([\d.]+), CPU waited for the GPU ([\d.]+) ms")
STAT_FIELDS = ("t", "frames", "seconds", "draws", "passes", "resolves", "resolve_mp", "resolve_clears", "transfers",
               "texture_loads", "texture_mb", "gpu_written_loads", "uploads", "upload_mb", "readbacks", "readback_mb",
               "submissions", "cpu_wait_ms")
SWAP = re.compile(r"\[\s*([\d.]+)\].*VdSwap #(\d+)")


def adb(*args):
    return phone_preset.adb(*args)


def shell(command):
    try:
        return adb("shell", command).strip()
    except subprocess.CalledProcessError:
        return ""


def instance_path(args):
    """The instance's folder on the phone, and its title ID."""
    if args.instance:
        name = args.instance
    else:
        name = None
        for candidate in adb("shell", f"ls -t {phone_preset.INSTANCES}").split():
            if args.title_id in shell(f"cat {phone_preset.INSTANCES}/{candidate}/instance.json"):
                name = candidate
                break
        if not name:
            sys.exit(f"No instance with title {args.title_id} on the phone")
    return f"{phone_preset.INSTANCES}/{name}"


def saves_path(path, title_id):
    return f"{path}/userdata/content/{title_id}/00000001"


def freeze(args):
    path = instance_path(args)
    out = SAVES / args.name
    out.mkdir(parents=True, exist_ok=True)
    adb("pull", saves_path(path, args.title_id) + "/.", str(out))
    print(f"Frozen {saves_path(path, args.title_id)} -> {out}")


def prep(args):
    path = instance_path(args)
    if args.save:
        source = SAVES / args.save
        if not source.is_dir():
            sys.exit(f"No frozen save {source}")
        target = saves_path(path, args.title_id)
        backup = SAVES / f"_before-{datetime.datetime.now():%Y%m%d-%H%M%S}"
        backup.mkdir(parents=True, exist_ok=True)
        adb("pull", target + "/.", str(backup))
        for item in source.iterdir():
            adb("push", str(item), target + "/")
        print(f"Restored {source} (the saves there before: {backup})")
    env = ["DAO_GPU_STATS=1"] + list(args.env or [])
    env_file = BENCH / "env.txt"
    BENCH.mkdir(parents=True, exist_ok=True)
    env_file.write_text("\n".join(env) + "\n", encoding="utf-8", newline="\n")
    adb("push", str(env_file), f"{path}/userdata/env.txt")
    print("env.txt:", ", ".join(env))
    if args.preset == "none":
        shell(f"rm -f {path}/game/systemoverride.cfg")
    elif args.preset:
        adb("push", str(ROOT / "presets" / f"{args.preset}.cfg"), f"{path}/game/systemoverride.cfg")
    if args.preset:
        print("preset:", args.preset)
    print("Now she starts the game (it reads these at boot).")


def pull_log(path, out):
    for name in LOG_NAMES:
        try:
            adb("pull", f"{path}/userdata/{name}", str(out / "game.log"))
            return True
        except subprocess.CalledProcessError:
            continue
    print("No game log pulled (is the phone still connected?)")
    return False


def record(args):
    path = instance_path(args)
    out = BENCH / f"{args.label}-{datetime.datetime.now():%Y%m%d-%H%M%S}"
    out.mkdir(parents=True, exist_ok=True)
    (out / "settings.txt").write_text(
        f"instance {path}\nconfig.toml:\n{shell(f'cat {path}/userdata/config.toml')}\n"
        f"env.txt:\n{shell(f'cat {path}/userdata/env.txt')}\n"
        f"systemoverride.cfg:\n{shell(f'cat {path}/game/systemoverride.cfg')}\n", encoding="utf-8")
    start = time.time()
    next_shot = 0.0
    fields = ["t", "busy", "mhz", "pwrlevel", "thermal", "skin", "battery_ua", "battery_uv", "threads"]
    with open(out / "samples.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(fields)
        while time.time() - start < args.seconds:
            t = time.time() - start
            pid = shell("pidof com.daodroid")
            gpu = shell(f"cat {KGSL}/gpu_busy_percentage {KGSL}/devfreq/cur_freq {KGSL}/thermal_pwrlevel 2>/dev/null").split()
            threads = shell(f"top -H -b -n 1 -p {pid} | grep com.daodroid | head -6") if pid.isdigit() else ""
            busiest = " | ".join(f"{w[8]}% {' '.join(w[11:-1])}" for w in (l.split() for l in threads.splitlines()) if len(w) > 12)
            thermal = shell("dumpsys thermalservice | grep -m1 'Thermal Status'").replace("Thermal Status: ", "")
            skin = re.search(r"mValue=([\d.]+), mType=3", shell("dumpsys thermalservice | grep -m1 'mType=3'"))
            battery = shell("cat /sys/class/power_supply/battery/current_now /sys/class/power_supply/battery/voltage_now 2>/dev/null").split()
            busy = gpu[0].rstrip("%") if gpu else ""
            mhz = int(gpu[2]) // 1000000 if len(gpu) > 2 and gpu[2].isdigit() else ""
            level = gpu[3] if len(gpu) > 3 else ""
            writer.writerow([round(t), busy, mhz, level, thermal, skin.group(1) if skin else "",
                             battery[0] if battery else "", battery[1] if len(battery) > 1 else "", busiest])
            f.flush()
            if t >= next_shot:
                next_shot += args.shot_every
                shot = f"/sdcard/Download/bench_{int(t)}.png"
                shell(f"screencap -p {shot}")
                try:
                    adb("pull", shot, str(out / f"shot_{int(t):04d}.png"))
                finally:
                    shell(f"rm -f {shot}")
            time.sleep(max(0.0, args.interval - (time.time() - start - t)))
    pull_log(path, out)
    print(summarize(out, args.seconds))


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def clock_fall(samples):
    """Seconds until the GPU's clock limit first falls, or None."""
    levels = [(s["t"], s["pwrlevel"]) for s in samples if s["pwrlevel"] is not None]
    if levels:
        early = [l for t, l in levels if t <= 30] or [levels[0][1]]
        for t, level in levels:
            if t > 30 and level > max(early):  # a higher level number is a lower clock
                return t
        return None
    # No thermal_pwrlevel: the GPU at full load, three samples in a row, at under 85% of the
    # highest clock it has reached so far.
    top = 0
    run = 0
    for s in samples:
        if not s["mhz"] or s["busy"] is None:
            continue
        top = max(top, s["mhz"]) if s["busy"] >= 70 else top
        run = run + 1 if s["busy"] >= 90 and s["mhz"] < top * 0.85 else 0
        if run == 3:
            return s["t"]
    return None


def summarize(out, seconds=None):
    out = Path(out)
    samples = []
    if (out / "samples.csv").exists():
        with open(out / "samples.csv", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                samples.append({k: number(row.get(k)) for k in ("t", "busy", "mhz", "pwrlevel", "skin")})
    text = (out / "game.log").read_text(encoding="utf-8", errors="replace") if (out / "game.log").exists() else ""
    stats = [dict(zip(STAT_FIELDS, map(float, m.groups()))) for m in STATS.finditer(text)]
    swaps = [(float(m.group(1)), int(m.group(2))) for m in SWAP.finditer(text)]
    if seconds and stats:
        end = stats[-1]["t"]
        stats = [s for s in stats if s["t"] > end - seconds]
    # The loads and the menu aren't the scene: from the first window with real work.
    scene = [s for s in stats if s["draws"] >= 100]
    lines = [f"# {out.name}", ""]
    if scene:
        fps = sorted(s["frames"] / s["seconds"] for s in scene)
        mean = statistics.mean
        lines += [
            f"- fps: mean {mean(fps):.1f}, median {statistics.median(fps):.1f}, worst 2 s {fps[0]:.1f}, "
            f"5% low {fps[max(0, len(fps) // 20 - 1)]:.1f} ({len(fps)} windows of 2 s)",
            f"- per frame: draws {mean(s['draws'] for s in scene):.0f} ({min(s['draws'] for s in scene):.0f}–"
            f"{max(s['draws'] for s in scene):.0f}), passes {mean(s['passes'] for s in scene):.0f}, "
            f"resolves {mean(s['resolves'] for s in scene):.1f} ({mean(s['resolve_mp'] for s in scene):.1f} MP, "
            f"{mean(s['resolve_clears'] for s in scene):.1f} clears), transfers {mean(s['transfers'] for s in scene):.1f}",
            f"- texture loads {mean(s['texture_loads'] for s in scene):.1f} ({mean(s['texture_mb'] for s in scene):.1f} MB, "
            f"{mean(s['gpu_written_loads'] for s in scene):.1f} GPU-written), uploads {mean(s['uploads'] for s in scene):.1f} "
            f"({mean(s['upload_mb'] for s in scene):.2f} MB), submissions {mean(s['submissions'] for s in scene):.1f}, "
            f"CPU wait {mean(s['cpu_wait_ms'] for s in scene):.2f} ms",
        ]
        # Heavy frames: fps against draws, to compare runs whose scenes differ.
        heavy = sorted(scene, key=lambda s: s["draws"])[-max(1, len(scene) // 5):]
        lines.append(f"- heaviest fifth: draws {mean(s['draws'] for s in heavy):.0f}, "
                     f"fps {mean(s['frames'] / s['seconds'] for s in heavy):.1f}")
    elif len(swaps) > 1:
        rates = [(b[1] - a[1]) / (b[0] - a[0]) for a, b in zip(swaps, swaps[1:])]
        lines.append(f"- fps per 600 swaps (no GPU stats lines): mean {statistics.mean(rates):.1f}, worst {min(rates):.1f}")
    active = [s for s in samples if s["busy"] is not None and s["mhz"]]
    if active:
        work = statistics.mean(s["busy"] * s["mhz"] / 100 for s in active)
        lines += [
            f"- GPU: busy {statistics.mean(s['busy'] for s in active):.1f}%, clock "
            f"{min(s['mhz'] for s in active):.0f}–{max(s['mhz'] for s in active):.0f} MHz, "
            f"**busy × clock {work:.0f} MHz** ({len(active)} samples)",
        ]
        fall = clock_fall(samples)
        lines.append(f"- clock limit first falls: {f'{fall:.0f} s' if fall is not None else 'not in this run'}")
        skins = [s["skin"] for s in samples if s["skin"]]
        if skins:
            lines.append(f"- skin {skins[0]:.1f} → {skins[-1]:.1f} °C")
    report = "\n".join(lines) + "\n"
    (out / "summary.md").write_text(report, encoding="utf-8")
    return report


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--instance")
    parser.add_argument("--title-id", default=SOD_TITLE_ID)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("freeze")
    p.add_argument("name")
    p = sub.add_parser("prep")
    p.add_argument("--save")
    p.add_argument("--env", action="append")
    p.add_argument("--preset")
    p = sub.add_parser("record")
    p.add_argument("label")
    p.add_argument("--seconds", type=int, default=300)
    p.add_argument("--interval", type=float, default=3)
    p.add_argument("--shot-every", type=int, default=60)
    p = sub.add_parser("report")
    p.add_argument("run")
    args = parser.parse_args()
    if args.command == "report":
        print(summarize(args.run))
    else:
        {"freeze": freeze, "prep": prep, "record": record}[args.command](args)


if __name__ == "__main__":
    main()
