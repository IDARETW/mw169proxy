#include <windows.h>

#include <cstdint>

extern "C" __declspec(dllexport) std::uintptr_t WINAPI DiscordCreate(
    std::uintptr_t,
    std::uintptr_t,
    std::uintptr_t core_output,
    std::uintptr_t)
{
    void** core = reinterpret_cast<void**>(core_output);
    if (core) *core = nullptr;
    return 1; // DiscordResult_ServiceUnavailable.
}

extern "C" __declspec(dllexport) std::uintptr_t WINAPI DiscordVersion(
    std::uintptr_t,
    std::uintptr_t,
    std::uintptr_t,
    std::uintptr_t)
{
    return 0;
}

extern "C" __declspec(dllexport) std::uintptr_t WINAPI rust_eh_personality(
    std::uintptr_t,
    std::uintptr_t,
    std::uintptr_t,
    std::uintptr_t)
{
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, void*)
{
    return TRUE;
}
