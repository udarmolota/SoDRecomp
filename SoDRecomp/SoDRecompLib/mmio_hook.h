#pragma once
// Force-included into the recompiled code (see CMakeLists.txt). XenonRecomp emits
// PPC_MM_STORE_U32 for stores preceded by eieio; the ones that hit the GPU register
// window (0x7FC80000) are forwarded to the runtime's fake command processor.
#include <cstdint>

void SodGpuRegisterWrite(uint32_t address, uint32_t value);
// (SoD) The APU's XMA registers (0x7FEA0000), which State of Decay's XMA library writes.
void SodApuRegisterWrite(uint32_t address, uint32_t value);

#define PPC_MM_STORE_U32(x, y)                                    \
    do                                                            \
    {                                                             \
        const uint32_t sodMmAddress_ = (x);                       \
        const uint32_t sodMmValue_ = (y);                         \
        PPC_STORE_U32(sodMmAddress_, sodMmValue_);                \
        if ((sodMmAddress_ & 0xFFFF0000u) == 0x7FC80000u)         \
            SodGpuRegisterWrite(sodMmAddress_, sodMmValue_);      \
        else if ((sodMmAddress_ & 0xFFFF0000u) == 0x7FEA0000u)    \
            SodApuRegisterWrite(sodMmAddress_, sodMmValue_);      \
    } while (0)
