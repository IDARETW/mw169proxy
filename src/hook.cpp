#include "hook.h"

#include "log.h"

#include <polyhook2/Detour/x64Detour.hpp>

#include <windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace hook
{
    namespace
    {
        std::mutex registry_mutex;
        std::vector<std::unique_ptr<PLH::x64Detour>> registry;
    }

    extern "C" __declspec(noinline) void* install(void* target, void* replacement)
    {
        if (!target || !replacement) return nullptr;

        std::uint64_t trampoline = 0;
        auto detour = std::make_unique<PLH::x64Detour>(
            reinterpret_cast<std::uint64_t>(target),
            reinterpret_cast<std::uint64_t>(replacement),
            &trampoline);

        if (!detour->hook() || !trampoline)
        {
            LOG_ERROR("Hook", "PolyHook2 could not patch %p", target);
            return nullptr;
        }

        std::lock_guard lock(registry_mutex);
        registry.push_back(std::move(detour));
        return reinterpret_cast<void*>(trampoline);
    }
}
