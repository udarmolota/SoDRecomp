#!/usr/bin/env bash
# P2 item 2: State of Decay's game profile for the XDroid360 launcher's on-device build
# (recipe format v2, SD1/toolchain/RECIPE_FORMAT_DRAFT.md):
#   SD1/toolchain/out/games/sod/   recipe.json, lib/, include/, xr/
#   SD1/toolchain/out/games/sod.zip
# lib/libsod-game.a  the runtime (android/runtime) and the front end (android/app/cpp,
#                    libsod-frontend.a), linked --whole-archive on the phone
# lib/libsod-deps.a  every other archive libgame.so links today (XenonUtils, Xenia, FFmpeg,
#                    ...), merged; taken from the dev build's own link line, so it can't drift
# Both without debug info (llvm-strip -g); the symbol tables stay for crash traces.
# licenses/: the license texts of everything in the archives and include/ (INDEX.md).
# Built in its own profile build (build/profile, CMake option XDROID360_PROFILE_BUILD), which
# needs no game: the runtime compiles against android/profile/inputs (the two generated
# headers it includes, and default.xex's sha256). Needs toolchain/out/kit (package.sh).
# Holds no game code or data: the phone makes those from the player's disc.
#
# Paths, each settable in the environment (defaults: the author's layout, this checkout next
# to the toolchain's, and the XDroid360 checkout one level up from both):
#   TOOLCHAIN       the xdroid360-toolchain checkout (its out/ receives games/sod)
#   XDROID360_ROOT  an XDroid360 checkout (the launcher's ABI header, recomp's tools and Xenia)
#   ANDROID_NDK_HOME, DAO_TOOLS as in the toolchain's env.sh
# Usage: scripts/make_profile.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=${PROFILE_BUILD_DIR:-$ROOT/build/profile}
INPUTS=$ROOT/android/profile/inputs
TOOLCHAIN=${TOOLCHAIN:-$ROOT/../toolchain}
TOOLCHAIN=$(cd "$TOOLCHAIN" && pwd)
. "$TOOLCHAIN/env.sh"
KIT_JSON=$TOOLCHAIN/out/kit/kit.json
ABI_HEADER=$XDROID360_ROOT/android/app/src/main/cpp/dao_abi.h
XE_TP=$XENIA_CANARY/third_party
OUT=$TOOLCHAIN/out/games/sod
# The first Python that runs (Windows has a python3 that only opens the Store).
if [ -z "${PYTHON:-}" ]; then
  for PYTHON in python3 python; do "$PYTHON" -c "" 2>/dev/null && break; done
fi
# A path as native tools take it (Windows: C:/...; elsewhere as is).
native() { if command -v cygpath > /dev/null; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }

[ -f "$KIT_JSON" ] || { echo "no $KIT_JSON: run toolchain/package.sh" >&2; exit 1; }

# The dev build, up to date: the libraries the profile is cut from.
[ -f "$B/build.ninja" ] || "$CMAKE" -G Ninja -S "$(native "$ROOT/android/app/cpp")" -B "$(native "$B")" \
  -DCMAKE_MAKE_PROGRAM="$(native "$NINJA")" \
  -DCMAKE_TOOLCHAIN_FILE="$(native "$NDK/build/cmake/android.toolchain.cmake")" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
  -DXDROID360_PROFILE_BUILD=ON -DXDROID360_ROOT="$(native "$XDROID360_ROOT")"
"$NINJA" -C "$B" game sod-frontend-static

# The archives libgame.so links, from its link command: the runtime goes into the game
# archive; the generated code (daoppc) and its descriptor (daobuildinfo) are made on the
# phone; everything else is a dependency. Not libdxbc.a: Xenia's DXBC checksum (AMD's
# DXBCChecksum.cpp, which carries no license grant) serves only the Direct3D 12 backend,
# and SoD's link keeps none of it (--gc-sections), so it isn't shipped.
mapfile -t archives < <("$NINJA" -C "$B" -t commands libgame.so | tail -1 | tr ' ' '\n' | grep '\.a$')
deps=()
for a in "${archives[@]}"; do
  case "$a" in
    runtime/libdaodroid-runtime.a|runtime/libdaobuildinfo.a|ppc/libdaoppc.a) ;;
    xenia-vulkan/libdxbc.a) ;;
    *) deps+=("$a") ;;
  esac
