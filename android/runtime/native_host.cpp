// The native renderer as the runtime's GPU (native_host.h): the services its ReXGlue-shaped code
// expects (native-renderer/compat/rex/host.h) on top of our kernel HLE - guest-attached host
// threads, the graphics interrupt callback, the GPU register range, the video mode - and the
// renderer's graphics system, driven the way xenia_host_vulkan.cpp drives Xenia's.
// (SoD) XDroid360's android/runtime/native_host.cpp (DAO's), with SoD's namespaces; the setting
// keeps DAO's name, dao_gpu_renderer, because the launcher writes that name for both games.
#include "stdafx.h"

#include "native_host.h"

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#include "gpu/xenia_host.h"
#include "kernel/thread.h"
#include "text.h"

#include "xenia/base/cvar.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"
#include "xenia/memory.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/windowed_app_context.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/host.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>

#include "nfsmw_nativo_sistema.h"

DEFINE_string(dao_gpu_renderer, "xenia",
    "The GPU: \"xenia\" (Xenia's Vulkan GPU emulation, the default) or \"native\" (the "
    "EDRAM-free native renderer, which needs the game's shader library nfsmw_shaders.nfsp in "
    "the user data folder). Read once at startup.",
    "GPU");

// The renderer's count of guest frames (native-renderer/compat/xdroid360_game_links.cpp).
extern std::atomic<uint64_t> g_nfsmw_fotogramas_juego;

namespace sod::gpu::native
{
    namespace
    {
        struct Mmio
        {
            uint32_t base = 0;
            uint32_t mask = 0;
            void* context = nullptr;
            rex::host::MmioWrite write = nullptr;
        };

        std::unique_ptr<rex::system::IGraphicsSystem> s_system;
        std::unique_ptr<rex::runtime::FunctionDispatcher> s_dispatcher;
        rex::system::KernelState s_kernelState;
        Mmio s_mmio;

        rex::host::Services MakeServices()
        {
            rex::host::Services services;
            services.start_guest_thread = [](const std::string& name, std::function<int()> body) {
                auto thread = std::make_shared<std::thread>([name, body = std::move(body)] {
                    sod::kernel::GuestThread::AttachHostThread(name.c_str());
                    body();
                });
                return std::function<void()>([thread] {
                    if (thread->joinable())
                        thread->join();
                });
            };
            services.call_interrupt = [](uint32_t callback, uint32_t userData, uint32_t source, uint32_t cpu) {
                sod::gpu::CallInterruptCallback(callback, userData, source, cpu);
            };
            services.map_mmio = [](uint32_t base, uint32_t mask, uint32_t, void* context, rex::host::MmioRead,
                                    rex::host::MmioWrite write) {
                // The game reads the registers as plain memory (the runtime pre-writes the ones it
                // polls); writes come through DaoGpuRegisterWrite -> WriteRegister.
                s_mmio = { base, mask, context, write };
                return true;
            };
            services.video_mode = [] { return rex::host::VideoMode{}; };
            return services;
        }

