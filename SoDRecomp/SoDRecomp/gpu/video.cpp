// xboxkrnl Vd* (video driver) exports: the display mode (1280x720), and the ring
// buffer, interrupt and swap setup, handed to the vendored Xenia GPU (xenia_host.cpp),
// or to the minimal command processor when Xenia isn't running.
#include "stdafx.h"
#include "kernel/function.h"
#include "gpu/video.h"
#include "gpu/command_processor.h"
#include "gpu/xenia_host.h"
#include "kernel/thread.h"
#include "kernel/objects.h"
#include "settings.h"
#include "ui/graphics_menu.h"
#include "xenia/dao_live_settings.h"

namespace sod::gpu
{
    VideoState g_video;

    // -1: not changed at runtime, use the setting.
    static std::atomic<int> s_frameRateOverride{ -1 };

    int FrameRate()
    {
        const int value = s_frameRateOverride.load(std::memory_order_relaxed);
        return value < 0 ? GetSettings().frameRate : value;
    }

    void SetFrameRate(int frameRate)
    {
        s_frameRateOverride = frameRate;
        LOGI("Frame rate: {}", FrameRateName(frameRate));
    }

    void ToggleGameFrameRate()
    {
        // The rate to go back to: the last one other than 30 (hotkey thread only).
        static int s_return = 60;
        const int frameRate = FrameRate();
        if (frameRate != 30)
            s_return = frameRate;
        SetFrameRate(frameRate != 30 ? 30 : s_return);
    }

    // Above 60 fps the game's presents aren't held to vblanks (SwapInterruptHook),
    // so the render thread is held to the frame rate here (not at all when
    // unlimited). A frame late by more than a frame starts the schedule again.
    // With vsync, 0.5% faster than the rate: a display at about the rate then
    // paces the frames itself, rather than the two clocks drifting apart and a
    // refresh going without a new frame every so often.
    static void PaceFrames()
    {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point s_next;
        const int frameRate = FrameRate();
        if (!FrameRateAbove60(frameRate) || frameRate == 0)
        {
            s_next = {};
            return;
        }
        const double rate =
            xe::dao_live::Load(xe::dao_live::values.present_vsync) ? frameRate * 1.005 : frameRate;
        const auto period = std::chrono::nanoseconds(int64_t(1e9 / rate));
        const auto now = Clock::now();
        if (s_next == Clock::time_point{} || now - s_next > period)
            s_next = now;
        else if (now < s_next)
            kernel::ShortSleep(uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(s_next - now).count()));
        s_next += period;
    }

    // A frame is paced once its swap is kicked to the GPU: the game's D3D writes the
    // ring buffer's write pointer just after VdSwap, so sleeping in VdSwap would
    // hold the finished frame back from the GPU. (Its render thread only.)
    static thread_local bool t_framePending = false;

    void FrameSwapped()
    {
        // (A frame not kicked by the next swap is paced then.)
        if (t_framePending)
            PaceFrames();
        t_framePending = true;
    }

    void FrameKicked()
    {
        if (!t_framePending)
            return;
        t_framePending = false;
        PaceFrames();
    }
}

namespace sod::gpu
{
    void SetDisplayMode(uint32_t width, uint32_t height)
    {
        g_video.displayWidth = width;
        g_video.displayHeight = height;
        LOGI("Display mode reported to the game: {}x{}", width, height);
    }
}

namespace sod::imp
{
    using namespace sod::kernel;
    using sod::gpu::g_video;

    struct X_VIDEO_MODE
    {
        u32 displayWidth;
        u32 displayHeight;
        u32 isInterlaced;
        u32 isWidescreen;
        u32 isHiDef;
        be<float> refreshRate;
        u32 videoStandard;
        u32 pixelRate;
        u32 widescreenFlag;
        u32 reserved[3];
    };

    static void FillVideoMode(X_VIDEO_MODE* mode)
    {
        memset(mode, 0, sizeof(*mode));
        mode->displayWidth = g_video.displayWidth;
        mode->displayHeight = g_video.displayHeight;
        mode->isWidescreen = 1;
        mode->isHiDef = 1;
        mode->refreshRate = 60.0f;
        mode->videoStandard = 1;
        mode->pixelRate = 0x8A;
        mode->widescreenFlag = 0x01;
    }

