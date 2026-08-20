#include "trigger.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>

namespace trigger
{
    namespace
    {
        using LoadImageA = HANDLE(WINAPI*)(HINSTANCE, LPCSTR, UINT, int, int, UINT);

        std::atomic<LoadImageA> original = nullptr;
        InstallFunction install_game_hooks = nullptr;
        std::uintptr_t main_base = 0;
        std::atomic<bool> complete = false;
        std::atomic<bool> installing = false;
        std::atomic<unsigned int> calls = 0;

        void try_install()
        {
            if (complete.load(std::memory_order_acquire)) return;

            bool expected = false;
            if (!installing.compare_exchange_strong(expected, true)) return;

            const unsigned int call = calls.fetch_add(1) + 1;
            if (install_game_hooks && install_game_hooks(main_base))
            {
                complete.store(true, std::memory_order_release);
                LOG_INFO("Trigger", "Game hooks installed on LoadImageA call %u", call);
            }
            else
            {
                LOG_TRACE("Trigger", "Game code was not ready on LoadImageA call %u", call);
            }
            installing.store(false, std::memory_order_release);
        }

        HANDLE WINAPI load_image_detour(
            HINSTANCE instance,
            LPCSTR name,
            UINT type,
            int width,
            int height,
            UINT flags)
        {
            try_install();
            LoadImageA next = nullptr;
            while (!(next = original.load(std::memory_order_acquire))) YieldProcessor();
            return next(instance, name, type, width, height, flags);
        }

        bool patch_main_import()
        {
            auto* base = reinterpret_cast<std::uint8_t*>(main_base);
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

            const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (!directory.VirtualAddress) return false;

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
            for (; descriptor->Name; ++descriptor)
            {
                const char* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
                if (_stricmp(module_name, "user32.dll") != 0) continue;
                if (!descriptor->OriginalFirstThunk) return false;

                auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
                auto* functions = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);

                for (std::size_t index = 0; names[index].u1.AddressOfData; ++index)
                {
                    if (IMAGE_SNAP_BY_ORDINAL64(names[index].u1.Ordinal)) continue;

                    auto* import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        base + names[index].u1.AddressOfData);
                    if (std::strcmp(reinterpret_cast<const char*>(import_name->Name), "LoadImageA") != 0)
                        continue;

                    auto* slot = reinterpret_cast<void**>(&functions[index].u1.Function);
                    DWORD old_protection = 0;
                    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection)) return false;
                    void* previous = InterlockedExchangePointer(
                        reinterpret_cast<void* volatile*>(slot),
                        reinterpret_cast<void*>(&load_image_detour));
                    if (!previous)
                        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), nullptr);
                    original.store(reinterpret_cast<LoadImageA>(previous), std::memory_order_release);
                    DWORD ignored = 0;
                    VirtualProtect(slot, sizeof(void*), old_protection, &ignored);
                    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
                    return previous != nullptr;
                }
            }
            return false;
        }
    }

    bool install(std::uintptr_t game_base, InstallFunction install_function)
    {
        main_base = game_base;
        install_game_hooks = install_function;

        if (!patch_main_import())
        {
            LOG_ERROR("Trigger", "The main executable does not import user32!LoadImageA by name");
            return false;
        }

        LOG_INFO("Trigger", "LoadImageA import hook installed");
        return true;
    }
}
