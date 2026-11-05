// xboxkrnl: deferred procedure calls (KeInitializeDpc, KeInsertQueueDpc). Dragon Age:
// Origins never used them; State of Decay's D3D finishes its GPU interrupt work in a DPC.
//
// A DPC runs at DISPATCH_LEVEL with the PCR's dpc_active set, as soon as the inserting
// processor's IRQL is below DISPATCH_LEVEL: inserted at PASSIVE level it runs before
// KeInsertQueueDpc returns; inserted at DISPATCH level (in an interrupt callback, or
// holding a spin lock) it runs when that ends (RunPendingDpcs, called by the interrupt
// dispatch and when a thread lowers its IRQL).
#include "stdafx.h"
#include "function.h"
#include "thread.h"

#include <unordered_set>

namespace sod::kernel
{
    namespace
    {
        // KDPC (Xenia's XDPC)
        constexpr uint32_t kDpcType = 0x00;           // u16, 19 = DpcObject
        constexpr uint32_t kDpcRoutine = 0x0C;
        constexpr uint32_t kDpcContext = 0x10;
        constexpr uint32_t kDpcArg1 = 0x14;
        constexpr uint32_t kDpcArg2 = 0x18;
        constexpr uint16_t kDpcObject = 19;

        std::mutex s_dpcMutex;
        std::deque<uint32_t> s_pending;       // inserted, not yet run (FIFO)
        std::unordered_set<uint32_t> s_queued; // the same set, for "already inserted"

        void Run(uint32_t dpc)
        {
            GuestThread* thread = GuestThread::Current();
            uint8_t& irql = *g_memory.Translate<uint8_t>(thread->pcr + kPcrIrql);
            be<uint32_t>& dpcActive = *g_memory.Translate<be<uint32_t>>(thread->pcr + kPcrDpcActive);
            const uint8_t oldIrql = irql;
            const uint32_t oldActive = dpcActive;
            irql = kDispatchLevel;
            dpcActive = 1;
            const auto word = [&](uint32_t offset) { return uint32_t(*g_memory.Translate<be<uint32_t>>(dpc + offset)); };
            CallGuest(word(kDpcRoutine), { dpc, word(kDpcContext), word(kDpcArg1), word(kDpcArg2) });
            dpcActive = oldActive;
            irql = oldIrql;
        }
    }

    void RunPendingDpcs()
    {
        for (;;)
        {
            uint32_t dpc;
            {
                std::lock_guard lock(s_dpcMutex);
                if (s_pending.empty())
                    return;
                dpc = s_pending.front();
                s_pending.pop_front();
                s_queued.erase(dpc);
            }
            Run(dpc);
        }
    }
}

namespace sod::imp
{
    using namespace sod::kernel;

    void KeInitializeDpc(uint32_t dpc, uint32_t routine, uint32_t context)
    {
        uint8_t* p = g_memory.Translate<uint8_t>(dpc);
        memset(p, 0, 0x1C);
        *reinterpret_cast<be<uint16_t>*>(p + kDpcType) = kDpcObject;
        *reinterpret_cast<be<uint32_t>*>(p + kDpcRoutine) = routine;
        *reinterpret_cast<be<uint32_t>*>(p + kDpcContext) = context;
    }

    // TRUE if inserted, FALSE if it was already waiting to run.
    uint32_t KeInsertQueueDpc(PPCContext& ctx, uint32_t dpc, uint32_t arg1, uint32_t arg2)
    {
        {
            std::lock_guard lock(s_dpcMutex);
            if (!s_queued.insert(dpc).second)
                return 0;
            *g_memory.Translate<be<uint32_t>>(dpc + kDpcArg1) = arg1;
            *g_memory.Translate<be<uint32_t>>(dpc + kDpcArg2) = arg2;
            s_pending.push_back(dpc);
        }
        if (*g_memory.Translate<uint8_t>(ctx.r13.u32 + kPcrIrql) < kDispatchLevel) // r13: the PCR
            RunPendingDpcs();
        return 1;
    }
}

SOD_IMPORT_AS(KeInitializeDpc, sod::imp::KeInitializeDpc)
SOD_IMPORT_AS(KeInsertQueueDpc, sod::imp::KeInsertQueueDpc)
