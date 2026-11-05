"""Minimal read-only reader for Xbox 360 STFS packages (CON / LIVE / PIRS).

Usage:
  python -I stfs.py info  <package>              # metadata + file listing
  python -I stfs.py extract <package> <out_dir>  # extract every file

Block addressing follows the published STFS layout (Velocity / py360):
hash tables are interleaved with data blocks, one level-0 table per 0xAA
blocks and one level-1 table per 0x70E4 blocks; "female" packages keep one
copy of each table, "male" packages two.
"""
import os
import struct
import sys

CONTENT_TYPES = {
    0x00000001: "Saved game", 0x00000002: "Marketplace content", 0x00000003: "Publisher",
    0x00007000: "Games on Demand", 0x000B0000: "Title update", 0x000D0000: "Arcade title",
    0x00080000: "Game demo", 0x00090000: "Video", 0x00004000: "Installed game",
}


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def le24(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


def be24(b, o):
    return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]


class Stfs:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        h = self.f.read(0xB000)
        self.h = h
        self.magic = h[0:4].decode("latin-1")
        if self.magic not in ("CON ", "LIVE", "PIRS"):
            raise ValueError(f"not an STFS package (magic {h[0:4]!r})")
        self.header_size = be32(h, 0x340)
        self.content_type = be32(h, 0x344)
        self.metadata_version = be32(h, 0x348)
        self.content_size = struct.unpack_from(">Q", h, 0x34C)[0]
        self.media_id = be32(h, 0x354)
        self.version = be32(h, 0x358)
        self.base_version = be32(h, 0x35C)
        self.title_id = be32(h, 0x360)
        self.descriptor_type = be32(h, 0x3A9 + 0xC) if False else h[0x3B5] if len(h) > 0x3B5 else 0
        vd = h[0x379:0x379 + 0x24]
        self.block_sep = vd[2]
        self.ft_block_count = struct.unpack_from("<H", vd, 3)[0]
        self.ft_block_num = le24(vd, 5)
        self.total_alloc = struct.unpack_from(">I", vd, 0x1C)[0]
        self.display_name = h[0x411:0x411 + 0x80].decode("utf-16-be", "replace").split("\x00")[0]
        self.title_name = h[0x1691:0x1691 + 0x80].decode("utf-16-be", "replace").split("\x00")[0]
        self.sex = (~self.block_sep) & 1  # 0 = female (one table), 1 = male (two)
        self.step0 = 0xAB if self.sex == 0 else 0xAC
        self.first_ht = (self.header_size + 0xFFF) & 0xFFFFF000
        self.files = self._read_file_table()

    def _backing_block(self, n):
        r = (((n + 0xAA) // 0xAA) << self.sex) + n
        if n < 0xAA:
            return r
        if n < 0x70E4:
            return r + (((n + 0x70E4) // 0x70E4) << self.sex)
        return (1 << self.sex) + r + (((n + 0x70E4) // 0x70E4) << self.sex)

    def _block_addr(self, n):
        return (self._backing_block(n) << 0xC) + self.first_ht

    def _level0_hash_addr(self, n):
        if n < 0xAA:
            num = 0
        else:
            num = (n // 0xAA) * self.step0
            num += ((n // 0x70E4) + 1) << self.sex
            if n // 0x70E4 != 0:
                num += 1 << self.sex
        return (num << 0xC) + self.first_ht + (n % 0xAA) * 0x18

    def _next_block(self, n):
        self.f.seek(self._level0_hash_addr(n))
        e = self.f.read(0x18)
        return be24(e, 0x15)

    def _read_block(self, n):
        self.f.seek(self._block_addr(n))
        return self.f.read(0x1000)

    def _read_file_table(self):
        raw = b""
        n = self.ft_block_num
        for i in range(self.ft_block_count):
            raw += self._read_block(n)
            n = self._next_block(n)
        files = []
        for i in range(0, len(raw), 0x40):
            e = raw[i:i + 0x40]
            if e[0] == 0:
                continue
            flags = e[0x28]
            nlen = flags & 0x3F
            name = e[0:nlen].decode("latin-1", "replace")
            files.append({
                "index": i // 0x40,
                "name": name,
                "dir": bool(flags & 0x80),
                "consecutive": bool(flags & 0x40),
                "blocks": le24(e, 0x29),
                "start": le24(e, 0x2F),
                "parent": struct.unpack_from(">h", e, 0x32)[0],
                "size": be32(e, 0x34),
            })
        return files

    def path_of(self, fe):
        parts = [fe["name"]]
        p = fe["parent"]
        byidx = {x["index"]: x for x in self.files}
        while p != -1 and p in byidx:
            parts.append(byidx[p]["name"])
            p = byidx[p]["parent"]
        return "/".join(reversed(parts))

    def read_file(self, fe):
        out = bytearray()
        n = fe["start"]
        remaining = fe["size"]
        for i in range(fe["blocks"]):
            data = self._read_block(n)
            out += data[:min(0x1000, remaining)]
            remaining -= 0x1000
            if remaining <= 0:
                break
            n = n + 1 if fe["consecutive"] else self._next_block(n)
        return bytes(out)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("info", "extract"):
        print(__doc__)
        sys.exit(1)
    p = Stfs(sys.argv[2])
    print(f"file:          {os.path.basename(p.path)}")
    print(f"magic:         {p.magic!r}")
    print(f"content type:  0x{p.content_type:08X} ({CONTENT_TYPES.get(p.content_type, '?')})")
    print(f"title id:      {p.title_id:08X}   media id: {p.media_id:08X}")
    print(f"version:       {p.version:08X}   base version: {p.base_version:08X}")
    print(f"display name:  {p.display_name}")
    print(f"title name:    {p.title_name}")
    print(f"header size:   0x{p.header_size:X}  block sep: {p.block_sep}  file table: block {p.ft_block_num} x{p.ft_block_count}")
    print(f"files:         {len(p.files)}")
    for fe in p.files:
        kind = "<dir>" if fe["dir"] else f"{fe['size']:>13,}"
        print(f"  {kind}  {p.path_of(fe)}")
    if sys.argv[1] == "extract":
        out = sys.argv[3]
        for fe in p.files:
            dst = os.path.join(out, p.path_of(fe))
            if fe["dir"]:
                os.makedirs(dst, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
            with open(dst, "wb") as o:
                o.write(p.read_file(fe))
        print("extracted to", out)


if __name__ == "__main__":
    main()
