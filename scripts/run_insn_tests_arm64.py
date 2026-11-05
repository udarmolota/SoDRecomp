"""Runs the XenonRecomp instruction unit checks (tests/insn) on an ARM64 Android phone.

Builds tests/insn/checks.cpp + the generated tests/insn/gen/*.cpp (run_insn_tests.py makes
gen/) into one standalone executable with the Android NDK's clang (aarch64-linux-android30,
static libc++), the generated code with DAODroid's flags for it (android/ppc-probe:
-fno-strict-aliasing -ffp-model=strict -ffp-contract=off). Then it pushes the binary to
/data/local/tmp/sod_insn_tests/ over adb, runs it, saves the output to
analysis/insn_tests_arm64.txt and removes that folder from the phone again.

No APK, no Gradle.

usage: run_insn_tests_arm64.py
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests' / 'insn'
BUILD = ROOT / 'build' / 'insn_tests_arm64'
XENONRECOMP = ROOT / 'XenonRecomp'
SDK = Path(os.environ.get('LOCALAPPDATA', '')) / 'Android' / 'Sdk'
NDK = SDK / 'ndk' / '27.3.13750724'  # DAODroid's pinned NDK
CLANG = NDK / 'toolchains' / 'llvm' / 'prebuilt' / 'windows-x86_64' / 'bin' / 'clang++.exe'
ADB = SDK / 'platform-tools' / 'adb.exe'
REMOTE = '/data/local/tmp/sod_insn_tests'

COMMON = ['--target=aarch64-linux-android30', '-std=c++20', '-O2',
          '-I', XENONRECOMP / 'XenonUtils', '-I', XENONRECOMP / 'thirdparty' / 'simde',
          '-include', TESTS / 'test_hooks.h',
          '-Wno-unused-label', '-Wno-unused-variable', '-Wno-unused-but-set-variable']
GENERATED_FLAGS = ['-fno-strict-aliasing', '-ffp-model=strict', '-ffp-contract=off']


def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    generated = sorted(p for p in (TESTS / 'gen').glob('*.cpp') if p.name != 'main.cpp')
    if not generated:
        sys.exit('tests/insn/gen is empty: run scripts/run_insn_tests.py first')
    objects = []
    for src in generated + [TESTS / 'checks.cpp']:
        obj = BUILD / (src.stem + '.o')
        flags = GENERATED_FLAGS if src.parent.name == 'gen' else []
        run([CLANG, *COMMON, *flags, '-c', src, '-o', obj])
        objects.append(obj)
    exe = BUILD / 'insn_tests_arm64'
    run([CLANG, '--target=aarch64-linux-android30', '-static-libstdc++', *objects, '-o', exe])
    print(f'built {exe} ({exe.stat().st_size} bytes)')

    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    adb = lambda *a, **kw: subprocess.run([str(ADB), *a], env=env, **kw)  # noqa: E731
    adb('shell', f'mkdir -p {REMOTE}', check=True)
    try:
        adb('push', str(exe), f'{REMOTE}/insn_tests_arm64', check=True, stdout=subprocess.DEVNULL)
        adb('shell', f'chmod +x {REMOTE}/insn_tests_arm64', check=True)
        model = adb('shell', 'getprop ro.product.model; getprop ro.board.platform; uname -m',
                    capture_output=True, text=True).stdout.split()
        result = adb('shell', f'cd {REMOTE} && ./insn_tests_arm64 > out.txt 2>&1; echo exit=$?',
                     capture_output=True, text=True)
        out = ROOT / 'analysis' / 'insn_tests_arm64.txt'
        adb('pull', f'{REMOTE}/out.txt', str(out), check=True, stdout=subprocess.DEVNULL)
    finally:
        adb('shell', f'rm -rf {REMOTE}')
    left = adb('shell', f'ls {REMOTE} 2>/dev/null; echo done', capture_output=True, text=True).stdout.strip()
    text = out.read_text()
    out.write_text(f'# device: {" ".join(model)}\n' + text)
    print(text, end='')
    print(f'device: {" ".join(model)}; {result.stdout.strip()}; phone folder removed: {left == "done"}')
    sys.exit(0 if result.stdout.strip() == 'exit=0' else 1)


if __name__ == '__main__':
    main()
