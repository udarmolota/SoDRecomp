# Host runtime: kernel/XAM HLE, memory, threads, file system, and (later) GPU/audio/input.
project(SoDRecomp CXX)

file(GLOB_RECURSE SOD_RUNTIME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Dear ImGui's SDL3 backends for the launcher (the imgui library itself comes from xenia/),
# and its std::string text fields.
set(SOD_IMGUI_BACKENDS
    ${SOD_XENIA_THIRD_PARTY}/imgui/backends/imgui_impl_sdl3.cpp
    ${SOD_XENIA_THIRD_PARTY}/imgui/backends/imgui_impl_sdlrenderer3.cpp
    ${SOD_XENIA_THIRD_PARTY}/imgui/misc/cpp/imgui_stdlib.cpp)
set_source_files_properties(${SOD_IMGUI_BACKENDS} PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS -w)

# The icon (res/sodrecomp.rc), in both builds.
set(SOD_ICON ${CMAKE_CURRENT_SOURCE_DIR}/res/sodrecomp.rc)

# The processor check (in both builds): built for any x86-64 processor (the last
# -march wins), told which instructions SOD_MARCH takes for granted.
set(SOD_CPU_CHECK ${CMAKE_CURRENT_SOURCE_DIR}/cpu_check.cpp)
if (SOD_MARCH STREQUAL "x86-64-v3")
    set(SOD_CPU_LEVEL 3)
elseif (SOD_MARCH STREQUAL "sandybridge")
    set(SOD_CPU_LEVEL 1)
else()
    set(SOD_CPU_LEVEL 0)
    message(WARNING "SOD_MARCH=${SOD_MARCH}: the startup processor check knows only sandybridge and x86-64-v3, so this build has none")
endif()
set_source_files_properties(${SOD_CPU_CHECK} PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    COMPILE_OPTIONS -march=x86-64 COMPILE_DEFINITIONS SOD_CPU_LEVEL=${SOD_CPU_LEVEL})

# The launcher-only build's entry point (SOD_LAUNCHER_ONLY, see ../CMakeLists.txt).
set(SOD_SETUP_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/launcher/setup_main.cpp)

if (SOD_LAUNCHER_ONLY)
    file(GLOB SOD_LAUNCHER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/content/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mods/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mods/convert/*.cpp)
    list(APPEND SOD_LAUNCHER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/erf.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel/title_update.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/patches/patches.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/settings.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/files.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/log.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/hash.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/program.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/version.cpp
        ${SOD_CPU_CHECK})
    add_executable(SoDRecomp ${SOD_LAUNCHER_SOURCES} ${SOD_IMGUI_BACKENDS} ${SOD_ICON})
    set(SOD_VERSION "dev" CACHE STRING "The release this is built from (e.g. v0.2.0)")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/version.cpp PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON COMPILE_DEFINITIONS "SOD_VERSION=\"${SOD_VERSION}\"")
    target_include_directories(SoDRecomp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${XENON_ROOT}/XenonUtils
        ${SOD_XENIA_THIRD_PARTY}/rapidjson/include
        ${SOD_XENIA_THIRD_PARTY}/imgui/misc/cpp)
    target_compile_definitions(SoDRecomp PRIVATE
        SOD_LAUNCHER_ONLY NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(SoDRecomp PRIVATE
        -fno-strict-aliasing -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
    # xenia-gpu: the texture layouts, for converting mods' textures.
    target_link_libraries(SoDRecomp PRIVATE
        fmt::fmt xenia-gpu SDL3::SDL3-static imgui shell32 winhttp bcrypt)
    # A window program: no console when it's double-clicked. Its DLLs load from
    # System32 only, not from the Downloads folder it's run from.
    target_link_options(SoDRecomp PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /DEPENDENTLOADFLAG:0x800)
    target_precompile_headers(SoDRecomp PRIVATE stdafx.h)
    return()
endif()

list(REMOVE_ITEM SOD_RUNTIME_SOURCES ${SOD_SETUP_MAIN})
add_executable(SoDRecomp ${SOD_RUNTIME_SOURCES} ${SOD_IMGUI_BACKENDS} ${SOD_ICON})

# The release the installer builds (its tag), for the launcher's update check.
set(SOD_VERSION "dev" CACHE STRING "The release this is built from (e.g. v0.2.0)")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/version.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON COMPILE_DEFINITIONS "SOD_VERSION=\"${SOD_VERSION}\"")

target_include_directories(SoDRecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XENON_ROOT}/XenonUtils
    ${SOD_XENIA_THIRD_PARTY}/rapidjson/include
    ${SOD_XENIA_THIRD_PARTY}/imgui/misc/cpp)

target_compile_definitions(SoDRecomp PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS)

target_compile_options(SoDRecomp PRIVATE
    -fno-strict-aliasing
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function)

# XenonUtils provides XEX decryption/decompression (tiny-AES, lzx) for the loader.
target_link_libraries(SoDRecomp PRIVATE
    SoDRecompLib
    XenonUtils
    fmt::fmt dao-xenia
    SDL3::SDL3-static
    imgui
    dao-ffmpeg
    onecore
    xinput
    shell32
    winmm
    winhttp
    bcrypt)

# Recompiled code recurses on the host stack as deeply as the guest does. A window
# program, like the launcher-only build above: no console window behind it
# (SOD_CONSOLE=1 gets one, see main.cpp). Unlike the launcher-only build, no
# /DEPENDENTLOADFLAG: this one is run from its install folder, where players put
# mods such as OptiScaler as dxgi.dll (README.md).
target_link_options(SoDRecomp PRIVATE /STACK:16777216 /MAP /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)

target_precompile_headers(SoDRecomp PRIVATE stdafx.h)

# The temporal upscalers' and frame generation's DLLs go next to the executable;
# they're loaded only when enabled: AMD FidelityFX's signed loader, upscaler (FSR 3.1 /
# FSR 4) and frame generation (FSR 3.1), Intel's libxess.dll (XeSS), and NVIDIA's
# Streamline with its plug-ins for Reflex and DLSS, and DLSS's model library (nvngx_dlss.dll)
# (installer/deps.py's
# STREAMLINE_DLLS, which scripts/check_constants.py checks against this list). (Their own
# rule, so a missing or changed DLL is copied without relinking the exe.)
set(_upscaler_dlls)
set(_streamline_dlls)
foreach(_dll sl.interposer.dll sl.common.dll sl.reflex.dll sl.pcl.dll sl.dlss.dll nvngx_dlss.dll)
    list(APPEND _streamline_dlls ${SOD_TOOLS_ROOT}/Streamline-SDK/bin/x64/${_dll})
endforeach()
foreach(_src
        ${SOD_TOOLS_ROOT}/FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll
        ${SOD_TOOLS_ROOT}/FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll
        ${SOD_TOOLS_ROOT}/FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll
        ${SOD_TOOLS_ROOT}/XeSS-SDK/bin/libxess.dll
        ${_streamline_dlls})
    get_filename_component(_dll ${_src} NAME)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_dll}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_src} ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${_src})
    list(APPEND _upscaler_dlls ${CMAKE_CURRENT_BINARY_DIR}/${_dll})
endforeach()
add_custom_target(SoDRecompUpscalerDlls DEPENDS ${_upscaler_dlls})
add_dependencies(SoDRecomp SoDRecompUpscalerDlls)