    void VdQueryVideoMode(X_VIDEO_MODE* mode) { FillVideoMode(mode); }

    uint32_t VdQueryVideoFlags()
    {
        return 1 | (g_video.displayWidth >= 1280 ? 2 : 0) | (g_video.displayWidth >= 1920 ? 4 : 0);
    }

    struct X_DISPLAY_INFO
    {
        u16 frontBufferWidth;      // 0x0
        u16 frontBufferHeight;     // 0x2
        uint8_t frontBufferColorFormat;
        uint8_t frontBufferPixelFormat;
        uint8_t pad0[2];
        u32 scalerSourceRect[4];   // 0x8
        u32 scaledOutputWidth;     // 0x18
        u32 scaledOutputHeight;    // 0x1C
        u32 verticalFilterType;    // 0x20
        u32 verticalFilter[3];     // 0x24
        u32 horizontalFilterType;  // 0x30
        u32 horizontalFilter[3];   // 0x34
        u16 overscanLeft;          // 0x40
        u16 overscanTop;
        u16 overscanRight;
        u16 overscanBottom;
        u16 displayWidth;          // 0x48
        u16 displayHeight;         // 0x4A
        be<float> refreshRate;     // 0x4C
        u32 interlaced;            // 0x50
        uint8_t displayColorFormat;// 0x54
        uint8_t pad1;
        u16 actualDisplayWidth;    // 0x56
    };
    static_assert(sizeof(X_DISPLAY_INFO) == 0x58);

    void VdGetCurrentDisplayInformation(X_DISPLAY_INFO* info)
    {
        memset(info, 0, sizeof(*info));
        const uint32_t w = g_video.displayWidth, h = g_video.displayHeight;
        info->frontBufferWidth = uint16_t(w);
        info->frontBufferHeight = uint16_t(h);
        info->scalerSourceRect[2] = w;
        info->scalerSourceRect[3] = h;
        info->scaledOutputWidth = w;
        info->scaledOutputHeight = h;
        info->verticalFilterType = 1;
        info->horizontalFilterType = 1;
        info->overscanLeft = 320;
        info->overscanTop = 180;
        info->overscanRight = 320;
        info->overscanBottom = 180;
        info->displayWidth = uint16_t(w);
        info->displayHeight = uint16_t(h);
        info->refreshRate = 60.0f;
        info->actualDisplayWidth = uint16_t(w);
    }

    void VdGetCurrentDisplayGamma(u32* type, be<float>* power)
    {
        if (type)
            *type = 2; // BT.709
        if (power)
            *power = 2.22222233f;
    }

    uint32_t VdSetDisplayMode(uint32_t flags) { return 0; }

