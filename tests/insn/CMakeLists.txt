# Unit checks for SoD's XenonRecomp additions. gen/ is XenonRecomp's test-mode output
# for obj/ (scripts/run_insn_tests.py makes both), checks.cpp the references.
cmake_minimum_required(VERSION 3.20)
project(sod_insn_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(XENONRECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../../XenonRecomp")

file(GLOB GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/gen/*.cpp")
list(FILTER GENERATED EXCLUDE REGEX "/main\\.cpp$")
# The generated code gets DAORecomp's flags for it (android/ppc-probe/CMakeLists.txt, clang-cl).
set_source_files_properties(${GENERATED} PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing;/fp:strict;/clang:-ffp-contract=off")

# One build for the PC target (x86-64-v3, as DAORecomp picks on AVX2 machines) and one
# for plain x86-64, so both simde paths (native SSE4/AVX and fallbacks) are exercised.
foreach(variant v3 baseline)
    add_executable(insn_tests_${variant} checks.cpp ${GENERATED})
    target_include_directories(insn_tests_${variant} PRIVATE "${XENONRECOMP}/XenonUtils" "${XENONRECOMP}/thirdparty/simde")
    target_compile_options(insn_tests_${variant} PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/test_hooks.h"
        -Wno-unused-label -Wno-unused-variable -Wno-deprecated-declarations)
    if(variant STREQUAL "v3")
        target_compile_options(insn_tests_${variant} PRIVATE "/clang:-march=x86-64-v3")
    endif()
endforeach()
