# SoDRecomp's runtime for ARM64 Android: the kernel (threads, objects, memory, VFS, XAM),
# the XMA audio decoder, Xenia's Vulkan GPU (xenia-vulkan/), the null command processor,
# the game's hooks, menu patches, settings and the generated game code, as one static
# library (daodroid-runtime) with two front ends:
#   daodroid_headless  a command-line executable for adb: no window, no SDL (phases 3-4)
#   libdaodroid.so     the app's game library (android/app/src/main/cpp, phase 5)
# See docs/PHASE3_runbook.md, PHASE4_runbook.md and PHASE5_runbook.md.
#
#   cmake -G Ninja -S android/runtime -B build/runtime \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
project(SodDroidRuntime C CXX ASM)

if (NOT ANDROID)
    message(FATAL_ERROR "This target is for the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# (SoD) sod360's layout: SoDRecomp/ is the runtime (RECOMP), ppc/ the generated code,
# XenonRecomp/ the patched recompiler checkout; xenia-canary's third_party is read from the
# DAO checkout (never written), as for xenia-vulkan.
set(SOD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(SOD_ROOT ${SOD_ROOT} ABSOLUTE)
set(RECOMP ${SOD_ROOT}/SoDRecomp)
set(SOD_SRC ${RECOMP}/SoDRecomp)
# XDROID360_PROFILE_BUILD (the same option in both games' runtimes): build only what the game
# profile ships (the runtime, its dependencies, the front end), with no generated game code.
# The runtime then compiles against android/profile/inputs: the two headers XenonRecomp
# generates that it includes (ppc_config.h: the image's addresses and sizes; ppc_context.h:
# the template plus three lines), committed so that CI needs no game. scripts/make_profile.sh
# builds this way; the phone links the generated code in.
option(XDROID360_PROFILE_BUILD "Build only the game profile's libraries (no generated game code)" OFF)
if (XDROID360_PROFILE_BUILD)
    set(SOD_PPC_DIR ${SOD_ROOT}/android/profile/inputs)
else()
    set(SOD_PPC_DIR ${SOD_ROOT}/ppc)
endif()
set(XENON_ROOT ${SOD_ROOT}/XenonRecomp)
set(XENON_TP ${XENON_ROOT}/thirdparty)
include(${SOD_ROOT}/cmake/XDroid360Root.cmake)
set(SOD_XENIA_CANARY "${XDROID360_ROOT}/recomp/tools/xenia-canary" CACHE PATH "xenia-canary checkout (third_party)")
set(XE_TP ${SOD_XENIA_CANARY}/third_party)

# ---------------------------------------------------------------- generated game code

if (NOT XDROID360_PROFILE_BUILD)
    add_subdirectory(${SOD_ROOT}/android/ppc ${CMAKE_BINARY_DIR}/ppc)
endif()

# ---------------------------------------------------------------- Xenia (Vulkan GPU)

# (SoD) The Xenia shared with DAO: DAO's xenia-vulkan, on DAORecomp's vendored Xenia, used
# read-only from the DAO checkout (the shared plan's A5-xv).
set(SOD_SHARED_XENIA_VULKAN "${XDROID360_ROOT}/xenia-vulkan" CACHE PATH "DAO's xenia-vulkan (read-only)")
get_filename_component(SOD_SHARED_XENIA ${SOD_SHARED_XENIA_VULKAN}/../recomp/xenia ABSOLUTE)
add_subdirectory(${SOD_SHARED_XENIA_VULKAN} ${CMAKE_BINARY_DIR}/xenia-vulkan)

# ---------------------------------------------------------------- the native renderer

# (SoD) XDroid360's EDRAM-free renderer from nfsmw-android (native-renderer/NOTICE.md), next to
# Xenia's GPU emulation, read-only from the XDroid360 checkout like xenia-vulkan; config.toml
# [xenia] dao_gpu_renderer picks one at startup (native_host.h).
set(XDROID360_SOURCE_ROOT ${XDROID360_ROOT})
set(XDROID360_DEPS_ROOT ${XDROID360_ROOT})
set(XDROID360_GEN_DIR ${CMAKE_BINARY_DIR}/xenia-vulkan/gen)
add_subdirectory(${XDROID360_ROOT}/native-renderer ${CMAKE_BINARY_DIR}/native-renderer)
add_dependencies(native-renderer xenia-spirv-shaders)

# ---------------------------------------------------------------- XenonUtils (XEX loading)

add_library(XenonUtils STATIC
    ${XENON_ROOT}/XenonUtils/xex.cpp
    ${XENON_ROOT}/XenonUtils/image.cpp
    ${XENON_ROOT}/XenonUtils/xex_patcher.cpp
    ${XENON_ROOT}/XenonUtils/memory_mapped_file.cpp
    ${XENON_TP}/libmspack/libmspack/mspack/lzxd.c
    ${XENON_TP}/tiny-AES-c/aes.c)
target_include_directories(XenonUtils
    PUBLIC ${XENON_ROOT}/XenonUtils ${XENON_TP}/simde
    PRIVATE ${XENON_TP}/libmspack/libmspack/mspack ${XENON_TP}/tiny-AES-c ${XENON_TP}/TinySHA1)
target_compile_options(XenonUtils PRIVATE -w)

# ---------------------------------------------------------------- FFmpeg subset (XMA)

set(XE_TARGET_X86_64 FALSE)
set(XE_TARGET_AARCH64 TRUE)
# The NEON files (ffmpeg-xenia/config.h declares them external for aarch64), assembled by
# clang; xenia-canary's third_party CMake defines this helper, ffmpeg.cmake only calls it.
set(XE_HAS_GAS_ASM TRUE)
function(xe_add_gas_sources target)
    set_source_files_properties(${ARGN} PROPERTIES LANGUAGE ASM)
    target_sources(${target} PRIVATE ${ARGN})
endfunction()
include(${SOD_SHARED_XENIA}/ffmpeg.cmake)

# ---------------------------------------------------------------- the runtime

# The build descriptor the runtime reads the image layout and function table from
# (SoDRecompLib/build_info.cpp, compiled per generated library as SoDRecomp does). In a
# profile build the phone compiles it (the recipe's build-info step).
if (NOT XDROID360_PROFILE_BUILD)
    add_library(daobuildinfo STATIC ${CMAKE_CURRENT_SOURCE_DIR}/build_info_android.cpp)
    target_include_directories(daobuildinfo PRIVATE ${SOD_PPC_DIR} ${RECOMP}/SoDRecompLib ${XENON_TP}/simde)
    target_compile_definitions(daobuildinfo PRIVATE SOD_BUILD_INFO=SodBuildDisc SOD_BUILD_MAPPINGS=PPCFuncMappings)
endif()

# Everything both front ends share. Not here: input/input.cpp and apu/audio_output.cpp
# (SDL3: real in the app, a shim in the headless build), the UI overlays (ImGui over the
# game: keyboard, notices, photo mode, cheats; not ported yet, stubbed by each front end).
add_library(daodroid-runtime STATIC
    ${SOD_SRC}/kernel/game_build.cpp
    ${SOD_SRC}/kernel/import_stubs.cpp
    ${SOD_SRC}/kernel/memory.cpp
    ${SOD_SRC}/kernel/module.cpp
    ${SOD_SRC}/kernel/objects.cpp
    ${SOD_SRC}/kernel/thread.cpp
    ${SOD_SRC}/kernel/title_update.cpp
    ${SOD_SRC}/kernel/vfs.cpp
    ${SOD_SRC}/kernel/xam.cpp
    ${SOD_SRC}/kernel/xboxkrnl_io.cpp
    ${SOD_SRC}/kernel/xboxkrnl_memory.cpp
    ${SOD_SRC}/kernel/xboxkrnl_rtl.cpp
    ${SOD_SRC}/kernel/xboxkrnl_threading.cpp
    ${SOD_SRC}/kernel/xnet.cpp
    ${SOD_SRC}/kernel/xboxkrnl_dpc.cpp
    ${SOD_SRC}/kernel/game_log.cpp
    ${SOD_SRC}/kernel/text_db.cpp
    ${SOD_SRC}/apu/audio.cpp
    ${SOD_SRC}/apu/xma_decoder.cpp
    ${SOD_SRC}/apu/xma_trace.cpp
    ${SOD_SRC}/gpu/command_processor.cpp
    ${SOD_SRC}/gpu/video.cpp
    ${SOD_SRC}/gpu/camera_fov.cpp
    ${SOD_SRC}/gpu/xenia_memory.cpp
    ${SOD_SRC}/content/installed.cpp
    ${SOD_SRC}/content/save_backups.cpp
    ${SOD_SRC}/patches/patches.cpp
    ${SOD_SRC}/ui/erf.cpp
    ${SOD_SRC}/ui/menu_movies.cpp
    ${SOD_SRC}/ui/graphics_menu.cpp
    ${SOD_SRC}/settings.cpp
    ${SOD_SRC}/files.cpp
    ${SOD_SRC}/log.cpp
    ${SOD_SRC}/hash.cpp
    ${SOD_SRC}/program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xenia_host_vulkan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/native_host.cpp
    ${SOD_SRC}/gpu/native_hooks.cpp)
target_include_directories(daodroid-runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SOD_SRC}
    ${SOD_PPC_DIR}
    ${RECOMP}/SoDRecompLib
    ${SOD_SHARED_XENIA}/src
    ${XE_TP}/fmt/include
    ${XE_TP}/tomlplusplus/include
    ${XE_TP}/ffmpeg-xenia
    ${XE_TP}/FFmpeg
    ${XENON_TP}/TinySHA1)
target_compile_definitions(daodroid-runtime PUBLIC FMT_HEADER_ONLY=1)
# The runtime's own sources (SoDRecomp/) are built with these; the generated code and
# Xenia have their own.
set(SOD_RUNTIME_OPTIONS
    -fexceptions -frtti
    -fno-strict-aliasing
    -ffp-contract=off
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
target_compile_options(daodroid-runtime PRIVATE ${SOD_RUNTIME_OPTIONS})
if (XDROID360_PROFILE_BUILD)
    target_link_libraries(daodroid-runtime PUBLIC XenonUtils dao-xenia-vulkan native-renderer libavcodec libavutil log)
else()
    target_link_libraries(daodroid-runtime PUBLIC daobuildinfo daoppc XenonUtils dao-xenia-vulkan native-renderer libavcodec libavutil log)
endif()
# The native renderer is linked: the game's D3D hooks into it (gpu/native_hooks.cpp, under
# XDROID360_NATIVE_RENDERER) are compiled, and they and native_host.cpp see the renderer's
# headers (its compat layer first), as in DAO's runtime.
target_compile_definitions(daodroid-runtime PRIVATE XDROID360_NATIVE_RENDERER=1)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/native_host.cpp ${SOD_SRC}/gpu/native_hooks.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${XDROID360_ROOT}/native-renderer/compat;${XDROID360_ROOT}/native-renderer/src")

# ---------------------------------------------------------------- headless front end

# (Not in a profile build: it would need the generated code.)
if (XDROID360_PROFILE_BUILD)
    return()
endif()

add_executable(daodroid_headless
    ${SOD_SRC}/input/input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main_headless.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs.cpp)
# shim/SDL3/SDL.h: no SDL in the headless build (input.cpp keeps SOD_INPUT_SCRIPT).
target_include_directories(daodroid_headless PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shim)
target_compile_definitions(daodroid_headless PRIVATE SOD_HEADLESS=1)
target_compile_options(daodroid_headless PRIVATE ${SOD_RUNTIME_OPTIONS})
target_link_libraries(daodroid_headless PRIVATE daodroid-runtime)
# The recompiled main thread runs on this thread's stack: as deep as the guest's.
target_link_options(daodroid_headless PRIVATE -Wl,-z,stack-size=16777216)
