#include "version.h"

// Only this file sees SOD_VERSION (and it skips the precompiled header), so a new
// version rebuilds nothing else.
#ifndef SOD_VERSION
#define SOD_VERSION "dev"
#endif

namespace sod
{
    const char* Version()
    {
        return SOD_VERSION;
    }
}
