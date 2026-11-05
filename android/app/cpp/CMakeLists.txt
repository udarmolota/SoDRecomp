# The app's native side (phase 5): libgame.so, the game as an SDL3 application for the
# XDroid360 launcher (SDL owns the activity, the window, the gamepads and the audio device), on
# the runtime library from android/runtime and Xenia's Vulkan GPU painting into the window;
# libsod-frontend.a, the same front end for the game profile (the launcher's on-device build,
# toolchain/make_profile.sh); and
# libvkdriverhook.so, part of the private Vulkan loader that runs the game on a Turnip
# build instead of the phone's driver (vulkan_private_loader.c). Built by Gradle
# (android/app/build.gradle), or directly:
#   cmake -G Ninja -S android/app/src/main/cpp -B build/app \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
project(SodDroidApp C CXX ASM)

if (NOT ANDROID)
    message(FATAL_ERROR "This target is for the Android NDK toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# (SoD) sod360/android/app/cpp: sod360 is three levels up; SoDRecomp/ is the runtime.
set(SOD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
get_filename_component(SOD_ROOT ${SOD_ROOT} ABSOLUTE)
set(RECOMP ${SOD_ROOT}/SoDRecomp)
set(SOD_SRC ${RECOMP}/SoDRecomp)
include(${SOD_ROOT}/cmake/XDroid360Root.cmake)

# ---------------------------------------------------------------- SDL3 (shared)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
add_subdirectory(${RECOMP}/thirdparty/SDL ${CMAKE_BINARY_DIR}/sdl)

# SOD_LAUNCHER_ONLY: the launcher APK, without the game library (no translated code needed to
# build it; the game library comes in the player's game package).
option(SOD_LAUNCHER_ONLY "Build only SDL3 and the driver hook (the launcher APK)" OFF)

# ---------------------------------------------------------------- the runtime

if (NOT SOD_LAUNCHER_ONLY)
    add_subdirectory(${SOD_ROOT}/android/runtime ${CMAKE_BINARY_DIR}/runtime)
endif()

# ---------------------------------------------------------------- private Vulkan loader

# liblinkernsbypass (BSD-2-Clause, Billy Laws): linker namespaces for the private loader.
add_library(linkernsbypass STATIC
    linkernsbypass/android_linker_ns.cpp
    linkernsbypass/elf_soname_patcher.cpp)
target_include_directories(linkernsbypass PUBLIC linkernsbypass)
target_compile_options(linkernsbypass PRIVATE -w)
target_link_libraries(linkernsbypass android dl)

# Loaded into the private loader's namespace ahead of the system libvulkan.so copy;
# hands that copy the chosen driver when it looks for the phone's (-z global: it comes
# before libdl for every library in that namespace).
add_library(vkdriverhook SHARED vkdriverhook.c)
target_compile_options(vkdriverhook PRIVATE -fvisibility=hidden -Wall -Wextra)
target_link_options(vkdriverhook PRIVATE -Wl,-z,global -Wl,-z,max-page-size=16384)

# The launcher's device check (DeviceCheck.java): what the phone's Vulkan offers Xenia, through
# the same driver choice as the game. In both APKs.
add_library(daoprobe SHARED vk_probe.c vulkan_private_loader.c)
target_compile_options(daoprobe PRIVATE -fvisibility=hidden -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(daoprobe PRIVATE linkernsbypass android log dl)
target_link_options(daoprobe PRIVATE -Wl,-z,max-page-size=16384)

# ---------------------------------------------------------------- the game library

if (SOD_LAUNCHER_ONLY)
    return()
endif()

# The launcher<->game ABI (the JNI methods, the SDL build, the command-line options) is the
# launcher's: its header in the XDroid360 checkout, read only (xdroid360_abi.h).
set(XDROID360_ABI_HEADER "${XDROID360_ROOT}/android/app/src/main/cpp/dao_abi.h"
    CACHE FILEPATH "The XDroid360 launcher's ABI header (DAO_ABI_VERSION / XDROID360_ABI)")
if (NOT EXISTS ${XDROID360_ABI_HEADER})
    message(FATAL_ERROR "No launcher ABI header at ${XDROID360_ABI_HEADER}")
endif()

# The front end: SDL's main, the window, input, audio output, the JNI methods and the ABI
# note the launcher reads before loading (xdroid360_note.c). Compiled once: into the dev
# build's libgame.so here, and as libsod-frontend.a for the game profile, which the launcher
# links on the phone with the generated code (recipe format v2).
add_library(sod-frontend OBJECT
    ${SOD_SRC}/input/input.cpp
    ${SOD_SRC}/apu/audio_output.cpp
    app_main.cpp
    app_stubs.cpp
    sdl_window.cpp
    virtual_pad_jni.cpp
    keyboard_android.cpp
    vulkan_private_loader.c
    xdroid360_note.c)
target_compile_options(sod-frontend PRIVATE ${SOD_RUNTIME_OPTIONS})
target_compile_definitions(sod-frontend PRIVATE XDROID360_ABI_HEADER="${XDROID360_ABI_HEADER}")
target_link_libraries(sod-frontend PRIVATE daodroid-runtime SDL3::SDL3 linkernsbypass)
add_library(sod-frontend-static STATIC $<TARGET_OBJECTS:sod-frontend>)
set_target_properties(sod-frontend-static PROPERTIES OUTPUT_NAME sod-frontend)

add_library(game SHARED $<TARGET_OBJECTS:sod-frontend>)
# The runtime's kernel imports and hooks register through static initializers: the whole
# archive goes in, not only what is referenced.
target_link_libraries(game PRIVATE -Wl,--whole-archive daodroid-runtime -Wl,--no-whole-archive SDL3::SDL3 linkernsbypass android log)
# SDL_main must be visible to SDL's Java side (dlsym); the rest stays hidden.
# -Bsymbolic: FFmpeg's NEON assembly addresses its tables directly (no GOT).
target_link_options(game PRIVATE -Wl,--gc-sections -Wl,-Bsymbolic -Wl,-z,max-page-size=16384)
# In a profile build (XDROID360_PROFILE_BUILD, android/runtime) there is no generated game
# code: libgame.so links with it left undefined. It is never shipped or run; building it
# builds every archive the profile takes, and its link command is the list
# scripts/make_profile.sh reads them from.
if (XDROID360_PROFILE_BUILD)
    target_link_options(game PRIVATE -Wl,-z,undefs)
endif()
