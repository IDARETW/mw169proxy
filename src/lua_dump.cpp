#include "lua_dump.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace lua_dump
{
    namespace
    {
        struct section_range
        {
            std::uintptr_t begin = 0;
            std::uintptr_t end = 0;
            bool executable = false;
            bool readable = false;
            bool writable = false;
        };

        struct lua_reg_entry
        {
            const char* name = nullptr;
            void* function = nullptr;
        };

        struct binding_candidate
        {
            std::uintptr_t name = 0;
            std::uintptr_t function = 0;
            std::string token;
        };

        std::atomic<bool> installed = false;
        std::atomic<game::LuaOpenLib> original_openlib = nullptr;
        std::mutex install_mutex;
        std::mutex snapshot_mutex;
        HANDLE snapshot_file = INVALID_HANDLE_VALUE;
        std::unordered_set<std::string> snapshot_entries;
        std::atomic<bool> snapshot_capture_enabled = false;

        std::atomic<bool> map_pack_override_seen = false;
        std::atomic<bool> install_state_override_seen = false;
        std::atomic<bool> app_ownership_override_seen = false;
        std::atomic<bool> mode_installation_override_seen = false;

        bool contains(const section_range& range, std::uintptr_t address, std::size_t size)
        {
            if (!range.readable || address < range.begin) return false;
            const auto end = address + size;
            return end >= address && end <= range.end;
        }

        bool contains_any(
            const std::vector<section_range>& ranges,
            std::uintptr_t address,
            std::size_t size,
            bool executable,
            bool readable)
        {
            for (const auto& range : ranges)
            {
                if (executable && !range.executable) continue;
                if (readable && !range.readable) continue;
                if (contains(range, address, size)) return true;
            }
            return false;
        }

        bool is_hash_token(const char* text, std::size_t& length)
        {
            length = 0;
            if (!text) return false;

            for (; length < 32; ++length)
            {
                const auto character = static_cast<unsigned char>(text[length]);
                if (character == 0) break;
                const bool uppercase = character >= 'A' && character <= 'Z';
                const bool digit = character >= '0' && character <= '9';
                if (!uppercase && !digit && character != '_') return false;
            }

            return length >= 6 && length < 32 && text[length] == 0;
        }

        bool read_hash_token(
            std::uintptr_t address,
            const std::vector<section_range>& ranges,
            std::string& output)
        {
            if (!contains_any(ranges, address, 1, false, true)) return false;

            char buffer[32]{};
            for (std::size_t index = 0; index < sizeof(buffer); ++index)
            {
                const auto current = reinterpret_cast<const void*>(address + index);
                if (!contains_any(ranges, reinterpret_cast<std::uintptr_t>(current), 1, false, true) ||
                    !safe_mem::read(current, buffer[index]))
                    return false;
                if (buffer[index] == 0) break;
            }

            std::size_t length = 0;
            if (!is_hash_token(buffer, length)) return false;
            output.assign(buffer, length);
            return true;
        }

        bool get_image_ranges(
            std::uintptr_t game_base,
            std::vector<section_range>& ranges)
        {
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game_base);
            if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                game_base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

            const auto* section = IMAGE_FIRST_SECTION(nt);
            for (unsigned short index = 0; index < nt->FileHeader.NumberOfSections; ++index)
            {
                const auto size = static_cast<std::size_t>(section[index].Misc.VirtualSize);
                if (!size) continue;

                ranges.push_back({
                    game_base + section[index].VirtualAddress,
                    game_base + section[index].VirtualAddress + size,
                    (section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0,
                    (section[index].Characteristics & IMAGE_SCN_MEM_READ) != 0,
                    (section[index].Characteristics & IMAGE_SCN_MEM_WRITE) != 0});
            }
            return !ranges.empty();
        }

        std::wstring snapshot_path()
        {
            wchar_t module_path[MAX_PATH]{};
            const auto length = GetModuleFileNameW(
                nullptr,
                module_path,
                static_cast<DWORD>(_countof(module_path)));
            if (!length) return L"mw169proxy_lui_bindings_169.tsv";

            std::wstring path(module_path, length);
            const auto slash = path.find_last_of(L"\\/");
            if (slash == std::wstring::npos)
                return L"mw169proxy_lui_bindings_169.tsv";

            path.resize(slash + 1);
            path.append(L"mw169proxy_lui_bindings_169.tsv");
            return path;
        }

        void prepare_snapshot()
        {
            if (!config::dump_lui_bindings && !config::dump_lui_registrations)
                return;

            const auto path = snapshot_path();
            const auto file = CreateFileW(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                LOG_WARN(
                    "Lua",
                    "Could not open the persistent LUI binding snapshot");
                return;
            }

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size))
            {
                CloseHandle(file);
                LOG_WARN(
                    "Lua",
                    "Could not query the persistent LUI binding snapshot");
                return;
            }

            if (size.QuadPart > 0)
            {
                CloseHandle(file);
                LOG_INFO(
                    "Lua",
                    "Persistent LUI binding snapshot found; full dump skipped");
                return;
            }

            constexpr char header[] =
                "# mw169proxy LUI binding snapshot\n"
                "# retail_build=1.69.0.26668155\n"
                "# fields=namespace\thash\tfunction_rva\tsource\n";
            DWORD written = 0;
            if (!WriteFile(file, header, sizeof(header) - 1, &written, nullptr) ||
                written != sizeof(header) - 1)
            {
                CloseHandle(file);
                LOG_WARN(
                    "Lua",
                    "Could not initialize the persistent LUI binding snapshot");
                return;
            }

            snapshot_file = file;
            snapshot_capture_enabled.store(true, std::memory_order_release);
            LOG_INFO(
                "Lua",
                "Persistent LUI binding snapshot capture started");
        }

        void record_snapshot_binding(
            const char* namespace_name,
            const char* entry_name,
            std::uintptr_t function_address,
            std::uintptr_t game_base,
            const char* source)
        {
            if (!snapshot_capture_enabled.load(std::memory_order_acquire) ||
                !namespace_name || !entry_name || !source)
                return;

            const auto function_rva = function_address >= game_base
                ? function_address - game_base
                : 0;
            char rva_text[32]{};
            _snprintf_s(
                rva_text,
                sizeof(rva_text),
                _TRUNCATE,
                "0x%zX",
                function_rva);

            std::lock_guard lock(snapshot_mutex);
            if (snapshot_file == INVALID_HANDLE_VALUE)
                return;

            std::string key = namespace_name;
            key.push_back('\t');
            key.append(entry_name);
            key.push_back('\t');
            key.append(rva_text);
            key.push_back('\t');
            key.append(source);
            if (!snapshot_entries.insert(key).second)
                return;

            key.push_back('\n');
            DWORD written = 0;
            if (!WriteFile(
                    snapshot_file,
                    key.data(),
                    static_cast<DWORD>(key.size()),
                    &written,
                    nullptr) ||
                written != key.size())
            {
                LOG_WARN(
                    "Lua",
                    "Persistent LUI binding snapshot write failed");
            }
        }

        void dump_bindings(std::uintptr_t game_base)
        {
            std::vector<section_range> ranges;
            if (!get_image_ranges(game_base, ranges))
            {
                LOG_WARN("Lua", "LUI binding scan skipped: invalid retail image");
                return;
            }

            std::vector<section_range> data_ranges;
            std::vector<section_range> code_ranges;
            for (const auto& range : ranges)
            {
                if (range.readable && !range.executable && !range.writable)
                    data_ranges.push_back(range);
                if (range.readable && range.executable) code_ranges.push_back(range);
            }

            std::unordered_set<std::uint64_t> seen;
            std::size_t candidates = 0;

            for (const auto& range : data_ranges)
            {
                if (range.end - range.begin < 16) continue;

                for (auto address = range.begin; address + 16 <= range.end; address += 8)
                {
                    std::uintptr_t name_pointer = 0;
                    std::uintptr_t function_pointer = 0;
                    std::memcpy(&name_pointer, reinterpret_cast<const void*>(address),
                                sizeof(name_pointer));
                    std::memcpy(&function_pointer, reinterpret_cast<const void*>(address + 8),
                                sizeof(function_pointer));

                    std::string token;
                    if (!read_hash_token(name_pointer, ranges, token) ||
                        !contains_any(code_ranges, function_pointer, 1, true, true))
                        continue;

                    const auto key = static_cast<std::uint64_t>(name_pointer) ^
                                     (static_cast<std::uint64_t>(function_pointer) >> 4);
                    if (!seen.insert(key).second) continue;

                    ++candidates;
                    record_snapshot_binding(
                        "<static>",
                        token.c_str(),
                        function_pointer,
                        game_base,
                        "static");
                }
            }

            LOG_INFO(
                "Lua",
                "LUI binding scan completed: %zu candidates; snapshot=\"%ls\"",
                candidates,
                L"mw169proxy_lui_bindings_169.tsv");
        }

        bool prologue_matches(const void* target, const std::uint8_t* expected, std::size_t size)
        {
            std::uint8_t current[32]{};
            if (!target || size > sizeof(current)) return false;
            return safe_mem::read_bytes(target, current, size) &&
                   std::memcmp(current, expected, size) == 0;
        }

        void capture_registration(
            std::uintptr_t game_base,
            const char* library,
            const lua_reg_entry& entry)
        {
            if (!entry.name || !entry.function) return;

            char library_name[128]{};
            char entry_name[128]{};
            if (library)
                safe_mem::read_string(library, library_name, sizeof(library_name));
            if (!safe_mem::read_string(entry.name, entry_name, sizeof(entry_name)) ||
                !entry_name[0])
                return;

            const auto function_address = reinterpret_cast<std::uintptr_t>(entry.function);
            const char* namespace_name = library_name[0] ? library_name : "<anonymous>";
            record_snapshot_binding(
                namespace_name,
                entry_name,
                function_address,
                game_base,
                "registration");
        }

        void push_boolean(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushBoolean>(
                base + game::lua_push_boolean_rva);
            push(lua_state, value);
        }

        void push_integer(void* lua_state, int value)
        {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;

            const auto push = reinterpret_cast<game::LuaPushInteger>(
                base + game::lua_push_integer_rva);
            push(lua_state, value);
        }

        int __fastcall map_pack_owned_override(void* lua_state)
        {
            if (!map_pack_override_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Lua", "Engine.BAJHDFAJJF local map-pack ownership reported true");
            push_boolean(lua_state, 1);
            return 1;
        }

        int __fastcall install_state_override(void* lua_state)
        {
            if (!install_state_override_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Lua", "Engine.BJCCIACEHG local install state reported installed/idle");
            push_integer(lua_state, 1);
            push_integer(lua_state, 0);
            return 2;
        }

        int __fastcall app_ownership_override(void* lua_state)
        {
            if (!app_ownership_override_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Lua", "Engine.BAJIGJADFI local app ownership reported true");
            push_boolean(lua_state, 1);
            return 1;
        }

        int __fastcall mode_installation_override(void* lua_state)
        {
            if (!mode_installation_override_seen.exchange(true, std::memory_order_acq_rel))
                LOG_INFO("Lua", "Engine.EDHEFGEJA local mode installation reported true");
            push_boolean(lua_state, 1);
            return 1;
        }

        void* local_content_override(const char* library, const char* entry_name)
        {
            if (!config::override_local_content_bindings ||
                !library || !entry_name || std::strcmp(library, "Engine") != 0)
                return nullptr;

            if (std::strcmp(entry_name, "BAJHDFAJJF") == 0)
                return reinterpret_cast<void*>(&map_pack_owned_override);
            if (std::strcmp(entry_name, "BJCCIACEHG") == 0)
                return reinterpret_cast<void*>(&install_state_override);
            if (std::strcmp(entry_name, "BAJIGJADFI") == 0)
                return reinterpret_cast<void*>(&app_ownership_override);
            if (std::strcmp(entry_name, "EDHEFGEJA") == 0)
                return reinterpret_cast<void*>(&mode_installation_override);
            return nullptr;
        }

        void __fastcall openlib_detour(
            void* lua_state,
            const char* library,
            void* raw_entries,
            unsigned int nup)
        {
            constexpr std::size_t maximum_entries = 16384;
            const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            auto* entries = static_cast<const lua_reg_entry*>(raw_entries);
            char library_name[128]{};
            if (library)
                safe_mem::read_string(library, library_name, sizeof(library_name));

            std::vector<lua_reg_entry> registration_entries;
            const bool can_override =
                config::override_local_content_bindings &&
                std::strcmp(library_name, "Engine") == 0;
            bool replaced_entry = false;
            bool terminated = false;

            if (entries)
            {
                for (std::size_t index = 0; index < maximum_entries; ++index)
                {
                    lua_reg_entry entry{};
                    const auto address = entries + index;
                    if (!safe_mem::read(address, entry)) break;
                    if (!entry.name)
                    {
                        terminated = true;
                        break;
                    }
                    if (config::dump_lui_registrations)
                        capture_registration(game_base, library, entry);

                    if (can_override)
                    {
                        char entry_name[128]{};
                        if (safe_mem::read_string(
                                entry.name,
                                entry_name,
                                sizeof(entry_name)))
                        {
                            if (auto* replacement = local_content_override(
                                    library_name,
                                    entry_name))
                            {
                                entry.function = replacement;
                                replaced_entry = true;
                                LOG_INFO(
                                    "Lua",
                                    "LUI registration override Engine.%s replacement=%p",
                                    entry_name,
                                    replacement);
                            }
                        }
                    }

                    if (can_override)
                        registration_entries.push_back(entry);
                }
            }

            auto original = original_openlib.load(std::memory_order_acquire);
            if (!original) return;

            if (replaced_entry && terminated)
            {
                registration_entries.push_back({nullptr, nullptr});
                original(lua_state, library, registration_entries.data(), nup);
                return;
            }

            original(lua_state, library, raw_entries, nup);
        }

        bool install_registration_hook(std::uintptr_t game_base)
        {
            if (!config::dump_lui_registrations &&
                !config::override_local_content_bindings)
                return true;
            if (original_openlib.load(std::memory_order_acquire)) return true;

            auto* target = reinterpret_cast<void*>(game_base + game::lua_openlib_rva);
            if (!prologue_matches(
                    target,
                    game::lua_openlib_prologue,
                    sizeof(game::lua_openlib_prologue)))
            {
                LOG_WARN(
                    "Lua",
                    "luaL_openlib hook skipped: retail prologue mismatch at %p",
                    target);
                return false;
            }

            auto* trampoline = hook::install(target, reinterpret_cast<void*>(&openlib_detour));
            if (!trampoline)
            {
                LOG_WARN("Lua", "luaL_openlib hook installation failed at %p", target);
                return false;
            }

            original_openlib.store(
                reinterpret_cast<game::LuaOpenLib>(trampoline),
                std::memory_order_release);
            LOG_INFO("Lua", "luaL_openlib hook installed at %p", target);
            return true;
        }

        DWORD WINAPI static_dump_worker(void* parameter)
        {
            dump_bindings(reinterpret_cast<std::uintptr_t>(parameter));
            return 0;
        }

        void start_static_dump(std::uintptr_t game_base)
        {
            if (!config::dump_lui_bindings ||
                !snapshot_capture_enabled.load(std::memory_order_acquire))
                return;

            auto* thread = CreateThread(
                nullptr,
                0,
                &static_dump_worker,
                reinterpret_cast<void*>(game_base),
                0,
                nullptr);
            if (thread)
            {
                CloseHandle(thread);
                LOG_INFO("Lua", "Read-only LUI binding scan started on a worker");
            }
            else
            {
                LOG_WARN("Lua", "Could not start the LUI binding scan worker");
            }
        }
    }

    void init()
    {
        LOG_INFO(
            "Lua",
            config::dump_lui_bindings
                ? "LUI binding dump is enabled"
                : "LUI binding dump is disabled");
        LOG_INFO(
            "Lua",
            "Local content registration overrides are %s",
            config::override_local_content_bindings ? "enabled" : "disabled");
        LOG_INFO(
            "Lua",
            "Persistent binding snapshot: mw169proxy_lui_bindings_169.tsv");
    }

    bool install(std::uintptr_t game_base)
    {
        if (installed.load(std::memory_order_acquire)) return true;
        if (!game_base)
        {
            installed.store(true, std::memory_order_release);
            return true;
        }

        std::lock_guard lock(install_mutex);
        if (installed.load(std::memory_order_relaxed)) return true;

        prepare_snapshot();
        (void)install_registration_hook(game_base);
        start_static_dump(game_base);
        installed.store(true, std::memory_order_release);
        return true;
    }
}
