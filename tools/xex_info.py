"""Read-only dump of an Xbox 360 XEX2 header (default.xex or a default.xexp patch).

Usage: python -I xex_info.py <file.xex>

Reports the optional headers: file format (encryption / compression), execution
info (title id, versions), image base, entry point, TLS, stack size, import
libraries (names, versions, record counts), static libraries (SDK components and
versions) and the original PE name. Nothing is decrypted.
"""
import struct
import sys

KEYS = {
    0x000002FF: "RESOURCE_INFO", 0x000003FF: "FILE_FORMAT_INFO", 0x000005FF: "DELTA_PATCH_DESCRIPTOR",
    0x000080FF: "BOUNDING_PATH", 0x00010001: "ORIGINAL_BASE_ADDRESS", 0x00010100: "ENTRY_POINT",
    0x00010201: "IMAGE_BASE_ADDRESS", 0x000103FF: "IMPORT_LIBRARIES", 0x00018002: "CHECKSUM_TIMESTAMP",
    0x00018102: "ENABLED_FOR_CALLCAP", 0x00018200: "ENABLED_FOR_FASTCAP", 0x000183FF: "ORIGINAL_PE_NAME",
    0x000200FF: "STATIC_LIBRARIES", 0x00020104: "TLS_INFO", 0x00020200: "DEFAULT_STACK_SIZE",
    0x00020301: "DEFAULT_FILESYSTEM_CACHE_SIZE", 0x00020401: "DEFAULT_HEAP_SIZE",
    0x00028002: "PAGE_HEAP_SIZE_AND_FLAGS", 0x00030000: "SYSTEM_FLAGS", 0x00040006: "EXECUTION_INFO",
    0x00040201: "TITLE_WORKSPACE_SIZE", 0x00040310: "GAME_RATINGS", 0x00040404: "LAN_KEY",
    0x000405FF: "XBOX360_LOGO", 0x000406FF: "MULTIDISC_MEDIA_IDS", 0x000407FF: "ALTERNATE_TITLE_IDS",
    0x00040801: "ADDITIONAL_TITLE_MEMORY", 0x00E10402: "EXPORTS_BY_NAME",
}


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def ver(v):
    return f"{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}"


def main(path):
    b = open(path, "rb").read()
    if b[:4] != b"XEX2":
        sys.exit("not XEX2")
    module_flags, pe_off, _res, sec_off, count = struct.unpack_from(">IIIII", b, 4)
    flags = [n for bit, n in [(1, "title"), (2, "exports_to_title"), (4, "system_debugger"), (8, "dll"),
                              (0x10, "module_patch"), (0x20, "patch_full"), (0x40, "patch_delta"),
                              (0x80, "user_mode")] if module_flags & bit]
    print(f"file: {path}\nsize: {len(b):,}  module flags 0x{module_flags:X} {flags}  pe data offset 0x{pe_off:X}  security info 0x{sec_off:X}")
    if sec_off and sec_off + 8 < len(b):
        image_size = u32(b, sec_off + 4)
        print(f"image size (uncompressed PE): {image_size:,}")
    off = 0x18
    for _ in range(count):
        key, val = struct.unpack_from(">II", b, off)
        off += 8
        name = KEYS.get(key, f"0x{key:08X}")
        lo = key & 0xFF
        if lo in (0, 1):
            print(f"  {name}: 0x{val:08X}")
            data = None
        elif lo == 0xFF:
            size = u32(b, val)
            data = b[val:val + size]
            print(f"  {name}: {size} bytes at 0x{val:X}")
        else:
            data = b[val:val + lo * 4]
            print(f"  {name}: {lo * 4} bytes at 0x{val:X}")
        if data is None:
            continue
        if key == 0x000003FF:
            enc, comp = u16(data, 4), u16(data, 6)
            print(f"      encryption: {['none', 'normal (AES)'][enc] if enc < 2 else enc}   compression: {['none', 'basic', 'normal (LZX)', 'delta'][comp] if comp < 4 else comp}")
        elif key == 0x00040006:
            media, version, base, title = struct.unpack_from(">IIII", data, 0)
            print(f"      media id {media:08X}  version {ver(version)}  base version {ver(base)}  title id {title:08X}  platform {data[16]} exec type {data[17]} disc {data[18]}/{data[19]}")
        elif key == 0x000183FF:
            print("      " + data[4:].split(b"\0")[0].decode("latin-1"))
        elif key == 0x000200FF:
            n = (len(data) - 4) // 16
            libs = []
            for i in range(n):
                e = data[4 + i * 16: 4 + (i + 1) * 16]
                nm = e[:8].split(b"\0")[0].decode("latin-1")
                maj, mnr, bld, qfe = struct.unpack_from(">HHHH", e, 8)
                libs.append(f"{nm} {maj}.{mnr}.{bld}.{qfe & 0xFF}")
            print("      " + ", ".join(libs))
        elif key == 0x000103FF:
            str_size, lib_count = u32(data, 4), u32(data, 8)
            names = [s.decode("latin-1") for s in data[12:12 + str_size].split(b"\0") if s]
            p = 12 + str_size
            for _ in range(lib_count):
                size = u32(data, p)
                version, min_version = u32(data, p + 0x1C), u32(data, p + 0x20)
                name_idx, rec_count = u16(data, p + 0x24), u16(data, p + 0x26)
                nm = names[name_idx] if name_idx < len(names) else f"#{name_idx}"
                print(f"      {nm}: version {ver(version)} (min {ver(min_version)}), {rec_count} import records (~{rec_count} thunk/variable entries)")
                p += size
        elif key == 0x00020104:
            slots, raw_addr, data_size, raw_size = struct.unpack_from(">IIII", data, 0)
            print(f"      TLS slots {slots}, raw data at 0x{raw_addr:08X}, data size {data_size}, raw size {raw_size}")
        elif key == 0x000005FF:
            print("      (delta patch descriptor present)")


if __name__ == "__main__":
    main(sys.argv[1])
