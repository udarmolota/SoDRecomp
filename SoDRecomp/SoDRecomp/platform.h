#pragma once
// POSIX (Android, Linux) stand-ins for the few Win32 calls and types the runtime uses in
// passing: thread names, sleeps, ids, the process exit, FILETIME. Included by stdafx.h in
// place of <windows.h>. The pieces with real logic behind them (guest memory, host
// threads, file I/O, hashing, program start) have their own #ifdef _WIN32 branches.
#ifndef _WIN32

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

using HANDLE = void*;
using HMODULE = void*;
using DWORD = uint32_t;
using BOOL = int;
using LONG = int32_t;
using ULONG = uint32_t;
using UINT = unsigned int;
using LPCWSTR = const wchar_t*;
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(~uintptr_t(0)))
#define FALSE 0
#define TRUE 1
#define INFINITE 0xFFFFFFFFu
#define MAX_PATH 4096
#define CP_UTF8 65001

struct FILETIME
{
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
};

// 100 ns since 1601-01-01 from the realtime clock.
inline uint64_t PlatformSystemTime100ns()
{
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return uint64_t(ts.tv_sec) * 10000000ull + uint64_t(ts.tv_nsec) / 100 + 116444736000000000ull;
}

inline void GetSystemTimeAsFileTime(FILETIME* ft)
{
    const uint64_t t = PlatformSystemTime100ns();
    ft->dwLowDateTime = uint32_t(t);
    ft->dwHighDateTime = uint32_t(t >> 32);
}

