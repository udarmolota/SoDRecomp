"""Patches the XenonRecomp submodule with the XDroid360 games' one recompiler patch script:
recomp/scripts/patch_recompiler.py of the public DAORecomp fork, at a pinned commit (the same
script builds the launcher's libxenonrecomp.so, so SoD's code is recompiled exactly as on the
phone). It holds DAO's and State of Decay's instructions and fixes, the timebase hook in
ppc_context.h, the Xenon disassembler dialect and absolute config paths.

The script comes from XDROID360_ROOT/recomp when that checkout has the commit, else from
GitHub; either way its sha256 must match. Idempotent: patch_recompiler.py keeps each file's
pristine original next to it (.orig) and re-patches from that.

usage: python scripts/patch_xenonrecomp.py [<XenonRecomp checkout>]   (default: ./XenonRecomp)
"""
import hashlib
import importlib.util
import os
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "udarmolota/Dragon-Age-Origins-Recomp-Android"
COMMIT = "86b9d0b1a0af40a44439e9310f02f8c3f2489510"
SCRIPT = "scripts/patch_recompiler.py"
SHA256 = "f007dcf14c2bc8579bebe404d8d466351e6858f2104cbcb5b5aad6793726d936"
XENONRECOMP_COMMIT = "ddd128bcca99fe8bfbb99bea583c972351fa6ace"


def fetch():
    """The pinned script's bytes: from a local DAORecomp checkout if it has the commit."""
    recomp = Path(os.environ.get("XDROID360_ROOT", ROOT.parent.parent / "DAO")) / "recomp"
    if (recomp / ".git").exists():
        r = subprocess.run(["git", "-C", str(recomp), "show", f"{COMMIT}:{SCRIPT}"], capture_output=True)
        if r.returncode == 0:
            return r.stdout
    url = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{SCRIPT}"
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def main():
    checkout = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "XenonRecomp"
    head = subprocess.run(["git", "-C", str(checkout), "rev-parse", "HEAD"], capture_output=True, text=True)
    if head.returncode != 0 or head.stdout.strip() != XENONRECOMP_COMMIT:
        sys.exit(f"{checkout} isn't XenonRecomp at {XENONRECOMP_COMMIT} (git submodule update --init --recursive)")
    data = fetch()
    if hashlib.sha256(data).hexdigest() != SHA256:
        sys.exit(f"{SCRIPT} at {COMMIT} doesn't match its pinned sha256")
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "patch_recompiler.py"
        path.write_bytes(data)
        spec = importlib.util.spec_from_file_location("patch_recompiler", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.patch(str(checkout / "XenonRecomp" / "recompiler.cpp"))
    print(f"XenonRecomp patched with {REPO}@{COMMIT[:7]}:{SCRIPT}")


if __name__ == "__main__":
    main()