        // The renderer's own settings (its nfsmw_* and xdroid360_* rex cvars) from config.toml:
        // any "name = value" line whose name is one of them, in [xenia] (which the launcher keeps
        // verbatim when it rewrites the file) or in [nfsmw]. Xenia's loader logs them as unknown
        // Xenia options; that is harmless. Read once, before the renderer is created.
        void ApplyRendererSettings(const std::filesystem::path& userRoot)
        {
            // (SoD) What this game needs from the renderer, off by default there (Dragon Age does
            // not use any of it): its occlusion queries subtract consecutive sample count snapshots;
            // it draws its lights as 2_10_10_10_FLOAT and resolves the same EDRAM memory as
            // 2_10_10_10, decoding the 7e3 bits in its shaders; it restores its normals with a fetch
            // constant of type 0; its albedo textures are gamma. config.toml can still override them.
            for (const char* name : { "xdroid360_oclusion_acumulada", "xdroid360_alias_edram",
                                      "xdroid360_fetch_tipo_invalido", "xdroid360_texturas_gamma" })
            {
                if (!rex::cvar::GetFlagInfo(name) || !rex::cvar::SetFlagByName(name, "true"))
                    LOGW("Native renderer setting {}: not set (an older renderer?)", name);
            }
            // And one of Most Wanted's savings off: it leaves alpha-tested draws out of the shadow
            // maps (that game's trees and fences), and here the characters' clothes and the foliage
            // are alpha tested, so only a character's head cast a shadow.
            if (!rex::cvar::GetFlagInfo("nfsmw_sombras_sin_vegetacion") ||
                !rex::cvar::SetFlagByName("nfsmw_sombras_sin_vegetacion", "false"))
                LOGW("Native renderer setting nfsmw_sombras_sin_vegetacion: not set");
            std::ifstream file(userRoot / "config.toml");
            std::string line;
            bool inside = false;
            const auto trim = [](std::string_view s) {
                const size_t first = s.find_first_not_of(" \t\r");
                if (first == std::string_view::npos)
                    return std::string_view();
                return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
            };
            while (std::getline(file, line))
            {
                const std::string_view text = trim(line);
                if (text.empty() || text[0] == '#')
                    continue;
                if (text[0] == '[')
                {
                    inside = text.rfind("[xenia]", 0) == 0 || text.rfind("[nfsmw]", 0) == 0;
                    continue;
                }
                const size_t equals = text.find('=');
                if (!inside || equals == std::string_view::npos)
                    continue;
                const std::string name(trim(text.substr(0, equals)));
                std::string_view value = trim(text.substr(equals + 1));
                if (!value.empty() && value[0] == '"')
                {
                    const size_t close = value.find('"', 1);
                    value = value.substr(1, close == std::string_view::npos ? value.size() - 1 : close - 1);
                }
                else if (const size_t comment = value.find('#'); comment != std::string_view::npos)
                {
                    value = trim(value.substr(0, comment));
                }
                if (!rex::cvar::GetFlagInfo(name))
                    continue;
                if (rex::cvar::SetFlagByName(name, value))
                    LOGI("Native renderer setting: {} = {}", name, value);
                else
                    LOGW("Native renderer setting {}: '{}' not accepted", name, value);
            }
        }
    }

    bool Requested()
    {
        return cvars::dao_gpu_renderer == "native";
    }

    bool Initialize(const std::filesystem::path& userRoot, xe::Memory* memory, xe::ui::WindowedAppContext* appContext)
    {
        rex::host::SetServices(MakeServices());
        ApplyRendererSettings(userRoot);
        rex::filesystem::SetNativeRendererDataFolder(userRoot);
        s_system = nfsmw::nativo::CrearSistemaGrafico();
        s_dispatcher = std::make_unique<rex::runtime::FunctionDispatcher>(memory);
        if (!s_system || XFAILED(s_system->Setup(s_dispatcher.get(), &s_kernelState, appContext, true)))
        {
            LOGE("Native renderer: setup failed (see logcat, tag dao-xenia)");
            s_system.reset();
            s_dispatcher.reset();
            return false;
        }
        LOGI("Native renderer: on (shader library {})", Utf8(userRoot / "nfsmw_shaders.nfsp"));
        return true;
    }

    bool Active()
    {
        return s_system != nullptr;
    }

    void Shutdown()
    {
        if (s_system)
            s_system->Shutdown();
        s_system.reset();
        s_dispatcher.reset();
    }

    xe::ui::Presenter* Presenter()
    {
        return s_system ? s_system->presenter() : nullptr;
    }

    xe::ui::GraphicsProvider* Provider()
    {
        return s_system ? s_system->provider() : nullptr;
    }

    void InitializeRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2)
    {
        s_system->InitializeRingBuffer(physicalAddress, sizeLog2);
    }

    void EnableReadPointerWriteBack(uint32_t address, uint32_t blockSizeLog2)
    {
        s_system->EnableReadPointerWriteBack(address, blockSizeLog2);
    }

    void SetInterruptCallback(uint32_t callback, uint32_t userData)
    {
        s_system->SetInterruptCallback(callback, userData);
    }

    void WriteRegister(uint32_t address, uint32_t value)
    {
        if (s_mmio.write && (address & s_mmio.mask) == (s_mmio.base & s_mmio.mask))
            s_mmio.write(nullptr, s_mmio.context, address, value);
    }

    void FrameSubmitted()
    {
        g_nfsmw_fotogramas_juego.fetch_add(1, std::memory_order_relaxed);
        // The frame counter the launcher's fps display reads (nativeGetPresentedFrames), which
        // Xenia's GPU advances at each swap.
        ++xe::gpu::dao_trace::SwapCount();
    }
}
