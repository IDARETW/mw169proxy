#include "trigger.h"

#include "config.h"
#include "hook.h"
#include "log.h"

#include "../thirdparty/cwhook/src/entry.h"

#include <windows.h>

#include <atomic>
#include <cstddef>

namespace trigger
{
    namespace
    {
        using LoadImageA = HANDLE(WINAPI*)(HINSTANCE, LPCSTR, UINT, int, int, UINT);
        using SetThreadAffinityMaskFn = DWORD_PTR(WINAPI*)(HANDLE, DWORD_PTR);

        std::atomic<LoadImageA> original_load_image = nullptr;
        std::atomic<SetThreadAffinityMaskFn> original_affinity = nullptr;
        InstallFunction install_game_hooks = nullptr;
        std::uintptr_t main_base = 0;
        std::atomic<bool> hooks_complete = false;
        std::atomic<bool> installing_hooks = false;
        std::atomic<bool> arxan_installed = false;
        std::atomic<bool> installing_arxan = false;
        std::atomic<unsigned int> load_image_calls = 0;

        void try_install_game_hooks()
        {
            if (hooks_complete.load(std::memory_order_acquire)) return;

            bool expected = false;
            if (!installing_hooks.compare_exchange_strong(expected, true)) return;

            const unsigned int call = load_image_calls.fetch_add(1) + 1;
            if (call <= 3 || (call % 100) == 0)
                LOG_INFO("Trigger", "LoadImageA #%u: checking offline hooks", call);

            // This is the first safe edge for scanning the decrypted game
            // text. Arm checksum healing before any retail code patch.
            if (config::install_cwhook)
                (void)arxan::InstallHealers();

            if (install_game_hooks && install_game_hooks(main_base))
            {
                hooks_complete.store(true, std::memory_order_release);
                LOG_INFO("Trigger", "Game hooks installed on LoadImageA call %u", call);
            }
            else
            {
                if (call <= 3 || (call % 100) == 0)
                    LOG_INFO("Trigger", "LoadImageA #%u: retail code is not ready", call);
            }
            installing_hooks.store(false, std::memory_order_release);
        }

        void try_install_arxan()
        {
            if (arxan_installed.load(std::memory_order_acquire)) return;

            bool expected = false;
            if (!installing_arxan.compare_exchange_strong(expected, true)) return;

            arxan::Install();
            arxan_installed.store(true, std::memory_order_release);
            LOG_INFO("Trigger", "Arxan install ran on SetThreadAffinityMask");
            installing_arxan.store(false, std::memory_order_release);
        }

        HANDLE WINAPI load_image_detour(
            HINSTANCE instance,
            LPCSTR name,
            UINT type,
            int width,
            int height,
            UINT flags)
        {
            try_install_game_hooks();
            LoadImageA next = nullptr;
            while (!(next = original_load_image.load(std::memory_order_acquire))) YieldProcessor();
            return next(instance, name, type, width, height, flags);
        }

        DWORD_PTR WINAPI affinity_detour(HANDLE thread, DWORD_PTR mask)
        {
            try_install_arxan();
            SetThreadAffinityMaskFn next = nullptr;
            while (!(next = original_affinity.load(std::memory_order_acquire))) YieldProcessor();
            return next(thread, mask);
        }
    }

    bool install(
        std::uintptr_t game_base,
        InstallFunction install_function,
        bool install_cwhook_trigger)
    {
        main_base = game_base;
        install_game_hooks = install_function;

        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32)
        {
            LOG_ERROR("Trigger", "user32.dll is not loaded");
            return false;
        }

        auto* target = reinterpret_cast<void*>(GetProcAddress(user32, "LoadImageA"));
        if (!target)
        {
            LOG_ERROR("Trigger", "user32!LoadImageA was not found");
            return false;
        }

        void* trampoline = hook::install(target, reinterpret_cast<void*>(&load_image_detour));
        if (!trampoline)
        {
            LOG_ERROR("Trigger", "Could not inline-hook user32!LoadImageA at %p", target);
            return false;
        }

        original_load_image.store(reinterpret_cast<LoadImageA>(trampoline), std::memory_order_release);
        LOG_INFO("Trigger", "LoadImageA inline hook installed; waiting for engine initialization");

        if (!install_cwhook_trigger)
            return true;

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32)
        {
            LOG_ERROR("Trigger", "kernel32.dll is not loaded");
            return false;
        }

        auto* affinity_target = reinterpret_cast<void*>(
            GetProcAddress(kernel32, "SetThreadAffinityMask"));
        if (!affinity_target)
        {
            LOG_ERROR("Trigger", "kernel32!SetThreadAffinityMask was not found");
            return false;
        }

        void* affinity_trampoline = hook::install(
            affinity_target,
            reinterpret_cast<void*>(&affinity_detour));
        if (!affinity_trampoline)
        {
            LOG_ERROR(
                "Trigger",
                "Could not inline-hook kernel32!SetThreadAffinityMask at %p",
                affinity_target);
            return false;
        }

        original_affinity.store(
            reinterpret_cast<SetThreadAffinityMaskFn>(affinity_trampoline),
            std::memory_order_release);
        LOG_INFO("Trigger", "SetThreadAffinityMask inline hook installed");
        return true;
    }
}