    uint32_t VdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t arg, uint32_t pfp, uint32_t me)
    {
        LOGI("VdInitializeEngines({:08X}, cb {:08X}, arg {:08X})", unk0, callback, arg);
        return 1;
    }

    void VdShutdownEngines() {}
    uint32_t VdEnableDisableClockGating(uint32_t) { return 0; }

    void VdSetGraphicsInterruptCallback(uint32_t callback, uint32_t userData)
    {
        LOGI("VdSetGraphicsInterruptCallback({:08X}, {:08X})", callback, userData);
        g_video.interruptCallback = callback;
        g_video.interruptUserData = userData;
        if (sod::gpu::XeniaActive())
            sod::gpu::XeniaSetInterruptCallback(callback, userData);
    }

    void VdInitializeRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2)
    {
        LOGI("VdInitializeRingBuffer({:08X}, 2^{})", physicalAddress, sizeLog2);
        g_video.ringBufferBase = physicalAddress;
        g_video.ringBufferSizeLog2 = sizeLog2;
        if (sod::gpu::XeniaActive())
            sod::gpu::XeniaInitializeRingBuffer(physicalAddress, sizeLog2);
        else
            sod::gpu::StartCommandProcessor();
    }

    void VdEnableRingBufferRPtrWriteBack(uint32_t address, uint32_t blockSizeLog2)
    {
        LOGI("VdEnableRingBufferRPtrWriteBack({:08X}, 2^{})", address, blockSizeLog2);
        g_video.readPointerWriteBack = address;
        g_video.writeBackBlockSizeLog2 = blockSizeLog2;
        if (sod::gpu::XeniaActive())
            sod::gpu::XeniaEnableReadPointerWriteBack(address, blockSizeLog2);
    }

    void VdGetSystemCommandBuffer(uint8_t* p0, u32* p1)
    {
        memset(p0, 0, 0x94);
        *reinterpret_cast<u32*>(p0) = 0xBEEF0000;
        *p1 = 0xBEEF0001;
    }

    void VdSetSystemCommandBufferGpuIdentifierAddress(uint32_t address)
    {
        g_video.gpuIdentifierAddress = address;
    }

    uint32_t VdInitializeScalerCommandBuffer(uint32_t sourceXY, uint32_t sourceWH, uint32_t outputXY,
        uint32_t outputWH, uint32_t frontBufferWH, uint32_t verticalFilterType, uint32_t verticalFilter,
        uint32_t horizontalFilterType)
    {
        // Args 9-12 are on the stack: (horizontal params, unk, dest, count).
        PPCContext& ctx = *CurrentContext();
        const uint32_t dest = uint32_t(bridge::IntArg(ctx, 10));
        const uint32_t count = uint32_t(bridge::IntArg(ctx, 11));
        if (dest)
        {
            auto* words = g_memory.Translate<u32>(dest);
            for (uint32_t i = 0; i < count; i++)
                words[i] = 0x80000000; // type-2 packet: nop
        }
        return count;
    }

    uint32_t VdCallGraphicsNotificationRoutines(uint32_t unk0, uint32_t args) { return 0; }
    uint32_t VdIsHSIOTrainingSucceeded() { return 1; }

    uint32_t VdPersistDisplay(uint32_t unk0, u32* out)
    {
        if (out)
            *out = PhysicalAlloc(64, 0x1000, 0x1000, X_PAGE_READWRITE, 0, 0xFFFFFFFF);
        return 1;
    }

    uint32_t VdRetrainEDRAMWorker(uint32_t) { return 0; }
    uint32_t VdRetrainEDRAM(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }

    // Args 9 and 10 (width/height pointers) are on the stack.
    void VdSwap(uint32_t bufferPtr, uint32_t fetchPtr, uint32_t systemWriteBack, uint32_t systemBuffer,
        uint32_t systemBufferId, u32* frontBufferPtr, u32* textureFormatPtr, u32* colorSpacePtr, u32* widthPtr,
        u32* heightPtr)
    {
        // Before the count: the count lets the game's thread start the next frame.
        if (bufferPtr)
            sod::gpu::XeniaLowLatencyFrameSwapped();
        g_video.swapCount++;
        if (g_video.swapCount == 1 || (g_video.swapCount % 600) == 0)
            LOGI("VdSwap #{}", g_video.swapCount.load());
        sod::gpu::FrameSwapped();
        if (!bufferPtr)
            return;

        static const bool invalidateAll = getenv("SOD_GPU_INVALIDATE_ALL") != nullptr;
        if (invalidateAll)
            sod::gpu::InvalidateAllGpuCopies();

        if (sod::gpu::XeniaActive() && fetchPtr && frontBufferPtr && widthPtr && heightPtr)
        {
            sod::gpu::XeniaWriteSwapPacket(bufferPtr, fetchPtr, *frontBufferPtr, *widthPtr, *heightPtr);
            return;
        }

        // Null command processor: one XE_SWAP packet marks the frame boundary.
        auto* words = g_memory.Translate<u32>(bufferPtr);
        memset(words, 0, 64 * 4);
        words[0] = (3u << 30) | ((63u - 1) << 16) | (0x64u << 8);
        words[1] = 0x53574150; // 'SWAP'
        words[2] = frontBufferPtr ? uint32_t(*frontBufferPtr) : 0;
        words[3] = widthPtr ? uint32_t(*widthPtr) : g_video.displayWidth;
        words[4] = heightPtr ? uint32_t(*heightPtr) : g_video.displayHeight;
    }
}

