# XDROID360_ROOT: an XDroid360 checkout (https://github.com/udarmolota/XDroid360) with its
# recomp submodule (DAORecomp) and the tools recomp's installer fetches (recomp/install.py):
# xenia-vulkan, DAORecomp's vendored Xenia, xenia-canary's third_party, CMake/Ninja/LLVM and
# the launcher's ABI header. Used read only. Taken from -DXDROID360_ROOT=..., else the
# XDROID360_ROOT environment variable, else the author's layout (the XDroid360 checkout,
# named DAO, one level above this checkout's parent).
if (NOT XDROID360_ROOT)
    if (DEFINED ENV{XDROID360_ROOT})
        set(_xdroid360_root "$ENV{XDROID360_ROOT}")
    else()
        # cmake/ -> this checkout -> its parent -> the folder holding DAO.
        set(_xdroid360_root "${CMAKE_CURRENT_LIST_DIR}/../../../DAO")
    endif()
    get_filename_component(_xdroid360_root "${_xdroid360_root}" ABSOLUTE)
    set(XDROID360_ROOT "${_xdroid360_root}" CACHE PATH "XDroid360 checkout (read only)")
    unset(_xdroid360_root)
endif()
if (NOT EXISTS "${XDROID360_ROOT}/xenia-vulkan" OR NOT EXISTS "${XDROID360_ROOT}/recomp")
    message(FATAL_ERROR "No XDroid360 checkout (with its recomp submodule) at ${XDROID360_ROOT}: "
        "set XDROID360_ROOT")
endif()
