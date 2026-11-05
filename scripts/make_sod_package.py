"""Makes a game package (.x360pkg, format 2; DAO's docs/PACKAGE_FORMAT.md) for State of Decay, for
the shared launcher (DAODroid) to host as an instance (phase 5, PHASE5_LOG.md).

    make_sod_package.py --game install/game --library build/android/libgame.so --out <dir>

Format 2 names the game itself (title_id, title), so the manifest carries State of Decay's own
values: title ID 584111E8 and the real SHA-1 of its default.xex. Format 1 (.daopkg, made by this
script before 2026-10-29) carried DAO's xex_sha1 pin instead, because that launcher accepted only
DAO's disc. The library checks its own default.xex at start (kernel/title_update.cpp) as on the PC.

Format 2's optional `options` (DAO's launcher, its A2) carry the game's choices for the launcher:
the frame rate (30 by default, or 60; written to the instance's config.toml [video] frame_rate)
and the graphics presets (presets/<id>.cfg, copied to game/systemoverride.cfg, which CryEngine
reads at boot). The runtime reads config.toml first and SOD_FRAME_RATE after it, so a
SOD_FRAME_RATE left in an instance's env.txt overrides the launcher's choice. With --banner, the
picture is shipped as banner.png/.jpg for the instance's card.

The package holds the game's translated code and data: the user's own copy, never to be shared.
Based on DAO's scripts/make_package.py (copied here as make_package.py, unchanged).
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FORMAT = 2
TOOL = "make_sod_package.py 2"
LIBRARY_ENTRY = "lib/arm64-v8a/libgame.so"
# State of Decay (XBLA), base game, no title update (SoDRecomp/kernel/title_update.cpp,
# content/content.h).
SOD_TITLE_ID = "584111E8"
SOD_TITLE = "State of Decay"
SOD_XEX_SHA1 = "f75024457e30940e3cc79e0b56f4fa82d06dd039"
# The launcher's menu order (the game's own settings always come first, written by the launcher).
PRESETS = ["balanced", "very_low"]
OPTIONS = {
    "frame_rate": {"config": "video.frame_rate", "choices": [30, 60], "default": 30},
    "preset": {"target": "game/systemoverride.cfg", "choices": PRESETS},
}


# The launcher's ABI header (XDroid360, DAO's tree, read only): the library takes its ABI from
# it too (android/app/cpp/xdroid360_abi.h), so the manifest and the library can't disagree.
# XDROID360_ROOT: an XDroid360 checkout (default: the author's layout, one level above this
# one's parent).
XDROID360_ROOT = Path(os.environ.get("XDROID360_ROOT", ROOT.parent.parent / "DAO"))
ABI_HEADER = XDROID360_ROOT / "android" / "app" / "src" / "main" / "cpp" / "dao_abi.h"


def abi_version():
    """XDROID360_ABI (today DAO_ABI_VERSION) from the launcher's dao_abi.h."""
    text = ABI_HEADER.read_text(encoding="utf-8")
    m = re.search(r"#define\s+(?:XDROID360_ABI|DAO_ABI_VERSION)\s+(\d+)", text)
    if not m:
        sys.exit(f"{ABI_HEADER} has no XDROID360_ABI / DAO_ABI_VERSION")
    return int(m.group(1))


def file_hash(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True)
    parser.add_argument("--library", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--banner", help="a .png or .jpg for the instance's card")
    parser.add_argument("--legacy-format1", action="store_true",
                        help="a format 1 .daopkg for the launcher already on the phone (test builds only): "
                             "DAO's xex_sha1 pin, SoD's values in build.sod_*, no options")
    args = parser.parse_args()

    presets = {p: ROOT / "presets" / f"{p}.cfg" for p in PRESETS}
    for p, path in presets.items():
        if not re.fullmatch(r"[a-z0-9_]+", p) or not path.is_file():
            sys.exit(f"Bad or missing preset {p} ({path})")
    banner = Path(args.banner).resolve() if args.banner else None
    if banner and (not banner.is_file() or banner.suffix.lower() not in (".png", ".jpg")):
        sys.exit(f"The banner must be an existing .png or .jpg: {banner}")

    game = Path(args.game).resolve()
    library = Path(args.library).resolve()
    xex = game / "default.xex"
    if not xex.is_file():
        sys.exit(f"No default.xex in {game}")
    xex_sha1 = file_hash(xex, "sha1")
    if xex_sha1 != SOD_XEX_SHA1:
        sys.exit(f"{xex} isn't State of Decay's supported default.xex (SHA-1 {xex_sha1})")
    if not library.is_file():
        sys.exit(f"No game library at {library}")

    files = sorted(p for p in game.rglob("*") if p.is_file())
    game_bytes = sum(p.stat().st_size for p in files)
    library_sha256 = file_hash(library, "sha256")
    created = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = {
        "format": FORMAT,
        "abi": abi_version(),
        "title_id": SOD_TITLE_ID,
        "title": SOD_TITLE,
        "media_id": None,  # an XBLA package, no disc
        "xex_sha1": xex_sha1,
        "title_update": None,
        "build": {
            "game": "State of Decay (XBLA) - SoDRecomp",
            "created": created,
            "tool": TOOL,
        },
        "library_sha256": library_sha256,
        "game_bytes": game_bytes,
        "game_files": len(files),
        "options": OPTIONS,
    }

    if args.legacy_format1:
        # The installed launcher (format 1) accepts only DAO's disc pin (PHASE5_LOG.md section 1).
        manifest = {k: v for k, v in manifest.items() if k not in ("title", "options")}
        manifest.update(format=1, media_id="", xex_sha1="532c0ec164f44bc8784cc0e8f6f67f72c1ed3751")
        manifest["build"].update(sod_title_id=SOD_TITLE_ID, sod_xex_sha1=xex_sha1, tool="make_sod_package.py 2 (format 1)")
        presets, banner = {}, None

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    suffix = ".daopkg" if args.legacy_format1 else ".x360pkg"
    out = out_dir / f"SoD-{SOD_TITLE_ID}-{library_sha256[:8]}{suffix}"
    partial = out.with_suffix(suffix + ".partial")
    print(f"Writing {out} ({len(files)} game files, {game_bytes / 2**30:.2f} GB)")
    with zipfile.ZipFile(partial, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        z.write(library, LIBRARY_ENTRY)
        for p, path in presets.items():
            z.write(path, f"presets/{p}.cfg")
        if banner:
            z.write(banner, "banner" + banner.suffix.lower())
        for p in files:
            z.write(p, "game/" + p.relative_to(game).as_posix())
    partial.replace(out)
    print(f"Done: {out} ({out.stat().st_size / 2**30:.2f} GB)")
    print("This package is your own copy of the game: don't share it.")


if __name__ == "__main__":
    main()
