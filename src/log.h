#pragma once

#include <windows.h>

namespace logging
{
    enum class Level
    {
        info,
        warning,
        error
    };

    void init(HMODULE self, bool show_console, bool trace_enabled);
    void shutdown();
    void write(Level level, const char* area, const char* format, ...);
    void trace(const char* area, const char* format, ...);
}

#define LOG_INFO(area, ...) ::logging::write(::logging::Level::info, area, __VA_ARGS__)
#define LOG_WARN(area, ...) ::logging::write(::logging::Level::warning, area, __VA_ARGS__)
#define LOG_ERROR(area, ...) ::logging::write(::logging::Level::error, area, __VA_ARGS__)
#define LOG_TRACE(area, ...) ::logging::trace(area, __VA_ARGS__)

