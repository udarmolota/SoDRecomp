#pragma once

namespace sod::gpu
{
    struct VideoState
    {
        uint32_t displayWidth = 1280;
        uint32_t displayHeight = 720;

        uint32_t interruptCallback = 0;
        uint32_t interruptUserData = 0;

        uint32_t ringBufferBase = 0;
        uint32_t ringBufferSizeLog2 = 0;
        uint32_t readPointerWriteBack = 0;
        uint32_t writeBackBlockSizeLog2 = 0;
        uint32_t gpuIdentifierAddress = 0;

        std::atomic<uint64_t> swapCount{ 0 };
    };

    extern VideoState g_video;

    // The display mode reported to the game (VdQueryVideoMode, the display information):
    // another than 1280x720 before the game starts, e.g. a wider one the game projects
    // its scene at (the host then shows the frame at that aspect ratio). Before LoadXex.
    void SetDisplayMode(uint32_t width, uint32_t height);

    // The frame rate, one of kFrameRates (video.frame_rate, or as set in the
    // Graphics tab).
    int FrameRate();
    void SetFrameRate(int frameRate);
    // Switches between the game's own 30 fps and the last other rate (60 if none;
    // the F10 hotkey).
    void ToggleGameFrameRate();
    // Frame pacing above 60 fps (PaceFrames): VdSwap calls FrameSwapped, a write of
    // the ring buffer's write pointer FrameKicked.
    void FrameSwapped();
    void FrameKicked();
}
