# (SoD) State of Decay's generated game code (ppc/, XenonRecomp output) for ARM64 Android, as
# a static library for the runtime. SoDRecompLib's flags in GCC style (clang-cl's
# /fp:strict -> -ffp-model=strict, /FI mmio_hook.h -> -include), as the ARM64 instruction tests
# (tests/insn) build it. Replaces DAO's android/ppc-probe (its harness isn't needed here).
cmake_minimum_required(VERSION 3.22)

if (NOT EXISTS ${SOD_PPC_DIR}/ppc_func_mapping.cpp)
    message(FATAL_ERROR "No generated code in ${SOD_PPC_DIR} (PHASE1_RESULT.md: How to regenerate).")
endif()

set(SOD_PPC_FLAGS -fno-strict-aliasing -ffp-model=strict -ffp-contract=off
    -include ${RECOMP}/SoDRecompLib/mmio_hook.h
    -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)

file(GLOB SOD_PPC_SOURCES CONFIGURE_DEPENDS
    ${SOD_PPC_DIR}/ppc_recomp.*.cpp
    ${SOD_PPC_DIR}/ppc_func_mapping.cpp)
list(LENGTH SOD_PPC_SOURCES _n)
message(STATUS "Generated units: ${_n}")

add_library(daoppc STATIC ${SOD_PPC_SOURCES})
target_include_directories(daoppc PRIVATE ${SOD_PPC_DIR} ${XENON_TP}/simde)
target_compile_options(daoppc PRIVATE ${SOD_PPC_FLAGS})
target_precompile_headers(daoppc PRIVATE ${SOD_PPC_DIR}/ppc_recomp_shared.h)
