"""Writes the per-instruction unit-check inputs for the SoD XenonRecomp additions.

Each test is one tiny big-endian PowerPC ELF object, tests/insn/obj/<name>.o, whose
.text holds the instruction under test followed by `blr` at 0x10000. XenonRecomp's
test mode (`XenonRecomp <obj dir> <out dir>`) turns each object into <name>.cpp with a
function <name>_10000; tests/insn/checks.cpp runs those functions against reference
implementations. tests/insn/expect.txt lists the disassembly each object must produce,
which check_insn_tests.py compares with the comments in the generated code.

usage: make_insn_tests.py <tests/insn dir>
"""
import os
import struct
import sys

BLR = 0x4E800020


def X(op, xo, rt, ra, rb, rc=0):
    return (op << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc


def XO(op, xo, rt, ra, rb, rc=0):
    return (op << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc


def A(op, xo, frt, fra, frc, frb):
    return (op << 26) | (frt << 21) | (fra << 16) | (frb << 11) | (frc << 6) | (xo << 1)


def VX(xo, vd, va, vb, rc=0):
    return (4 << 26) | (vd << 21) | (va << 16) | (vb << 11) | (rc << 10) | xo


# VMX128 split register fields (the inverse of the disassembler's extract_v*128).
def vd128(v):
    return ((v & 0x1F) << 21) | ((v & 0x60) >> 3)


def va128(v):
    return ((v & 0x1F) << 16) | (v & 0x20) | ((v & 0x40) << 4)


def vb128(v):
    return ((v & 0x1F) << 11) | ((v & 0x60) >> 5)


def VX128(op, xop, vd, va, vb):
    return (op << 26) | (xop & 0x3D0) | vd128(vd) | va128(va) | vb128(vb)


def rlwimi_dot(ra, rs, sh, mb, me):
    return (20 << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | 1


def rldicl_dot(ra, rs, sh, mb):
    return (30 << 26) | (rs << 21) | (ra << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((mb >> 5) << 5) | ((sh >> 5) << 1) | 1


def vupkd3d128(vd, vb, uimm):
    return (6 << 26) | 0x7F0 | vd128(vd) | vb128(vb) | (uimm << 16)


def vpkd3d128(vd, vb, kind, pack, shift):
    return (6 << 26) | 0x610 | vd128(vd) | vb128(vb) | (kind << 18) | (pack << 16) | (shift << 6)


# name: (instruction words, expected disassembly of the first one)
TESTS = {
    'ldux':        ([X(31, 53, 3, 4, 5)], 'ldux r3,r4,r5'),
    'stfdux':      ([X(31, 759, 1, 4, 5)], 'stfdux f1,r4,r5'),
    'lhbrx':       ([X(31, 790, 3, 4, 5)], 'lhbrx r3,r4,r5'),
    'lhbrx_ra0':   ([X(31, 790, 3, 0, 5)], 'lhbrx r3,0,r5'),
    'mulhd':       ([XO(31, 73, 3, 4, 5)], 'mulhd r3,r4,r5'),
    'mulhdu':      ([XO(31, 9, 3, 4, 5)], 'mulhdu r3,r4,r5'),
    'fctiw':       ([X(63, 14, 1, 0, 2)], 'fctiw f1,f2'),
    'fctiwz':      ([X(63, 15, 1, 0, 2)], 'fctiwz f1,f2'),
    'fctid':       ([X(63, 814, 1, 0, 2)], 'fctid f1,f2'),
    'fctidz':      ([X(63, 815, 1, 0, 2)], 'fctidz f1,f2'),
    'fnmadd':      ([A(63, 31, 1, 2, 3, 4)], 'fnmadd f1,f2,f3,f4'),
    'rlwimi_dot':  ([rlwimi_dot(30, 11, 16, 0, 15)], 'rlwimi. r30,r11,16,0,15'),
    'rldicl_dot':  ([rldicl_dot(11, 11, 32, 32)], 'rldicl. r11,r11,32,32'),
    'blrl':        ([0x4E800021], 'blrl'),
    'vminsw':      ([VX(898, 3, 4, 5)], 'vminsw v3,v4,v5'),
    'vsubuwm':     ([VX(1152, 3, 4, 5)], 'vsubuwm v3,v4,v5'),
    'vpkshss':     ([VX(398, 3, 4, 5)], 'vpkshss v3,v4,v5'),
    'vpkshss128':  ([VX128(5, 512, 70, 100, 3)], 'vpkshss128 v70,v100,v3'),
    'vcmpgtuw':    ([VX(646, 3, 4, 5)], 'vcmpgtuw v3,v4,v5'),
    'vcmpgtuw_dot': ([VX(646, 3, 4, 5, 1)], 'vcmpgtuw. v3,v4,v5'),
    'vcmpgtsw':    ([VX(902, 3, 4, 5)], 'vcmpgtsw v3,v4,v5'),
    'vcmpgtsw_dot': ([VX(902, 3, 4, 5, 1)], 'vcmpgtsw. v3,v4,v5'),
    'vavgsw':      ([VX(1410, 3, 4, 5)], 'vavgsw v3,v4,v5'),
    'vslh':        ([VX(324, 3, 4, 5)], 'vslh v3,v4,v5'),
    'vsrh':        ([VX(580, 3, 4, 5)], 'vsrh v3,v4,v5'),
    'vsl':         ([VX(452, 3, 4, 5)], 'vsl v3,v4,v5'),
    'vslo':        ([VX(1036, 3, 4, 5)], 'vslo v3,v4,v5'),
    'vslo128':     ([VX128(5, 912, 54, 12, 60)], 'vslo128 v54,v12,v60'),
    'vrlw128':     ([VX128(6, 80, 11, 60, 58)], 'vrlw128 v11,v60,v58'),
    'stvebx':      ([X(31, 135, 3, 4, 5)], 'stvebx v3,r4,r5'),
    'vupkd3d128_f16x4': ([vupkd3d128(63, 0, 20)], 'vupkd3d128 v63,v0,20'),
    'vupkd3d128_f16x2': ([vupkd3d128(63, 62, 12)], 'vupkd3d128 v63,v62,12'),
    'vpkd3d128_f16x2':  ([vpkd3d128(64, 63, 3, 1, 3)], 'vpkd3d128 v64,v63,3,1,3'),
    # The word SoD has at 0x823037B0, which the unpatched disassembler read as mullhwu.
    'vsldoi128_misdecode': ([0x13C0F311], 'vsldoi128 v30,v0,v62,12'),
}


def elf(words):
    """A minimal ELF32 big-endian PowerPC relocatable: .text at 0x10000 + .shstrtab."""
    text = b''.join(struct.pack('>I', w) for w in words + [BLR])
    shstr = b'\0.text\0.shstrtab\0'
    ehsize, shentsize = 52, 40
    text_off = ehsize
    shstr_off = text_off + len(text)
    sh_off = (shstr_off + len(shstr) + 3) & ~3
    ident = b'\x7fELF' + bytes([1, 2, 1]) + bytes(9)
    header = ident + struct.pack('>HHIIIIIHHHHHH', 1, 20, 1, 0, 0, sh_off, 0, ehsize, 0, 0, shentsize, 3, 2)
    null = bytes(shentsize)
    sh_text = struct.pack('>IIIIIIIIII', 1, 1, 0x6, 0x10000, text_off, len(text), 0, 0, 4, 0)  # PROGBITS, ALLOC|EXECINSTR
    sh_str = struct.pack('>IIIIIIIIII', 7, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)
    body = header + text + shstr
    body += bytes(sh_off - len(body))
    return body + null + sh_text + sh_str


def main():
    out = sys.argv[1]
    obj = os.path.join(out, 'obj')
    os.makedirs(obj, exist_ok=True)
    for f in os.listdir(obj):
        os.remove(os.path.join(obj, f))
    with open(os.path.join(out, 'expect.txt'), 'w') as expect:
        for name, (words, dis) in TESTS.items():
            open(os.path.join(obj, name + '.o'), 'wb').write(elf(words))
            expect.write(f'{name}\t{dis}\n')
    print(f'{len(TESTS)} test objects in {obj}')


if __name__ == '__main__':
    main()
