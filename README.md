# SoDRecomp

A static recompilation of **State of Decay** (Xbox 360 XBLA, title 584111E8) for Android
(ARM64) and the PC. It is based on [DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) (by way of
[its XDroid360 fork](https://github.com/udarmolota/Dragon-Age-Origins-Recomp-Android)):
the same runtime (kernel, XAM, audio), with Xenia's Vulkan GPU shared with
[XDroid360](https://github.com/udarmolota/XDroid360).

**This repository holds no game code or data.** The game is the player's own copy. On the
phone, the XDroid360 launcher builds the game library from it: it extracts the packages,
translates `default.xex` with XenonRecomp, compiles the result and links it with this game
profile.

## What is here

| Path | What |
|---|---|
| `SoDRecomp/` | the runtime: kernel, XAM, audio, the GPU front end, the game's hooks and patches, settings; the PC launcher |
| `android/runtime/`, `android/app/cpp/` | the Android runtime library and front end (SDL3, window, input, JNI, the XDroid360 ABI note) |
| `android/ppc/` | the CMake for the generated game code (development builds) |
| `android/profile/` | the game profile's recipe template (`recipe.json.in`) and its committed inputs (below) |
| `config/` | XenonRecomp's configuration for the game: `SoD.toml` (function boundaries) and `SoD_switch_tables.toml` |
| `scripts/` | `make_profile.sh` (the game profile), `patch_xenonrecomp.py`, `as2c.py` (compiles `ui/*.as` into the `*_as.h` headers), packaging and test tools |
| `presets/` | graphics presets (CryEngine settings) |
| `tests/insn/` | unit checks for the XenonRecomp additions |
| `XenonRecomp/` | submodule: XenonRecomp at ddd128b, patched by `scripts/patch_xenonrecomp.py` (the XDroid360 games' one patch script, pinned) |
| `SoDRecomp/thirdparty/SDL` | submodule: SDL 3.4.18 |

`android/profile/inputs/` holds the only files derived from the game: the two headers
XenonRecomp generates that the runtime includes. `ppc_config.h` holds the image's addresses and
sizes; `ppc_context.h` is the template plus three lines. There is also the sha256 of the
`default.xex` the code was recompiled from. They let the profile build without the game. The
generated code itself (`ppc/`) is never committed.

## Building the game profile

The profile (`xdroid360-profile-sod-<n>.zip`) is what XDroid360 ships for this game. It holds
`recipe.json`, the runtime and front end as `libsod-game.a`, their dependencies as
`libsod-deps.a`, the headers the phone's compile needs, XenonRecomp's config templates and
`licenses/`.

```bash
scripts/make_profile.sh
```

It configures `build/profile` with `-DXDROID360_PROFILE_BUILD=ON` (no generated code needed) and
writes the profile to the toolchain's `out/games/`. It needs:

- `XDROID360_ROOT`: an XDroid360 checkout with its `recomp` submodule and recomp's installer
  tools (xenia-canary's third_party, CMake, Ninja);
- `TOOLCHAIN`: an [xdroid360-toolchain](https://github.com/udarmolota/xdroid360-toolchain)
  checkout with its `out/kit` built;
- `ANDROID_NDK_HOME`: NDK 27.3.13750724.

The recipe format is the toolchain's `RECIPE_FORMAT_DRAFT.md`.

## Licences

MIT (`LICENSE`), like DAORecomp. The profile's `licenses/INDEX.md` lists every third-party
component it carries, with its licence and source. FFmpeg (LGPL-2.1-or-later) is shipped as
object archives that the phone relinks.
