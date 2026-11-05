"""SoD's stand-in for DAORecomp's scripts/ppc.py: helpers over the dumped image.

The finders next to this file (find_switches.py, fix_functions.py, find_helpers.py) come
from DAORecomp's recomp/scripts and import BASE, IMG, TEXT, BINK, VARIANT, u32,
code_words, branch_target, pdata from here. The image is analysis/<SOD_VARIANT>/
(default base: image.bin + info.txt written by xexdump). SoD has no BINK section; its
second code section, PSFD00, takes that slot. capstone is optional (only dis() uses it).
"""
import os
import re
import struct

VARIANT = 'sod_' + os.environ.get('SOD_VARIANT', 'base')  # never matches DAO's UNBOUNDED keys
ANALYSIS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis',
                        os.environ.get('SOD_VARIANT', 'base')) + os.sep
BASE = 0x82000000
IMG = open(ANALYSIS + 'image.bin', 'rb').read()


def _sections():
    out = {}
    for m in re.finditer(r'^section (\S+) \w+ (0x[0-9A-F]+) (0x[0-9A-F]+)', open(ANALYSIS + 'info.txt').read(), re.M):
        out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


SECTIONS = _sections()
TEXT = (SECTIONS['.text'][0], SECTIONS['.text'][0] + SECTIONS['.text'][1])
BINK = (SECTIONS['PSFD00'][0], SECTIONS['PSFD00'][0] + SECTIONS['PSFD00'][1])


def u32(a):
    return struct.unpack_from('>I', IMG, a - BASE)[0]


def imports():
    out = {}
    for m in re.finditer(r'^thunk (0x[0-9A-F]+) (?:__imp__)+(\w+)', open(ANALYSIS + 'info.txt').read(), re.M):
        out[m.group(2)] = int(m.group(1), 16)
    return out


def code_words():
    for lo, hi in (TEXT, BINK):
        for a in range(lo, hi, 4):
            yield a, u32(a)


def branch_target(a, w):
    """Target of an I-form b/bl at address a, or None."""
    if w >> 26 != 18:
        return None
    li = w & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    return (li if w & 2 else a + li) & 0xFFFFFFFF


def callers(target):
    return [a for a, w in code_words() if (w & 3) == 1 and branch_target(a, w) == target]


def dis(a, n=16):
    import capstone
    cs = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    code = IMG[a - BASE:a - BASE + 4 * n]
    lines = []
    for i in range(n):
        insn = next(cs.disasm(code[4 * i:4 * i + 4], a + 4 * i), None)
        word = code[4 * i:4 * i + 4].hex()
        lines.append(f"{a + 4*i:08X}  {word}  " + (f"{insn.mnemonic} {insn.op_str}" if insn else '???'))
    return '\n'.join(lines)


def pdata():
    """[(start, length_bytes)] from .pdata (start, packed: prolog:8 len:22 flags:2)."""
    lo, size = SECTIONS['.pdata']
    out = []
    for o in range(lo, lo + size, 8):
        start, packed = struct.unpack_from('>II', IMG, o - BASE)
        if start == 0:
            continue
        out.append((start, ((packed >> 8) & 0x3FFFFF) * 4))
    return out


def func_of(addr, pd=None):
    for s, l in (pd or pdata()):
        if s <= addr < s + l:
            return s, l
    return None