// One step of D3D's wait for a GPU fence (BlockOnFence, sub_82CD7810, e.g. in
// Present for a frame to be done): a few cycles of delay and the GPU hang
// check; returns nonzero to keep waiting. The render thread spent half its time
// spinning in it, keeping a core busy (and on handhelds taking power from the
// GPU): between the checks it now sleeps briefly.
static void FenceWaitStepHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    original(ctx, base);
    if (ctx.r3.u32)
        sod::kernel::ShortSleep(sod::kernel::kSpinSleepMicroseconds);
}
// (SoD) The same D3D function in SoD's XDK, instruction for instruction (DAO's was sub_82CC4338).
GAME_HOOK_ONE(sub_8375C450, FenceWaitStepHook)

// D3D's swap interrupt handler (runs when the GPU reaches a swap). Its argument
// packs the front buffer address (high bits), the presentation interval (bits
// 8-11) and the immediate-present threshold (low byte); the flip is queued for
// vblank (last flip + interval). The game presents its 3D scenes with interval 2,
// i.e. at 30 fps; clamping the interval to 1 lets them flip on every vblank.
// A late flip (its vblank passed) waits for the next vblank unless less of the
// current one has passed than the threshold (percent): with the threshold at 100
// (set for interval 1 presents too), late frames are shown right away instead of
// dropping to the next vblank, so a frame taking a bit longer than a vblank costs
// that bit rather than a whole vblank. Above 60 fps (or unlimited), every
// present gets interval 0, which the handler flips at once instead of at a
// vblank; they're paced to the rate unless unlimited (PaceFrames). The
// guest flip only chooses which frame the host presents next; tearing is up to
// the host's own vsync setting.
static void SwapInterruptHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    const int frameRate = sod::gpu::FrameRate();
    const uint32_t interval = (ctx.r3.u32 >> 8) & 0xF;
    if (sod::FrameRateAbove60(frameRate))
        ctx.r3.u64 &= ~uint64_t(0xF00);
    else if (frameRate == 60 && interval >= 1)
        ctx.r3.u64 = (ctx.r3.u64 & ~uint64_t(0xFFF)) | 0x100 | 100;
    original(ctx, base);
}
// (SoD) The same D3D handler in SoD's XDK, instruction for instruction (DAO's was sub_82CD4218).
// SoD presents with interval 1 already: its 30 fps cap is the engine's limiter below.
GAME_HOOK_ONE(sub_8376F410, SwapInterruptHook)

// (SoD) CryEngine's frame limiter (sub_830D0FF0, on the render thread). With sys_MaxFPS set
// (30 on the console, from the game's own config) it waits until int(1000 / (sys_MaxFPS + 0.5))
// ms after the last frame by spinning on the engine's timer: 32 ms, so 31.25 fps, with a core
// busy all the while. This sleeps through that wait up to kLimiterSpinMicroseconds before the
// frame is due and leaves the rest, and the frame time it records, to the game's own loop, so
// the pacing stays the game's. At a frame rate other than 30 (video.frame_rate, off by default)
// the limiter is skipped: the vblanks (60) or PaceFrames (above 60) pace the frames instead.
constexpr uint32_t kMaxFpsCVarSlot = 0x83D5E618; // the limiter's cached ICVar* (sys_MaxFPS)
constexpr uint32_t kLimiterSpinMicroseconds = 1500;
static void FrameLimiterHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    if (sod::gpu::FrameRate() != 30)
        return;
    static std::chrono::steady_clock::time_point s_lastFrame;
    const uint32_t cvar = PPC_LOAD_U32(kMaxFpsCVarSlot);
    if (cvar && s_lastFrame != std::chrono::steady_clock::time_point{})
    {
        // ICVar::GetIVal, the call the limiter makes itself.
        const uint32_t getIVal = PPC_LOAD_U32(PPC_LOAD_U32(cvar) + 8);
        const int32_t maxFps = int32_t(sod::kernel::CallGuest(getIVal, { cvar }));
        if (maxFps > 0)
        {
            const auto interval = std::chrono::milliseconds(int(1000.0f / (float(maxFps) + 0.5f)));
            const auto wake = s_lastFrame + interval - std::chrono::microseconds(kLimiterSpinMicroseconds);
            const auto now = std::chrono::steady_clock::now();
            if (wake > now)
                sod::kernel::ShortSleep(
                    uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(wake - now).count()));
        }
    }
    original(ctx, base);
    s_lastFrame = std::chrono::steady_clock::now();
}
GAME_HOOK_ONE(sub_830D0FF0, FrameLimiterHook)

