"""Runs SoDRecomp.exe for a while and summarizes the log (phase 3 bring-up).

usage: run_game.py [seconds] [KEY=VALUE ...]
Defaults: 60 seconds, SOD_GPU=null, SOD_AUDIO=null, SOD_DUMP_AFTER=<seconds - 5>.
The run's log is install/userdata/sod.log (copied to analysis/runs/<time>.log).
"""
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / 'build' / 'sodrecomp' / 'SoDRecomp' / 'SoDRecomp.exe'
GAME = ROOT / 'install' / 'game'
LOG = ROOT / 'install' / 'userdata' / 'sod.log'


def main():
    args = sys.argv[1:]
    seconds = int(args.pop(0)) if args and args[0].isdigit() else 60
    env = dict(os.environ, SOD_GPU='null', SOD_AUDIO='null', SOD_DUMP_AFTER=str(max(5, seconds - 5)))
    for a in args:
        k, _, v = a.partition('=')
        if v:
            env[k] = v
        else:
            env.pop(k, None)
    start = time.time()
    try:
        proc = subprocess.run([str(EXE), str(GAME), '--play'], env=env, timeout=seconds,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        outcome = f'exited with {proc.returncode:#x} after {time.time() - start:.1f} s'
    except subprocess.TimeoutExpired:
        outcome = f'still running after {seconds} s (killed)'
    runs = ROOT / 'analysis' / 'runs'
    runs.mkdir(parents=True, exist_ok=True)
    saved = runs / time.strftime('%Y%m%d-%H%M%S.log')
    shutil.copyfile(LOG, saved)
    text = LOG.read_text(encoding='utf-8', errors='replace').splitlines()
    print(f'{outcome}; {len(text)} log lines -> {saved.name}')
    norm = Counter()
    for line in text:
        m = re.match(r'^\[\s*[\d.]+\] ([WE]) +\d+ \| (.*)$', line)
        if m:
            msg = re.sub(r'\(([0-9A-F]{8}, )*[0-9A-F]{8}\)( lr [0-9A-F]{8})?', '(...)', m.group(2))
            norm[f'{m.group(1)} {msg[:150]}'] += 1
    for msg, n in norm.most_common(40):
        print(f'{n:6} {msg}')
    swaps = [l for l in text if 'VdSwap #' in l]
    print('VdSwap lines:', len(swaps), swaps[-1][:80] if swaps else '')
    print('--- last lines before the dump:')
    body = [l for l in text if '---- thread dump' not in l]
    cut = next((i for i, l in enumerate(text) if '---- thread dump' in l), len(text))
    for l in text[max(0, cut - 15):cut]:
        print(l[:200])


if __name__ == '__main__':
    main()
