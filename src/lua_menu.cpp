#include "lua_menu.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "lua_errors.h"
#include "safe_mem.h"

#include "lua_menu_embedded.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace lua_menu
{
    namespace
    {
        game::LuaLoadfileFastfile original_loader = nullptr;
        std::uintptr_t game_base = 0;
        std::atomic<bool> installed = false;
        std::atomic<unsigned int> logged_replacements = 0;

        bool prologue_matches(
            const void* target,
            const std::uint8_t* expected,
            std::size_t size)
        {
            std::uint8_t current[32]{};
            return size <= sizeof(current) &&
                   safe_mem::read_bytes(target, current, size) &&
                   std::memcmp(current, expected, size) == 0;
        }

        bool executable(const void* address)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!address || !VirtualQuery(address, &info, sizeof(info))) return false;
            if (info.State != MEM_COMMIT) return false;
            const DWORD protection = info.Protect & 0xFF;
            return protection == PAGE_EXECUTE ||
                   protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE ||
                   protection == PAGE_EXECUTE_WRITECOPY;
        }

        void copy_name(const char* source, char* destination, std::size_t size)
        {
            destination[0] = '\0';
            if (!source || size == 0) return;
            safe_mem::read_string(source, destination, size);
            for (char* cursor = destination; *cursor; ++cursor)
            {
                if (*cursor == '\\') *cursor = '/';
                if (*cursor >= 'A' && *cursor <= 'Z')
                    *cursor = static_cast<char>(*cursor - 'A' + 'a');
            }
            while (destination[0] == '@' || destination[0] == '=')
                std::memmove(destination, destination + 1, std::strlen(destination));
        }

        const lua_menu_embedded::Blob* replacement_for(const char* filename)
        {
            char name[256]{};
            copy_name(filename, name, sizeof(name));
            if (std::strcmp(name, "ui/frontend/mp/mpplaymenu.lua") == 0)
                return &lua_menu_embedded::mp_play_menu;
            if (std::strcmp(name, "ui/frontend/mp/mpplaymenubuttons.lua") == 0)
                return &lua_menu_embedded::mp_play_menu_buttons;
            return nullptr;
        }

        std::uint8_t key_byte(std::uint8_t seed, std::size_t index)
        {
            return static_cast<std::uint8_t>(seed + index * 31u + 0x5Bu);
        }

        char* decode(
            const lua_menu_embedded::Blob& blob,
            std::size_t& size)
        {
            size = blob.size;
            auto* output = static_cast<char*>(std::malloc(size + 1));
            if (!output) return nullptr;
            for (std::size_t index = 0; index < size; ++index)
                output[index] = static_cast<char>(
                    blob.data[index] ^ key_byte(blob.seed, index));
            output[size] = '\0';
            return output;
        }

        int __fastcall loader_detour(void* lua_state, const char* filename)
        {
            const auto* blob = replacement_for(filename);
            if (blob && lua_state)
            {
                auto load_buffer = reinterpret_cast<game::LuaLoadBuffer>(
                    game_base + game::lua_loadbuffer_rva);
                if (executable(reinterpret_cast<const void*>(load_buffer)))
                {
                    std::size_t size = 0;
                    char* source = decode(*blob, size);
                    if (source)
                    {
                        const int result = load_buffer(lua_state, source, size, filename);
                        std::free(source);
                        if (result == 0)
                        {
                            const unsigned int bit =
                                (blob == &lua_menu_embedded::mp_play_menu) ? 1u : 2u;
                            const unsigned int previous =
                                logged_replacements.fetch_or(bit, std::memory_order_relaxed);
                            if ((previous & bit) == 0)
                            {
                                LOG_INFO(
                                    "LuaMenu",
                                    "Replaced %s with the local Private Match/Trials menu source (%zu bytes)",
                                    filename ? filename : "?",
                                    size);
                            }
                            return 0;
                        }
                        lua_errors::capture_compile_error(lua_state, filename, result);
                        LOG_WARN(
                            "LuaMenu",
                            "Replacement compile failed for %s; keeping the retail chunk",
                            filename ? filename : "?");
                    }
                }
                else
                {
                    LOG_WARN("LuaMenu", "luaL_loadbuffer is not executable at the 1.69 RVA");
                }
            }

            return original_loader(lua_state, filename);
        }
    }

    void init()
    {
        LOG_INFO(
            "LuaMenu",
            config::hook_lua_mp_menu
                ? "1.69 local MP menu replacement is enabled"
                : "1.69 local MP menu replacement is disabled");
    }

    bool install(std::uintptr_t base)
    {
        if (!config::hook_lua_mp_menu) return true;
        if (installed.load(std::memory_order_acquire)) return true;
        if (!base) return false;

        auto* target = reinterpret_cast<void*>(
            base + game::lua_loadfile_fastfile_rva);
        if (!prologue_matches(
                target,
                game::lua_loadfile_fastfile_prologue,
                sizeof(game::lua_loadfile_fastfile_prologue)))
        {
            LOG_TRACE(
                "LuaMenu",
                "1.69 luaL_loadfile_FastFile prologue is not ready at %p",
                target);
            return false;
        }

        void* trampoline = hook::install(target, reinterpret_cast<void*>(&loader_detour));
        if (!trampoline)
        {
            LOG_ERROR("LuaMenu", "Could not install the 1.69 Lua loader hook at %p", target);
            return false;
        }

        game_base = base;
        original_loader = reinterpret_cast<game::LuaLoadfileFastfile>(trampoline);
        installed.store(true, std::memory_order_release);
        LOG_INFO(
            "LuaMenu",
            "1.69 Lua loader hook installed at %p; only MPPlayMenu and MPPlayMenuButtons are replaced",
            target);
        return true;
    }
}
