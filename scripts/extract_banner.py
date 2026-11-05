"""Extracts State of Decay's package images from its STFS (LIVE/PIRS/CON) header, for a launcher
banner (the drafted package-format-2 `banner` entry; nothing uses it yet).

    extract_banner.py <STFS package> <out dir>

Writes thumbnail.png (the package's thumbnail, header offset 0x171A, size at 0x1712) and
title_thumbnail.png (0x571A, size at 0x1716), and prints their sizes in pixels. Both are small
(64x64 in the XBLA header). For a wider banner, a capture of the game's own title screen is the
alternative (screenshots/phase4_menu_d3d12.png).
"""
import struct
import sys
from pathlib import Path

THUMBNAIL_SIZE, TITLE_THUMBNAIL_SIZE = 0x1712, 0x1716
THUMBNAIL, TITLE_THUMBNAIL = 0x171A, 0x571A
PNG = b"\x89PNG\r\n\x1a\n"


def png_size(data):
    """(width, height) from a PNG's IHDR."""
    return struct.unpack(">II", data[16:24])


def main():
    package, out = Path(sys.argv[1]), Path(sys.argv[2])
    header = package.read_bytes()[:0xA000] if package.stat().st_size < 0xA000 else open(package, "rb").read(0xA000)
    if header[:4] not in (b"LIVE", b"PIRS", b"CON "):
        sys.exit(f"{package} isn't an STFS package")
    out.mkdir(parents=True, exist_ok=True)
    sizes = struct.unpack(">II", header[THUMBNAIL_SIZE:THUMBNAIL_SIZE + 8])
    for name, offset, size in (("thumbnail.png", THUMBNAIL, sizes[0]), ("title_thumbnail.png", TITLE_THUMBNAIL, sizes[1])):
        data = header[offset:offset + size]
        if not size or data[:8] != PNG:
            print(f"{name}: none")
            continue
        (out / name).write_bytes(data)
        print(f"{name}: {size} bytes, {png_size(data)[0]}x{png_size(data)[1]}")


if __name__ == "__main__":
    main()
