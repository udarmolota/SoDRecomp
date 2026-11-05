#include "stdafx.h"
#ifndef SOD_LAUNCHER_ONLY
#include "kernel/thread.h"
#endif

namespace sod::log
{
    static std::mutex s_mutex;
    static FILE* s_file;
    static std::atomic<bool> s_trace = getenv("SOD_TRACE") != nullptr;
    static const auto s_start = std::chrono::steady_clock::now();

    void Init(const std::filesystem::path& path)
    {
        s_file = _wfopen(path.c_str(), L"w");
        // SOD_TRACE_AFTER=<seconds>: import tracing from that moment on (a whole run's trace
        // is too much on a phone, and shifts the timing it is meant to show).
        if (const char* after = getenv("SOD_TRACE_AFTER"))
        {
            const int seconds = std::max(1, atoi(after));
            std::thread([seconds] {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                s_trace.store(true, std::memory_order_relaxed);
            }).detach();
        }
    }

    bool TraceEnabled()
    {
        return s_trace.load(std::memory_order_relaxed);
    }

    void Write(Level level, std::string_view message)
    {
        static constexpr const char* prefixes[] = { "T", "I", "W", "E" };
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start).count();
#ifdef SOD_LAUNCHER_ONLY
        const uint32_t threadId = 0;
#else
        auto* thread = kernel::GuestThread::Current();
        const uint32_t threadId = thread ? thread->threadId : 0;
#endif
        const auto line = fmt::format("[{:9.3f}] {} {:>3} | {}\n", t, prefixes[int(level)], threadId, message);

        std::lock_guard lock(s_mutex);
        fputs(line.c_str(), stdout);
        fflush(stdout);
        if (s_file)
        {
            fputs(line.c_str(), s_file);
            fflush(s_file);
        }
    }
}
