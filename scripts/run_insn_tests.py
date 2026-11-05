"""Runs the unit checks for SoD's XenonRecomp additions, end to end:

1. scripts/make_insn_tests.py writes one ELF object per instruction (tests/insn/obj);
2. the patched XenonRecomp translates them in its test mode (tests/insn/gen);
3. each generated function's first comment must be the expected disassembly
   (the encoding hit the intended instruction and operands);
4. tests/insn is built with DAORecomp's pinned toolchain (read-only use) in two
   variants, x86-64-v3 and baseline x86-64, and both are run.

usage: run_insn_tests.py        (from anywhere; paths are relative to this repo)
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests' / 'insn'
BUILD = ROOT / 'build' / 'insn_tests'
XENONRECOMP = ROOT / 'build' / 'xenonrecomp' / 'XenonRecomp' / 'XenonRecomp.exe'
# DAORecomp (XDroid360's recomp submodule): pinned CMake, Ninja, LLVM, xwin sysroot. XDROID360_ROOT
# is an XDroid360 checkout (default: the author's layout, one level above this one's parent).
DAO_TOOLS = Path(os.environ.get('XDROID360_ROOT', ROOT.parent.parent / 'DAO')) / 'recomp'


def run(args, **kw):
    print('>', ' '.join(str(a) for a in args))
    return subprocess.run([str(a) for a in args], check=True, **kw)


def main():
    run([sys.executable, '-I', ROOT / 'scripts' / 'make_insn_tests.py', TESTS])

    gen = TESTS / 'gen'
    shutil.rmtree(gen, ignore_errors=True)
    gen.mkdir()
    out = subprocess.run([str(XENONRECOMP), str(TESTS / 'obj'), str(gen)], capture_output=True, text=True)
    noise = [l for l in out.stdout.splitlines() if l and not l.startswith(('Unable to locate disassembly file', 'Unable to locate source file'))]
    if out.returncode != 0 or noise:
        sys.exit('XenonRecomp test mode:\n' + '\n'.join(noise) + f'\nexit {out.returncode}')

    bad = 0
    for line in (TESTS / 'expect.txt').read_text().splitlines():
        name, expected = line.split('\t')
        text = (gen / f'{name}.cpp').read_text()
        first = re.search(r'^\t// (.*?)\s*$', text, re.M)
        if not first or first.group(1) != expected:
            print(f'DECODE {name}: got {first.group(1) if first else None!r}, expected {expected!r}')
            bad += 1
        if 'debugtrap' in text.split('#define __builtin_debugtrap()', 1)[1]:
            print(f'TRAP {name}: the translation contains a debug trap')
            bad += 1
    if bad:
        sys.exit(f'{bad} decode problems')
    print(f'decode: all {len((TESTS / "expect.txt").read_text().splitlines())} tests disassemble as intended')

    run([DAO_TOOLS / 'tools' / 'cmake' / 'bin' / 'cmake.exe', '-G', 'Ninja', '-S', TESTS, '-B', BUILD, '-Wno-dev',
         f'-DCMAKE_MAKE_PROGRAM={DAO_TOOLS / "tools" / "ninja" / "ninja.exe"}', '-DCMAKE_BUILD_TYPE=Release',
         f'-DCMAKE_TOOLCHAIN_FILE={DAO_TOOLS / "cmake" / "clang-cl-xwin.cmake"}',
         f'-DDAO_LLVM_BIN={DAO_TOOLS / "tools" / "llvm" / "bin"}', f'-DDAO_WINSYSROOT={DAO_TOOLS / "tools" / "winsysroot"}'],
        stdout=subprocess.DEVNULL)
    run([DAO_TOOLS / 'tools' / 'ninja' / 'ninja.exe', '-C', BUILD])

    failed = 0
    for variant in ('v3', 'baseline'):
        exe = BUILD / f'insn_tests_{variant}.exe'
        print(f'== {variant}')
        failed |= subprocess.run([str(exe)]).returncode
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
