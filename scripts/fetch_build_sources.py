"""Checks out the sources the game profile's build needs inside an XDroid360 checkout
(XDROID360_ROOT, with its recomp submodule): the xenia-canary parts at the commits DAORecomp's
installer pins (recomp/installer/deps.py), as that installer does, plus the parts only the
Vulkan backend uses. The same as XDroid360's own scripts/fetch_build_sources.py does for DAO,
without what SoD brings itself (XenonRecomp and SDL are this repository's submodules) or
doesn't build (AMD's FidelityFX SDK: Windows only). For CI, or a new machine.

    XDROID360_ROOT=<XDroid360 checkout> python -I scripts/fetch_build_sources.py
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
XDROID360 = Path(os.environ.get("XDROID360_ROOT", ROOT.parent.parent / "DAO")).resolve()
RECOMP = XDROID360 / "recomp"
sys.path.insert(0, str(RECOMP))

from installer import deps, sources  # noqa: E402  (DAORecomp's installer)
from installer.util import Reporter  # noqa: E402

SKIP = {"tools/FidelityFX-SDK", "tools/XenonRecomp", "thirdparty/SDL"}
# What the Android build takes from xenia-canary beyond DAORecomp's sparse checkout: the Vulkan
# backend's sources and shaders, and the submodules it compiles with.
CANARY_PATHS = ("src/xenia/gpu", "src/xenia/ui", "src/xenia/base", "cmake", "third_party/CMakeLists.txt")
CANARY_SUBMODULES = ("third_party/Vulkan-Headers", "third_party/VulkanMemoryAllocator", "third_party/glslang",
                     "third_party/SPIRV-Tools", "third_party/FidelityFX-CAS", "third_party/FidelityFX-FSR")


def git(*args):
    subprocess.run(["git", *args], check=True)


def main():
    if not (RECOMP / "installer" / "deps.py").is_file():
        sys.exit(f"no recomp submodule in {XDROID360} (set XDROID360_ROOT; git submodule update --init)")
    reporter = Reporter()
    for source in deps.GIT_SOURCES:
        if source.path not in SKIP:
            sources.ensure(reporter, source)
    canary = RECOMP / "tools" / "xenia-canary"
    git("-C", str(canary), "sparse-checkout", "add", *CANARY_PATHS)
    git("-C", str(canary), "submodule", "update", "--init", "--depth", "1", *CANARY_SUBMODULES)
    print("sources ready")


if __name__ == "__main__":
    main()
