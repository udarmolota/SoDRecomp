// (SoD) SOD_GAMELOG=1: the game's own log lines (CryLog and its siblings: format in r3, the
// arguments in r4-r10), formatted here. Log-only. Floats are taken from the GPR slots (varargs).
#include "stdafx.h"
#include "function.h"
#include "kernel/game_build.h"

namespace
{
    using namespace sod::kernel;

    const bool g_gameLog = getenv("SOD_GAMELOG") != nullptr;

    std::string GuestFormat(PPCContext& ctx, uint8_t* base, uint32_t format)
    {
        if (!format)
            return "(null)";
        const char* f = reinterpret_cast<const char*>(base + format);
        const uint64_t args[] = { ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64, ctx.r8.u64, ctx.r9.u64, ctx.r10.u64 };
        size_t next = 0;
        std::string out;
        for (size_t i = 0; f[i] && out.size() < 600; i++)
        {
            if (f[i] != '%')
            {
                out += f[i];
                continue;
            }
            std::string spec = "%";
            while (f[++i] && strchr("-+ #0123456789.lhzI", f[i]))
                spec += f[i];
            const char c = f[i];
            if (!c)
                break;
            if (c == '%')
            {
                out += '%';
                continue;
            }
            const uint64_t v = next < 7 ? args[next++] : 0;
            char buf[256];
            if (c == 's')
            {
                const uint32_t p = uint32_t(v);
                out += p ? std::string(reinterpret_cast<const char*>(base + p)).substr(0, 300) : "(null)";
                continue;
            }
            std::string clean;
            for (char ch : spec)
                if (!strchr("lhzI", ch))
                    clean += ch;
            if (strchr("fgeEG", c))
            {
                double d;
                memcpy(&d, &v, 8);
                snprintf(buf, sizeof(buf), (clean + c).c_str(), d);
            }
            else if (strchr("xXuo", c))
                snprintf(buf, sizeof(buf), (clean + c).c_str(), uint32_t(v));
            else if (c == 'p')
                snprintf(buf, sizeof(buf), "%08X", uint32_t(v));
            else
                snprintf(buf, sizeof(buf), (clean + (c == 'c' ? 'c' : 'd')).c_str(), int32_t(v));
            out += buf;
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        return out;
    }

    template<uint32_t Address>
    void GameLogHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        if (g_gameLog)
            LOGI("game[{:08X}]: {}", Address, GuestFormat(ctx, base, ctx.r3.u32));
        original(ctx, base);
    }
}

GAME_HOOK_ONE(sub_82473F38, GameLogHook<0x82473F38>)
GAME_HOOK_ONE(sub_822700E8, GameLogHook<0x822700E8>)
GAME_HOOK_ONE(sub_82A69100, GameLogHook<0x82A69100>)
GAME_HOOK_ONE(sub_830E7CC0, GameLogHook<0x830E7CC0>)
GAME_HOOK_ONE(sub_822FA2E8, GameLogHook<0x822FA2E8>)
GAME_HOOK_ONE(sub_822FA388, GameLogHook<0x822FA388>)
GAME_HOOK_ONE(sub_83A21FD0, GameLogHook<0x83A21FD0>)
