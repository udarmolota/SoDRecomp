// Force-included into the generated test functions (CMakeLists.txt): routes indirect
// calls (bctrl, blrl) to the checks' recorder instead of the function table.
#pragma once
#include <cstdint>

struct PPCContext;
void TestIndirectCall(PPCContext& ctx, uint8_t* base, uint32_t target);
#define PPC_CALL_INDIRECT_FUNC(x) TestIndirectCall(ctx, base, (x))