// The game's save writer: r4 is the texture the render thread drew the save's
// thumbnail into (or null), which it locks to write its pixels into the save
// (scrnshot.bin). Resolves stay on the GPU, so it read zeros (black thumbnails):
// the texture is read back from the GPU first. Its engine texture's methods, as
// the writer uses them: +12 width, +16 height, +88 lock (level, rect, out
// {pitch, bits}, flags), +92 unlock (level). The thumbnail is 8888 (436x256).
constexpr uint32_t kThumbnailPixelSizeLog2 = 2;
constexpr uint32_t kThumbnailMaxBytes = 16 << 20;
static void SaveWriterHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    const uint32_t texture = ctx.r4.u32;
    const uint32_t vtable = texture && sod::gpu::XeniaActive() ? PPC_LOAD_U32(texture) : 0;
    // Only an engine texture's (recompiled) methods.
    uint32_t methods[4] = {};
    bool known = vtable != 0;
    const uint32_t offsets[4] = { 12, 16, 88, 92 };
    for (uint32_t i = 0; known && i < 4; ++i)
    {
        methods[i] = PPC_LOAD_U32(vtable + offsets[i]);
        known = sod::kernel::g_memory.FindFunction(methods[i]) != nullptr;
    }
    if (known)
    {
        const uint32_t width = uint32_t(sod::kernel::CallGuest(methods[0], { texture }));
        const uint32_t height = uint32_t(sod::kernel::CallGuest(methods[1], { texture }));
        // The out arguments (a RECT, 16 bytes, and {pitch, bits}, 8): at the
        // hooked function's entry nothing lives below the stack pointer, and
        // CallGuest's frames start 0x100 below it.
        const uint32_t rect = ctx.r1.u32 - 48;
        const uint32_t locked = ctx.r1.u32 - 24;
        PPC_STORE_U32(rect, 0);
        PPC_STORE_U32(rect + 4, 0);
        PPC_STORE_U32(rect + 8, width);
        PPC_STORE_U32(rect + 12, height);
        PPC_STORE_U32(locked, 0);
        PPC_STORE_U32(locked + 4, 0);
        sod::kernel::CallGuest(methods[2], { texture, 0, rect, locked, 0 });
        const uint32_t pitch = PPC_LOAD_U32(locked);
        const uint32_t bits = PPC_LOAD_U32(locked + 4);
        sod::kernel::CallGuest(methods[3], { texture, 0 });
        // Whole pages: with resolution scaling only whole 32x32 tiles (4 KB at
        // 32bpp) are read back, and a partial last one would be left out.
        const uint64_t bytes = (uint64_t(pitch) * height + 4095) & ~uint64_t(4095);
        if (bits && bytes && bytes <= kThumbnailMaxBytes &&
            !sod::gpu::ReadBackGpuWrittenMemory(bits, uint32_t(bytes), kThumbnailPixelSizeLog2))
            LOGI("Save thumbnail at {:08X}: nothing newer on the GPU, or not read back", bits);
    }
    original(ctx, base);
}
// Removed for SoD (Dragon Age: Origins addresses): GAME_HOOK(sub_82302150, tu6_sub_82332EA0, SaveWriterHook)

