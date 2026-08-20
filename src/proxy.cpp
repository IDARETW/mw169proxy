#include "proxy.h"

#include "log.h"

#include <cstdint>
#include <cwchar>
#include <mutex>

namespace
{
    using DiscordCreateFunction = int(WINAPI*)(std::uint32_t, void*, void**);
    using GenericFunction = std::uintptr_t(WINAPI*)(
        std::uintptr_t,
        std::uintptr_t,
        std::uintptr_t,
        std::uintptr_t);

    struct RealDiscord
    {
        HMODULE module = nullptr;
        DiscordCreateFunction create = nullptr;
        GenericFunction version = nullptr;
        GenericFunction personality = nullptr;
    };

    HMODULE self_module = nullptr;
    RealDiscord real;
    std::once_flag load_once;

    void load_original()
    {
        std::call_once(load_once, []
        {
            wchar_t path[MAX_PATH]{};
            if (!self_module || !GetModuleFileNameW(self_module, path, MAX_PATH)) return;

            wchar_t* slash = wcsrchr(path, L'\\');
            if (!slash) return;
            slash[1] = L'\0';
            if (wcscat_s(path, MAX_PATH, L"discord_game_sdks.dll") != 0) return;

            real.module = LoadLibraryW(path);
            if (!real.module)
            {
                LOG_ERROR("Proxy", "discord_game_sdks.dll is absent or invalid");
                return;
            }

            real.create = reinterpret_cast<DiscordCreateFunction>(
                GetProcAddress(real.module, "DiscordCreate"));
            real.version = reinterpret_cast<GenericFunction>(GetProcAddress(real.module, "DiscordVersion"));
            real.personality = reinterpret_cast<GenericFunction>(
                GetProcAddress(real.module, "rust_eh_personality"));

            const int count = (real.create ? 1 : 0) + (real.version ? 1 : 0) +
                              (real.personality ? 1 : 0);
            LOG_INFO("Proxy", "Loaded the original Discord DLL with %d of 3 exports", count);
        });
    }
}

namespace proxy
{
    void set_module(HMODULE self)
    {
        self_module = self;
    }

    void preload()
    {
        load_original();
    }
}

extern "C" __declspec(dllexport) int WINAPI DiscordCreate(
    std::uint32_t version,
    void* parameters,
    void** core)
{
    load_original();
    if (real.create) return real.create(version, parameters, core);
    if (core) *core = nullptr;
    return 1; // DiscordResult_ServiceUnavailable.
}

#define FORWARD_EXPORT(export_name, member_name)                                           \
    extern "C" __declspec(dllexport) std::uintptr_t WINAPI export_name(                   \
        std::uintptr_t first,                                                               \
        std::uintptr_t second,                                                              \
        std::uintptr_t third,                                                               \
        std::uintptr_t fourth)                                                              \
    {                                                                                       \
        load_original();                                                                    \
        return real.member_name ? real.member_name(first, second, third, fourth) : 0;       \
    }

FORWARD_EXPORT(DiscordVersion, version)
FORWARD_EXPORT(rust_eh_personality, personality)

#undef FORWARD_EXPORT
