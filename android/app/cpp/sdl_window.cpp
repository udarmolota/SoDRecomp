#include "stdafx.h"

#include "sdl_window.h"

#include <android/native_window.h>

#include <chrono>
#include <dlfcn.h>
#include <unordered_map>

#include "gpu/video.h"
#include "input/input.h"
#include "kernel/game_exit.h"
#include "xenia/ui/surface_android.h"

namespace sod::app
{
    namespace
    {
        enum UserEventCode : Sint32
        {
            kExecutePendingFunctions = 1,
            kPaint = 2,
        };
    }

    // ------------------------------------------------------------------ app context

    SdlWindowedAppContext::SdlWindowedAppContext()
    {
        userEvent_ = SDL_RegisterEvents(1);
        wake_ = SDL_CreateSemaphore(0);
    }

    void SdlWindowedAppContext::Wake()
    {
        if (wake_)
            SDL_SignalSemaphore(wake_);
    }

    void SdlWindowedAppContext::NotifyUILoopOfPendingFunctions()
    {
        SDL_Event event{};
        event.type = userEvent_;
        event.user.code = kExecutePendingFunctions;
        SDL_PushEvent(&event);
        Wake();
    }

    void SdlWindowedAppContext::PlatformQuitFromUIThread()
    {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
        Wake();
    }

    void SdlWindowedAppContext::RequestPaint()
    {
        // One event per request burst.
        if (paintRequested_.exchange(true, std::memory_order_acq_rel))
            return;
        SDL_Event event{};
        event.type = userEvent_;
        event.user.code = kPaint;
        SDL_PushEvent(&event);
        Wake();
    }