SOD_IMPORT_AS(VdQueryVideoMode, sod::imp::VdQueryVideoMode)
SOD_IMPORT_AS(VdQueryVideoFlags, sod::imp::VdQueryVideoFlags)
SOD_IMPORT_AS(VdGetCurrentDisplayInformation, sod::imp::VdGetCurrentDisplayInformation)
SOD_IMPORT_AS(VdGetCurrentDisplayGamma, sod::imp::VdGetCurrentDisplayGamma)
SOD_IMPORT_AS(VdSetDisplayMode, sod::imp::VdSetDisplayMode)
SOD_IMPORT_AS(VdInitializeEngines, sod::imp::VdInitializeEngines)
SOD_IMPORT_AS(VdShutdownEngines, sod::imp::VdShutdownEngines)
SOD_IMPORT_AS(VdEnableDisableClockGating, sod::imp::VdEnableDisableClockGating)
SOD_IMPORT_AS(VdSetGraphicsInterruptCallback, sod::imp::VdSetGraphicsInterruptCallback)
SOD_IMPORT_AS(VdInitializeRingBuffer, sod::imp::VdInitializeRingBuffer)
SOD_IMPORT_AS(VdEnableRingBufferRPtrWriteBack, sod::imp::VdEnableRingBufferRPtrWriteBack)
SOD_IMPORT_AS(VdGetSystemCommandBuffer, sod::imp::VdGetSystemCommandBuffer)
SOD_IMPORT_AS(VdSetSystemCommandBufferGpuIdentifierAddress, sod::imp::VdSetSystemCommandBufferGpuIdentifierAddress)
SOD_IMPORT_AS(VdInitializeScalerCommandBuffer, sod::imp::VdInitializeScalerCommandBuffer)
SOD_IMPORT_AS(VdCallGraphicsNotificationRoutines, sod::imp::VdCallGraphicsNotificationRoutines)
SOD_IMPORT_AS(VdIsHSIOTrainingSucceeded, sod::imp::VdIsHSIOTrainingSucceeded)
SOD_IMPORT_AS(VdPersistDisplay, sod::imp::VdPersistDisplay)
SOD_IMPORT_AS(VdRetrainEDRAMWorker, sod::imp::VdRetrainEDRAMWorker)
SOD_IMPORT_AS(VdRetrainEDRAM, sod::imp::VdRetrainEDRAM)
SOD_IMPORT_AS(VdSwap, sod::imp::VdSwap)

// While the temporal upscaler antialiases the 3D scene, it's rendered in one
// tile without the game's 2xMSAA: the second predicated tile and the
// multisampling only cost GPU time (a GPU-bound 4K frame 10.4 -> 9.3 ms).
// sub_82661640 gives the scene's tile count, and its tile rectangles at *r4:
// with 2xMSAA, 2 tiles, 1280x512 and 1280x208, at 82144D64 (8215E5A8 in Title
// Update 6). It's called every frame, sets the MSAA type global itself, and the
// engine makes its render targets from the result, so the scene can go back to
// 2xMSAA (the upscaler failing) from one frame to the next.
constexpr sod::kernel::GameAddress kSceneMsaaType{ 0x8316E35C, 0x8323E83C }; // D3DMULTISAMPLE_TYPE, 0: none
constexpr sod::kernel::GameAddress kSceneTileRects{ 0x82144D64, 0x8215E5A8 }; // D3DRECT {x1, y1, x2, y2}[2]
static void SceneTilesHook(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    sod::ui::ApplyEngineGraphicsOptions(base);
    const uint32_t rectsOut = ctx.r4.u32;
    original(ctx, base);
    if (ctx.r3.s32 != 2)
        return;
    const uint32_t rects = kSceneTileRects.Get();
    if (sod::gpu::XeniaActive() && sod::gpu::XeniaWantsSingleSampleScene())
    {
        PPC_STORE_U32(kSceneMsaaType.Get(), 0);
        PPC_STORE_U32(rects + 12, 720); // the first tile covers the screen
        PPC_STORE_U32(rectsOut, rects);
        ctx.r3.u64 = 1;
    }
    else
    {
        PPC_STORE_U32(rects + 12, 512);
    }
}
// Removed for SoD (Dragon Age: Origins addresses): GAME_HOOK(sub_82661640, tu6_sub_826955E8, SceneTilesHook)