inline void Sleep(uint32_t milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline void SwitchToThread()
{
    sched_yield();
}

inline uint32_t GetCurrentThreadId()
{
    return uint32_t(syscall(SYS_gettid));
}

inline uint32_t GetCurrentProcessId()
{
    return uint32_t(getpid());
}

// Handles only as tags: the current thread/process is always the subject.
inline HANDLE GetCurrentThread() { return nullptr; }
inline HANDLE GetCurrentProcess() { return nullptr; }

inline void SetThreadDescription(HANDLE, const wchar_t* name)
{
    char narrow[16] = {};
    size_t i = 0;
    for (; name[i] && i < sizeof(narrow) - 1; i++)
        narrow[i] = char(name[i]);
    pthread_setname_np(pthread_self(), narrow);
}

inline void SetThreadDescription(HANDLE, const char* name)
{
    char narrow[16] = {};
    strncpy(narrow, name, sizeof(narrow) - 1);
    pthread_setname_np(pthread_self(), narrow);
}

constexpr int THREAD_PRIORITY_TIME_CRITICAL = 15;
constexpr int THREAD_PRIORITY_HIGHEST = 2;
constexpr int THREAD_PRIORITY_ABOVE_NORMAL = 1;
constexpr int THREAD_PRIORITY_NORMAL = 0;
constexpr int THREAD_PRIORITY_BELOW_NORMAL = -1;
constexpr int THREAD_PRIORITY_LOWEST = -2;
constexpr int THREAD_PRIORITY_IDLE = -15;
constexpr uint32_t THREAD_SET_INFORMATION = 0x20;
inline BOOL SetThreadPriority(HANDLE, int) { return 1; }
inline HANDLE OpenThread(uint32_t, BOOL, uint32_t) { return nullptr; }
inline BOOL CloseHandle(HANDLE) { return 1; }

// A host register context, as the Windows stack walker takes; nothing to walk here.
struct CONTEXT {};

inline void _mm_pause()
{
#if defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("pause");
#endif
}

// std::atomic_ref arrived in libc++ 19; the NDK's is 18. The subset the kernel uses, on
// the compiler's atomic builtins, until the NDK catches up.
#include <atomic>
#if !defined(__cpp_lib_atomic_ref)
namespace std
{
    template <typename T>
    class atomic_ref
    {
    public:
        explicit atomic_ref(T& value) : p_(&value) {}
        atomic_ref(const atomic_ref&) = default;
        T load(memory_order order = memory_order_seq_cst) const noexcept { return __atomic_load_n(p_, Order(order)); }
        void store(T value, memory_order order = memory_order_seq_cst) const noexcept { __atomic_store_n(p_, value, Order(order)); }
        T exchange(T value, memory_order order = memory_order_seq_cst) const noexcept { return __atomic_exchange_n(p_, value, Order(order)); }
        bool compare_exchange_weak(T& expected, T desired, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_compare_exchange_n(p_, &expected, desired, true, Order(order), Order(Failure(order)));
        }
        bool compare_exchange_strong(T& expected, T desired, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_compare_exchange_n(p_, &expected, desired, false, Order(order), Order(Failure(order)));
        }
        T fetch_add(T value, memory_order order = memory_order_seq_cst) const noexcept { return __atomic_fetch_add(p_, value, Order(order)); }
        T fetch_sub(T value, memory_order order = memory_order_seq_cst) const noexcept { return __atomic_fetch_sub(p_, value, Order(order)); }
        T fetch_or(T value, memory_order order = memory_order_seq_cst) const noexcept { return __atomic_fetch_or(p_, value, Order(order)); }
        T fetch_and(T value, memory_order order = memory_order_seq_cst) const noexcept { return __atomic_fetch_and(p_, value, Order(order)); }
        operator T() const noexcept { return load(); }
        T operator=(T value) const noexcept { store(value); return value; }

    private:
        static constexpr int Order(memory_order order)
        {
            switch (order)
            {
            case memory_order_relaxed: return __ATOMIC_RELAXED;
            case memory_order_consume: return __ATOMIC_CONSUME;
            case memory_order_acquire: return __ATOMIC_ACQUIRE;
            case memory_order_release: return __ATOMIC_RELEASE;
            case memory_order_acq_rel: return __ATOMIC_ACQ_REL;
            default: return __ATOMIC_SEQ_CST;
            }
        }
        static constexpr memory_order Failure(memory_order order)
        {
            return order == memory_order_acq_rel ? memory_order_acquire
                 : order == memory_order_release ? memory_order_relaxed : order;
        }
        T* p_;
    };
}
#endif

[[noreturn]] inline void TerminateProcess(HANDLE, uint32_t exitCode)
{
    _exit(int(exitCode & 0xFF));
}

inline bool IsDebuggerPresent() { return false; }
inline void __debugbreak() {}

// Keyboard polling (input/input.cpp): no keyboard, no foreground window here.
enum { VK_RETURN = 0x0D, VK_TAB = 0x09, VK_SPACE = 0x20, VK_ESCAPE = 0x1B, VK_LSHIFT = 0xA0,
       VK_UP = 0x26, VK_DOWN = 0x28, VK_LEFT = 0x25, VK_RIGHT = 0x27 };
inline short GetAsyncKeyState(int) { return 0; }
inline HANDLE GetForegroundWindow() { return nullptr; }
inline uint32_t GetWindowThreadProcessId(HANDLE, uint32_t* process) { if (process) *process = 0; return 0; }
inline uint32_t GetLastError() { return uint32_t(errno); }
constexpr uint32_t ERROR_SUCCESS = 0;

inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline uint32_t _byteswap_ulong(uint32_t v) { return __builtin_bswap32(v); }
inline uint16_t _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
inline uint64_t _byteswap_uint64(uint64_t v) { return __builtin_bswap64(v); }

#include <cstdlib>
inline void* _aligned_malloc(size_t size, size_t alignment)
{
    void* p = nullptr;
    return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
}
inline void _aligned_free(void* p) { free(p); }

// Civil time <-> 100 ns FILETIME (UTC), as the Windows pair.
struct SYSTEMTIME
{
    uint16_t wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
    const uint64_t t = (uint64_t(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
    const int64_t unixSeconds = int64_t(t / 10000000ull) - 11644473600ll;
    const time_t seconds = time_t(unixSeconds);
    tm parts{};
    if (!gmtime_r(&seconds, &parts))
        return 0;
    st->wYear = uint16_t(parts.tm_year + 1900);
    st->wMonth = uint16_t(parts.tm_mon + 1);
    st->wDayOfWeek = uint16_t(parts.tm_wday);
    st->wDay = uint16_t(parts.tm_mday);
    st->wHour = uint16_t(parts.tm_hour);
    st->wMinute = uint16_t(parts.tm_min);
    st->wSecond = uint16_t(parts.tm_sec);
    st->wMilliseconds = uint16_t((t / 10000ull) % 1000);
    return 1;
}

inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft)
{
    tm parts{};
    parts.tm_year = int(st->wYear) - 1900;
    parts.tm_mon = int(st->wMonth) - 1;
    parts.tm_mday = st->wDay;
    parts.tm_hour = st->wHour;
    parts.tm_min = st->wMinute;
    parts.tm_sec = st->wSecond;
    const time_t seconds = timegm(&parts);
    if (seconds == time_t(-1))
        return 0;
    const uint64_t t = (uint64_t(int64_t(seconds) + 11644473600ll) * 10000000ull) + uint64_t(st->wMilliseconds) * 10000ull;
    ft->dwLowDateTime = uint32_t(t);
    ft->dwHighDateTime = uint32_t(t >> 32);
    return 1;
}

// fopen with a wide mode string, as the Windows code spells it (paths are narrow here).
inline FILE* _wfopen(const char* path, const wchar_t* mode)
{
    char narrow[8] = {};
    for (size_t i = 0; mode[i] && i < sizeof(narrow) - 1; i++)
        narrow[i] = char(mode[i]);
    return fopen(path, narrow);
}

#endif  // !_WIN32