done
[ ${#deps[@]} -gt 5 ] || { echo "unexpected link line: ${archives[*]}" >&2; exit 1; }

rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/lib" "$OUT/include" "$OUT/xr"

merge() {  # merge <out.a> <in.a>...: one archive, via an MRI script (llvm-ar -M)
  local out=$1; shift
  { echo "CREATE $(native "$out")"
    for a in "$@"; do echo "ADDLIB $(native "$B/$a")"; done
    echo "SAVE"; echo "END"; } | "$NDK_LLVM/bin/llvm-ar$EXE" -M
  "$NDK_LLVM/bin/llvm-strip$EXE" -g "$out"
}
merge "$OUT/lib/libsod-game.a" runtime/libdaodroid-runtime.a libsod-frontend.a
merge "$OUT/lib/libsod-deps.a" "${deps[@]}"

# Headers and sources the phone's compile needs (the generated code's forced include, the
# build descriptor, SIMDe).
cp "$ROOT/SoDRecomp/SoDRecompLib/mmio_hook.h" "$ROOT/SoDRecomp/SoDRecompLib/build_info.h" \
   "$ROOT/SoDRecomp/SoDRecompLib/build_info.cpp" "$ROOT/android/runtime/build_info_android.cpp" \
   "$OUT/include/"
cp -r "$ROOT/XenonRecomp/thirdparty/simde" "$OUT/include/simde"
# Only the headers: not the submodule's git link (a .git file) or other dot files.
find "$OUT/include/simde" -name '.*' -prune -exec rm -rf {} +

# XenonRecomp's input, with absolute paths: the disc's default.xex in ${GAME} and the output in
# ${WORK}, which share no root. The patched XenonRecomp (DAO's patch_recompiler.py, marker
# "(XDroid360)") takes an absolute config path as is; the switch table stays next to the config.
sed -e 's|^file_path = .*|file_path = "${GAME}/game/default.xex"|' \
    -e 's|^out_directory_path = .*|out_directory_path = "${WORK}/ppc"|' \
    -e 's|^# Run from this folder.*|# The launcher writes this to ${WORK}/xr/SoD.toml (recipe step xr-config).|' \
    "$ROOT/config/SoD.toml" > "$OUT/xr/SoD.toml.in"
grep -q '^file_path = "${GAME}/game/default.xex"$' "$OUT/xr/SoD.toml.in"
grep -q '^out_directory_path = "${WORK}/ppc"$' "$OUT/xr/SoD.toml.in"
cp "$ROOT/config/SoD_switch_tables.toml" "$OUT/xr/"
cp "$ROOT/XenonRecomp/XenonUtils/ppc_context.h" "$OUT/xr/ppc_context.h"

# The committed inputs must match the generated headers where a local game's are at hand.
for f in ppc_config.h ppc_context.h; do
  if [ -f "$ROOT/ppc/$f" ] && ! cmp -s "$ROOT/ppc/$f" "$INPUTS/$f"; then
    echo "android/profile/inputs/$f differs from the generated ppc/$f: copy it over" >&2; exit 1
  fi
done

# recipe.json: the template with this build's values. The hashes pin what the prebuilt
# runtime was compiled against: the disc's default.xex, and the generated ppc_config.h and
# ppc_context.h (XenonRecomp writes the latter from xr/ppc_context.h, byte for byte).
sha() { sha256sum "$1" | cut -c1-64; }
abi=$(sed -n 's/^#define[[:space:]]\+\(XDROID360_ABI\|DAO_ABI_VERSION\)[[:space:]]\+\([0-9]\+\).*/\2/p' "$ABI_HEADER" | head -1)
clang=$("$PYTHON" -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["tools"]["clang"])' "$KIT_JSON")
xr=$("$PYTHON" -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["tools"]["xenonrecomp"])' "$KIT_JSON")
[ -n "$abi" ] || { echo "no ABI in $ABI_HEADER" >&2; exit 1; }
sed -e "s|@ABI@|$abi|g" -e "s|@CLANG@|$clang|g" -e "s|@XENONRECOMP@|$xr|g" \
    -e "s|@XEX_SHA256@|$(cut -c1-64 "$INPUTS/default.xex.sha256")|g" \
    -e "s|@PPC_CONFIG_SHA256@|$(sha "$INPUTS/ppc_config.h")|g" \
    -e "s|@PPC_CONTEXT_SHA256@|$(sha "$INPUTS/ppc_context.h")|g" \
    "$ROOT/android/profile/recipe.json.in" > "$OUT/recipe.json"
if grep -q '@[A-Z0-9_]*@' "$OUT/recipe.json"; then echo "unfilled placeholder in recipe.json" >&2; exit 1; fi
"$PYTHON" -I -c 'import json,sys; json.load(open(sys.argv[1])); print("recipe.json parses")' "$OUT/recipe.json"

# The license texts of what the archives and include/ hold (libgame.so on the phone is made
# from them, with the launcher's libSDL3.so).
LIC=$OUT/licenses
mkdir -p "$LIC"
lic() {  # lic <name in licenses/> <source file>
  [ -f "$2" ] || { echo "missing license text $2" >&2; exit 1; }
  cp "$2" "$LIC/$1"
}
ffmpeg_version=$(cat "$XE_TP/FFmpeg/VERSION" 2>/dev/null || cat "$XE_TP/FFmpeg/RELEASE")
ffmpeg_commit=$(git -C "$XE_TP/FFmpeg" rev-parse HEAD)
canary_commit=$(git -C "$XENIA_CANARY" rev-parse --short=7 HEAD)
xr_commit=$(git -C "$XR_SRC" rev-parse --short=7 HEAD)
grep -q 'FFMPEG_LICENSE "LGPL version 2.1 or later"' "$XE_TP/ffmpeg-xenia/config.h" ||
  { echo "FFmpeg isn't configured as LGPL 2.1+ ($XE_TP/ffmpeg-xenia/config.h)" >&2; exit 1; }
lic SoDRecomp.txt "$ROOT/SoDRecomp/LICENSE"
lic Xenia.txt "$XDROID360_ROOT/recomp/xenia/LICENSE"
lic LGPL-2.1.txt "$XE_TP/FFmpeg/COPYING.LGPLv2.1"
lic FFmpeg-LICENSE.md "$XE_TP/FFmpeg/LICENSE.md"
lic glslang.txt "$XE_TP/glslang/LICENSE.txt"
lic imgui.txt "$XE_TP/imgui/LICENSE.txt"
lic fmt.txt "$XE_TP/fmt/LICENSE"
lic snappy.txt "$XE_TP/snappy/COPYING"
lic xxhash.txt "$XE_TP/xxhash/LICENSE"
lic VulkanMemoryAllocator.txt "$XE_TP/VulkanMemoryAllocator/LICENSE.txt"
lic Vulkan-Headers.md "$XE_TP/Vulkan-Headers/LICENSE.md"
lic FidelityFX-FSR.txt "$XE_TP/FidelityFX-FSR/license.txt"
lic FidelityFX-CAS.txt "$XE_TP/FidelityFX-CAS/LICENSE.txt"
lic XenonRecomp.md "$XR_SRC/LICENSE.md"
lic tiny-AES-c.txt "$XR_SRC/thirdparty/tiny-AES-c/unlicense.txt"
sed -n '1,/\*\//p' "$XR_SRC/thirdparty/TinySHA1/TinySHA1.hpp" > "$LIC/TinySHA1.txt"
lic SIMDe.txt "$XR_SRC/thirdparty/simde/COPYING"
lic SDL3.txt "$SDL_SRC/LICENSE.txt"
# linkernsbypass carries only its SPDX line and copyright: the BSD-2-Clause text with them.
cat > "$LIC/linkernsbypass.txt" <<'EOF'
liblinkernsbypass (android/app/cpp/linkernsbypass)
Copyright (c) 2021 Billy Laws
SPDX-License-Identifier: BSD-2-Clause

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of
   conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
EOF
cat > "$LIC/INDEX.md" <<EOF
# Licenses: State of Decay's game profile

On the phone, libgame.so is linked from lib/libsod-game.a, lib/libsod-deps.a, the game code
the launcher generates from the player's own disc, and the launcher's libSDL3.so.

| Component | Where | License | Text | Source |
|---|---|---|---|---|
| SoDRecomp (the runtime, front end and build descriptor; based on DAORecomp) | libsod-game.a, include/ | MIT | SoDRecomp.txt | https://github.com/udarmolota/SoDRecomp |
| Xenia (GPU, UI, base; DAORecomp's vendored copy and XDroid360's xenia-vulkan) | libsod-deps.a | BSD-3-Clause | Xenia.txt | https://github.com/xenia-canary/xenia-canary at $canary_commit; https://github.com/udarmolota/XDroid360 (xenia-vulkan) |
| FFmpeg $ffmpeg_version (libavcodec, libavutil: the XMA decoder), configured LGPL 2.1 or later | libsod-deps.a | LGPL-2.1-or-later | LGPL-2.1.txt, FFmpeg-LICENSE.md | https://github.com/has207/FFmpeg (branch xmaframes) at $ffmpeg_commit. libsod-deps.a is an object archive: libgame.so is relinked on the phone at every build, and can be relinked with a modified FFmpeg |
| glslang (SPIR-V builder) | libsod-deps.a | BSD-3-Clause and others (see text) | glslang.txt | https://github.com/KhronosGroup/glslang |
| Dear ImGui | libsod-deps.a | MIT | imgui.txt | https://github.com/ocornut/imgui |
| {fmt} | libsod-deps.a | MIT | fmt.txt | https://github.com/fmtlib/fmt |
| Snappy | libsod-deps.a | BSD-3-Clause | snappy.txt | https://github.com/google/snappy |
| xxHash | libsod-deps.a | BSD-2-Clause | xxhash.txt | https://github.com/Cyan4973/xxHash |
| Vulkan Memory Allocator | libsod-deps.a | MIT | VulkanMemoryAllocator.txt | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator |
| Vulkan-Headers | libsod-deps.a (headers) | Apache-2.0 / MIT | Vulkan-Headers.md | https://github.com/KhronosGroup/Vulkan-Headers |
| FidelityFX FSR 1, CAS (shaders) | libsod-deps.a | MIT | FidelityFX-FSR.txt, FidelityFX-CAS.txt | https://github.com/GPUOpen-Effects |
| XenonUtils (XEX loading) | libsod-deps.a | MIT | XenonRecomp.md | https://github.com/hedge-dev/XenonRecomp at $xr_commit |
| in XenonUtils: libmspack's lzxd.c | libsod-deps.a | LGPL-2.1 | LGPL-2.1.txt | https://github.com/kyz/libmspack |
| in XenonUtils: tiny-AES-c, TinySHA1 | libsod-deps.a | Unlicense, ISC-style | tiny-AES-c.txt, TinySHA1.txt | XenonRecomp's thirdparty/ |
| SIMDe | include/simde, libsod-game.a | MIT | SIMDe.txt | https://github.com/simd-everywhere/simde |
| liblinkernsbypass | libsod-deps.a | BSD-2-Clause | linkernsbypass.txt | https://github.com/bylaws/liblinkernsbypass |
| SDL 3 (headers; linked against the launcher's libSDL3.so) | libsod-game.a | Zlib | SDL3.txt | https://github.com/libsdl-org/SDL |
EOF

(cd "$TOOLCHAIN/out/games" && "$PYTHON" -I -c '
import os, zipfile
with zipfile.ZipFile("sod.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk("sod"):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, "sod").replace(os.sep, "/"))
')

echo "deps: ${deps[*]}"
du -sh "$OUT/lib"/* "$OUT/include" "$OUT" "$OUT.zip"
