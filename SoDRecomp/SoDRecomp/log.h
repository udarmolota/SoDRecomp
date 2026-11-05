#pragma once

#include <fmt/core.h>
#include <string_view>

namespace sod::log
{
    enum class Level { Trace, Info, Warn, Error };

    void Init(const std::filesystem::path& path);
    void Write(Level level, std::string_view message);
    bool TraceEnabled();
}

#define LOGT(...) do { if (::sod::log::TraceEnabled()) ::sod::log::Write(::sod::log::Level::Trace, fmt::format(__VA_ARGS__)); } while (0)
#define LOGI(...) ::sod::log::Write(::sod::log::Level::Info, fmt::format(__VA_ARGS__))
#define LOGW(...) ::sod::log::Write(::sod::log::Level::Warn, fmt::format(__VA_ARGS__))
#define LOGE(...) ::sod::log::Write(::sod::log::Level::Error, fmt::format(__VA_ARGS__))
