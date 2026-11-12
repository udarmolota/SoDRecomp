// (SoD) Room for the Russian text in the game's text database (UL\TextDB\TextDB.cpp).
//
// The game keeps every string of its language file (Game/Languages/<language>.xbox360.btxt:
// "TXDB", 1, 1, the count, the sorted keys, then the NUL-terminated strings) in one buffer of
// 0xB6800 bytes (747,520), and both the file parser and AddString refuse anything bigger. That
// fits the disc's languages (French, the largest, has 727,792 bytes of strings), but not the
// Russian translation (kernel/vfs.h, VfsMountRussianText): Cyrillic takes two bytes a letter in
// UTF-8, about 1.09 MB. Here the buffer is 2 MB and both limits follow it; everything else is
// the game's own logic, step for step. Other languages load exactly as before.
//
// The database (at 0x83E00654): { count, keys, string pointers, bytes used, buffer }.
//   sub_833D1098  the database's start: allocates the keys (64 KB) and the buffer (0xB6800),
//                 then sub_833D0E08.
//   sub_833D0E08  clears the database and loads the language file (ParseTextData).
//   sub_833D0BF0  ParseTextData(data, size, name).
//   sub_833D06B8  AddString(string, key) -> 1 if added.
//   sub_833D0640  the key's index, or 16384 if absent; sub_833D05D8 a free index for a key, or
//                 16384 if full; sub_833D0550 reads a NUL-terminated string from a buffer.
//   sub_82271488  the game's allocator (size, tag).
#include "stdafx.h"
#include "function.h"
#include "kernel/game_build.h"
#include "kernel/thread.h"

namespace
{
    using namespace sod::kernel;

    constexpr uint32_t kDatabase = 0x83E00654;
    constexpr uint32_t kCount = kDatabase + 0, kKeys = kDatabase + 4, kStrings = kDatabase + 8;
    constexpr uint32_t kUsed = kDatabase + 12, kBuffer = kDatabase + 16;
    constexpr uint32_t kMaxStrings = 16384;
    // The game's buffer and ours.
    constexpr uint32_t kGameBufferSize = 0xB6800;
    constexpr uint32_t kBufferSize = 0x200000;

    uint32_t s_bigBuffer = 0;

    // Before each load (the first one runs from the database's start): the 2 MB buffer in
    // place of the game's. Allocated once, from the game's own heap; the game's buffer stays.
    void LoadHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        if (!s_bigBuffer)
        {
            s_bigBuffer = uint32_t(CallGuest(0x82271488, { kBufferSize, 14 }));
            if (s_bigBuffer)
                LOGI("Text database: a {} KB string buffer (the game's has {} KB)", kBufferSize / 1024,
                    kGameBufferSize / 1024);
            else
                LOGW("Text database: no memory for a bigger string buffer");
        }
        if (s_bigBuffer)
        {
            PPC_STORE_U32(kBuffer, s_bigBuffer);
            // The game clears 0xB6800 bytes of it before loading; the rest is cleared here.
            memset(base + s_bigBuffer + kGameBufferSize, 0, kBufferSize - kGameBufferSize);
        }
        original(ctx, base);
    }

    uint32_t BufferSize(uint8_t* base)
    {
        return s_bigBuffer && PPC_LOAD_U32(kBuffer) == s_bigBuffer ? kBufferSize : kGameBufferSize;
    }

    // AddString(string r3, key r4): the game's, with the buffer's real size.
    void AddStringHook(PPCContext& ctx, uint8_t* base, PPCFunc*)
    {
        const uint32_t string = ctx.r3.u32, key = ctx.r4.u32;
        ctx.r3.u64 = 0;
        if (uint32_t(CallGuest(0x833D0640, { key })) != kMaxStrings)
            return; // already there
        const uint32_t index = uint32_t(CallGuest(0x833D05D8, { key }));
        if (index >= kMaxStrings)
        {
            LOGW("Text database: no room for key {:08X} (16384 strings)", key);
            return;
        }
        const uint32_t length = uint32_t(strlen(reinterpret_cast<const char*>(base + string))) + 1;
        const uint32_t used = PPC_LOAD_U32(kUsed);
        if (used + length >= BufferSize(base))
        {
            LOGW("Text database: string buffer full at key {:08X}", key);
            return;
        }
        const uint32_t to = PPC_LOAD_U32(kBuffer) + used;
        PPC_STORE_U32(PPC_LOAD_U32(kKeys) + index * 4, key);
        PPC_STORE_U32(PPC_LOAD_U32(kStrings) + index * 4, to);
        memmove(base + to, base + string, length);
        PPC_STORE_U32(kUsed, used + length);
        PPC_STORE_U32(kCount, PPC_LOAD_U32(kCount) + 1);
        ctx.r3.u64 = 1;
    }

    // ParseTextData(data r3, size r4, name r5): the game's checks and order, with the string
    // area allowed up to the buffer's real size. A file the game would refuse is refused (and
    // logged here) the same way, so its strings show as <LANGUAGE>_LOC_MISSING.
    void ParseTextDataHook(PPCContext& ctx, uint8_t* base, PPCFunc*)
    {
        const uint32_t data = ctx.r3.u32, size = ctx.r4.u32, name = ctx.r5.u32;
        const std::string file = name ? reinterpret_cast<const char*>(base + name) : "?";
        const uint32_t end = data + size;
        if (size < 16 || PPC_LOAD_U32(data) != 0x54584442 || PPC_LOAD_U32(data + 4) != 1 ||
            PPC_LOAD_U32(data + 8) != 1)
        {
            LOGW("Text database: {} isn't a text file", file);
            return;
        }
        const uint32_t count = PPC_LOAD_U32(data + 12);
        if (count > kMaxStrings || uint64_t(data) + 16 + uint64_t(count) * 4 > end)
        {
            LOGW("Text database: {} has {} strings or is cut short", file, count);
            return;
        }
        uint32_t at = data + 16 + count * 4;
        if (end - at > BufferSize(base))
        {
            LOGW("Text database: {} has {} bytes of strings, more than {}", file, end - at, BufferSize(base));
            return;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            const void* nul = memchr(base + at, 0, end - at);
            if (!nul)
            {
                LOGW("Text database: {}: string {} of {} is cut short", file, i, count);
                return;
            }
            const uint32_t string = at;
            at = uint32_t(static_cast<const uint8_t*>(nul) - base) + 1;
            CallGuest(0x833D06B8, { string, PPC_LOAD_U32(data + 16 + i * 4) });
        }
        if (at != end)
            LOGW("Text database: {} has {} bytes left over", file, end - at);
    }
}

GAME_HOOK_ONE(sub_833D0E08, LoadHook)
GAME_HOOK_ONE(sub_833D06B8, AddStringHook)
GAME_HOOK_ONE(sub_833D0BF0, ParseTextDataHook)
