// The native (EDRAM-free) renderer as the runtime's GPU, in place of Xenia's Vulkan GPU emulation
// (native-renderer/, from nfsmw-android). Chosen once at startup: config.toml [xenia]
// dao_gpu_renderer = "native"; Xenia stays the default. xenia_host_vulkan.cpp routes its entry
// points here when it is on. (SoD) XDroid360's android/runtime/native_host.h, SoD's namespace.
#pragma once

#include <cstdint>
#include <filesystem>

namespace xe
{
    class Memory;
    namespace ui
    {
        class GraphicsProvider;
        class Presenter;
        class WindowedAppContext;
    }
}

namespace sod::gpu::native
{
    // dao_gpu_renderer is "native".
    bool Requested();
    // Creates the renderer: its Vulkan provider and presenter (on the app context's UI thread,
    // if any), then its ring and vblank threads. Looks for nfsmw_shaders.nfsp in userRoot.
    bool Initialize(const std::filesystem::path& userRoot, xe::Memory* memory,
        xe::ui::WindowedAppContext* appContext);
    bool Active();
    void Shutdown();

    xe::ui::Presenter* Presenter();
    xe::ui::GraphicsProvider* Provider();

    void InitializeRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2);
    void EnableReadPointerWriteBack(uint32_t address, uint32_t blockSizeLog2);
    void SetInterruptCallback(uint32_t callback, uint32_t userData);
    // A GPU register write (0x7FC8xxxx) from the game.
    void WriteRegister(uint32_t address, uint32_t value);
    // A guest frame submitted (VdSwap).
    void FrameSubmitted();
}
