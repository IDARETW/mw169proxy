#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>

namespace logging
{
    namespace
    {
        std::mutex output_mutex;
        HANDLE log_file = INVALID_HANDLE_VALUE;
        HANDLE trace_file = INVALID_HANDLE_VALUE;
        HANDLE console = INVALID_HANDLE_VALUE;

        const char* level_name(Level level)
        {
            switch (level)
            {
                case Level::warning: return "WARN";
                case Level::error: return "ERROR";
                default: return "INFO";
            }
        }

        bool make_path(HMODULE self, const wchar_t* name, wchar_t* output, std::size_t count)
        {
            if (!GetModuleFileNameW(self, output, static_cast<DWORD>(count))) return false;
            wchar_t* slash = wcsrchr(output, L'\\');
            if (!slash) return false;
            slash[1] = L'\0';
            return wcscat_s(output, count, name) == 0;
        }

        HANDLE open_log(HMODULE self, const wchar_t* name)
        {
            wchar_t path[MAX_PATH]{};
            if (!make_path(self, name, path, MAX_PATH)) return INVALID_HANDLE_VALUE;

            HANDLE file = CreateFileW(
                path,
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            return file;
        }

        void emit(HANDLE file, const char* text, DWORD size)
        {
            if (file == INVALID_HANDLE_VALUE || !text || size == 0) return;
            DWORD written = 0;
            WriteFile(file, text, size, &written, nullptr);
        }

        void format_line(
            char* output,
            std::size_t output_size,
            const char* prefix,
            const char* area,
            const char* format,
            va_list arguments)
        {
            SYSTEMTIME time{};
            GetLocalTime(&time);

            int used = _snprintf_s(
                output,
                output_size,
                _TRUNCATE,
                "%02u:%02u:%02u.%03u [%s] [%s] ",
                time.wHour,
                time.wMinute,
                time.wSecond,
                time.wMilliseconds,
                prefix,
                area ? area : "Core");
            if (used < 0) used = 0;

            const std::size_t remaining = output_size - static_cast<std::size_t>(used);
            const std::size_t body_size = remaining > 2 ? remaining - 2 : remaining;
            _vsnprintf_s(
                output + used,
                body_size,
                _TRUNCATE,
                format,
                arguments);
            strcat_s(output, output_size, "\r\n");
        }
    }

    void init(HMODULE self, bool show_console, bool trace_enabled)
    {
        std::lock_guard lock(output_mutex);
        log_file = open_log(self, L"mw169proxy.log");
        if (trace_enabled) trace_file = open_log(self, L"mw169proxy.trace.log");

        if (show_console)
        {
            if (AllocConsole()) SetConsoleTitleW(L"mw169proxy");
            console = GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }

    void shutdown()
    {
        std::lock_guard lock(output_mutex);
        if (log_file != INVALID_HANDLE_VALUE) CloseHandle(log_file);
        if (trace_file != INVALID_HANDLE_VALUE) CloseHandle(trace_file);
        log_file = INVALID_HANDLE_VALUE;
        trace_file = INVALID_HANDLE_VALUE;
    }

    void write(Level level, const char* area, const char* format, ...)
    {
        char line[2048]{};
        va_list arguments;
        va_start(arguments, format);
        format_line(line, sizeof(line), level_name(level), area, format, arguments);
        va_end(arguments);

        const DWORD size = static_cast<DWORD>(strlen(line));
        std::lock_guard lock(output_mutex);
        emit(log_file, line, size);
        emit(console, line, size);
        OutputDebugStringA(line);
    }

    void trace(const char* area, const char* format, ...)
    {
        if (trace_file == INVALID_HANDLE_VALUE) return;

        char line[2048]{};
        va_list arguments;
        va_start(arguments, format);
        format_line(line, sizeof(line), "TRACE", area, format, arguments);
        va_end(arguments);

        const DWORD size = static_cast<DWORD>(strlen(line));
        std::lock_guard lock(output_mutex);
        emit(trace_file, line, size);
    }

    void emergency(const char* format, ...)
    {
        if (!format) return;

        char line[8192]{};
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, arguments);
        va_end(arguments);
        strcat_s(line, sizeof(line), "\r\n");

        const DWORD size = static_cast<DWORD>(strlen(line));
        emit(log_file, line, size);
        emit(console, line, size);
        OutputDebugStringA(line);
    }
}