    // The display's refresh fitted to the game's frame rate: at the game's own 30 fps cap,
    // 60 Hz rather than a phone's 120 (less power and heat; each frame shown twice), not 30,
    // so a frame that misses its 33 ms waits a 17 ms refresh, not another 33 ms. Any other
    // rate: the phone's choice (a fixed 120 can land on 60 on a 90 Hz panel). Android 11's
    // ANativeWindow_setFrameRate, looked up at run time (the app runs from Android 10);
    // checked once a second, as the rate can change in the game's Graphics tab.
    static void UpdateDisplayRate(SDL_Window* window)
    {
        using SetFrameRateFn = int32_t (*)(ANativeWindow*, float, int8_t);
        // From libandroid.so's own handle: dlsym(RTLD_DEFAULT) searches only the app's linker
        // namespace, where the system's libandroid.so (linked in from the default one) isn't
        // found. A public library, so dlopen just takes a reference to the loaded one.
        static const auto setFrameRate = []() -> SetFrameRateFn {
            void* const libandroid = dlopen("libandroid.so", RTLD_NOW);
            auto* const fn =
                libandroid ? reinterpret_cast<SetFrameRateFn>(dlsym(libandroid, "ANativeWindow_setFrameRate")) : nullptr;
            if (!fn)
                LOGI("Display refresh: no ANativeWindow_setFrameRate (Android 10, or {})",
                    libandroid ? "not in libandroid.so" : "no libandroid.so");
            return fn;
        }();
        static std::chrono::steady_clock::time_point s_next;
        static ANativeWindow* s_window = nullptr;
        static int s_rate = -1;
        static bool s_noWindowLogged = false;
        if (!setFrameRate)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now < s_next)
            return;
        s_next = now + std::chrono::seconds(1);
        auto* nativeWindow = static_cast<ANativeWindow*>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
        const int rate = sod::gpu::FrameRate(); // 0: unlimited
        if (!nativeWindow)
        {
            // Normal for the first moments (no surface yet), so only once.
            if (!s_noWindowLogged)
                LOGI("Display refresh: no window yet");
            s_noWindowLogged = true;
            return;
        }
        if (nativeWindow == s_window && rate == s_rate)
            return;
        // 30 fps: 60 Hz; anything else, or unlimited: the phone's own choice (0).
        const float hz = rate == 30 ? 60.0f : 0.0f;
        constexpr int8_t kFixedSource = 1; // ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE
        const int32_t result = setFrameRate(nativeWindow, hz, kFixedSource);
        LOGI("Display refresh: {} for {} fps (result {})", hz > 0 ? fmt::format("{} Hz requested", hz) : std::string("the phone's"),
            rate, result);
        s_window = nativeWindow;
        s_rate = rate;
    }

    void SdlWindowedAppContext::RunUILoop(SDL_Window* window, SdlWindow* xeWindow)
    {
        bool surfaceLost = false;
        // SOD_PROFILE: how often the loop wakes and with what (the SDL thread was seen
        // burning a core); counts logged every 20 s.
        const bool countEvents = getenv("SOD_PROFILE") != nullptr;
        std::unordered_map<uint32_t, uint32_t> eventCounts;
        uint32_t paintCount = 0, pendingCount = 0;
        auto lastReport = std::chrono::steady_clock::now();
        uint32_t wakeups = 0;
        for (;;)
        {
            SDL_Event event;
            UpdateDisplayRate(window);
            if (countEvents)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastReport >= std::chrono::seconds(20))
                {
                    std::string line;
                    for (const auto& [type, count] : eventCounts)
                        line += fmt::format(" {:#x}:{}", type, count);
                    LOGI("UI loop: {} wakeups, {} paints, {} pending-function wakes, events by type:{}", wakeups, paintCount,
                        pendingCount, line);
                    eventCounts.clear();
                    paintCount = pendingCount = 0;
                    wakeups = 0;
                    lastReport = now;
                }
            }
            // Everything queued (SDL pumps its Android lifecycle and input here), then a
            // nap: our own requests wake it at once, anything else waits for the next poll.
            if (!SDL_PollEvent(&event))
            {
                SDL_WaitSemaphoreTimeout(wake_, 16);
                wakeups++;
                continue;
            }
            if (countEvents)
                eventCounts[event.type]++;
            if (event.type == userEvent_)
            {
                if (event.user.code == kExecutePendingFunctions)
                {
                    if (countEvents)
                        pendingCount++;
                    ExecutePendingFunctionsFromUIThread();
                }
                else if (event.user.code == kPaint)
                {
                    if (countEvents)
                        paintCount++;
                    paintRequested_.store(false, std::memory_order_release);
                    if (xeWindow && !surfaceLost)
                        xeWindow->Paint(false);
                }
                if (HasQuitFromUIThread())
                    return;
                continue;
            }
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_TERMINATING:
                LOGI("Window closed, exiting");
                QuitFromUIThread();
                return;
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                // The activity's surface is about to go: disconnect the presenter first.
                LOGI("Entering the background: surface disconnected");
                surfaceLost = true;
                if (xeWindow)
                    xeWindow->OnSdlSurfaceLost();
                break;
            case SDL_EVENT_DID_ENTER_FOREGROUND:
                LOGI("Back in the foreground: surface reconnected");
                surfaceLost = false;
                if (xeWindow)
                {
                    xeWindow->OnSdlSizeChanged();
                    xeWindow->OnSdlSurfaceAvailable();
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
                if (xeWindow && !surfaceLost)
                    xeWindow->OnSdlSizeChanged();
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
                if (xeWindow && !surfaceLost)
                    xeWindow->Paint(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (xeWindow)
                    xeWindow->OnSdlFocus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
                sod::input::OnSdlEvent(event);
                break;
            default:
                break;
            }
            if (HasQuitFromUIThread())
                return;
        }
    }

    // ------------------------------------------------------------------ window

    SdlWindow::SdlWindow(SdlWindowedAppContext& appContext, SDL_Window* window)
        : Window(appContext, "Dragon Age: Origins", 1280, 720), window_(window)
    {
    }

    SdlWindow::~SdlWindow()
    {
        EnterDestructor();
    }

    uint32_t SdlWindow::GetLatestDpiImpl() const
    {
        const float scale = SDL_GetWindowDisplayScale(window_);
        return scale > 0 ? uint32_t(std::lround(scale * 160.0f)) : 160;
    }

    void SdlWindow::OnSdlSizeChanged()
    {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0)
            return;
        OnDesiredLogicalSizeUpdate(SizeToLogical(uint32_t(width)), SizeToLogical(uint32_t(height)));
        WindowDestructionReceiver destructionReceiver(this);
        OnActualSizeUpdate(uint32_t(width), uint32_t(height), WindowResizeAction::kManual, destructionReceiver);
    }

    void SdlWindow::OnSdlFocus(bool focused)
    {
        WindowDestructionReceiver destructionReceiver(this);
        OnFocusUpdate(focused, destructionReceiver);
    }

    bool SdlWindow::OpenImpl()
    {
        OnSdlSizeChanged();
        return true;
    }

    void SdlWindow::RequestCloseImpl()
    {
        WindowDestructionReceiver destructionReceiver(this);
        OnBeforeClose(destructionReceiver);
        if (destructionReceiver.IsWindowDestroyed())
            return;
        OnAfterClose();
    }

    std::unique_ptr<xe::ui::Surface> SdlWindow::CreateSurfaceImpl(xe::ui::Surface::TypeFlags allowedTypes)
    {
        if (!(allowedTypes & xe::ui::Surface::kTypeFlag_AndroidNativeWindow))
            return nullptr;
        auto* nativeWindow = static_cast<ANativeWindow*>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
        if (!nativeWindow)
        {
            LOGW("Window: no native window yet");
            return nullptr;
        }
        return std::make_unique<xe::ui::AndroidNativeWindowSurface>(nativeWindow);
    }

    void SdlWindow::RequestPaintImpl()
    {
        static_cast<SdlWindowedAppContext&>(app_context()).RequestPaint();
    }
}
