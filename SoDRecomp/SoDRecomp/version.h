#pragma once
// The release this build is from.

namespace sod
{
    // The release tag the installer built it from (e.g. "v0.2.0", or "v0.3.0-dev.1"
    // for a dev release), or "dev" for other builds (set with the SOD_VERSION CMake
    // option).
    const char* Version();
}
