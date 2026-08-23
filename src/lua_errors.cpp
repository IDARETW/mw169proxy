#include "lua_errors.h"

#include "config.h"
#include "game.h"
#include "hook.h"
#include "log.h"
#include "safe_mem.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lua_errors
{
    namespace
    {
        constexpr std::uint32_t lua_exception_base = 0xE24C4A00u;
        constexpr std::size_t max_lua_slots = 64;
        constexpr std::size_t max_native_frames = 32;

        std::atomic<bool> veh_installed = false;
        std::atomic<bool> panic_hook_installed = false;
        std::atomic<std::uintptr_t> original_atpanic = 0;
        std::atomic<std::uintptr_t> original_panic = 0;
        std::uintptr_t game_base = 0;
        std::uintptr_t game_end = 0;
        using RtlCaptureStackBackTraceFn = USHORT(NTAPI*)(ULONG, ULONG, PVOID*, PULONG);
        RtlCaptureStackBackTraceFn capture_stack = nullptr;

        struct LuaStateView
        {
            std::uintptr_t base = 0;
            std::uintptr_t top = 0;
            std::size_t slots = 0;
        };

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

        bool module_span(std::uintptr_t base, std::uintptr_t& end)
        {
            if (!base) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;
            const std::uintptr_t size = nt->OptionalHeader.SizeOfImage;
            if (!size || base > UINTPTR_MAX - size) return false;
            end = base + size;
            return true;
        }

        bool read_state(void* lua_state, LuaStateView& state)
        {
            if (!lua_state) return false;

            if (!safe_mem::read_bytes(
                    static_cast<const std::uint8_t*>(lua_state) + game::lua_state_base_offset,
                    &state.base,
                    sizeof(state.base)) ||
                !safe_mem::read_bytes(
                    static_cast<const std::uint8_t*>(lua_state) + game::lua_state_top_offset,
                    &state.top,
                    sizeof(state.top)))
                return false;

            if (!state.base || !state.top || state.top < state.base)
                return false;
            const std::uintptr_t byte_count = state.top - state.base;
            if ((byte_count & 7u) != 0 || byte_count > 0x10000u)
                return false;
            state.slots = static_cast<std::size_t>(byte_count / 8u);
            return true;
        }

        bool read_slot(const LuaStateView& state, std::size_t index, std::uint64_t& value)
        {
            if (index >= state.slots) return false;
            return safe_mem::read_bytes(
                reinterpret_cast<const void*>(state.base + index * sizeof(std::uint64_t)),
                &value,
                sizeof(value));
        }

        bool read_string_value(std::uint64_t value, char* output, std::size_t output_size)
        {
            if (!output || output_size == 0) return false;
            output[0] = '\0';
            const auto type = static_cast<std::int32_t>(
                static_cast<std::int64_t>(value) >> 47);
            if (type != game::lua_string_type) return false;

            const std::uintptr_t string_object =
                static_cast<std::uintptr_t>(value & 0x7FFFFFFFFFFFull);
            if (!string_object || string_object > UINTPTR_MAX - game::lua_string_data_offset)
                return false;
            if (!safe_mem::read_string(
                    reinterpret_cast<const char*>(string_object + game::lua_string_data_offset),
                    output,
                    output_size))
                return output[0] != '\0';

            for (char* cursor = output; *cursor; ++cursor)
            {
                if (*cursor == '\r' || *cursor == '\n' || *cursor == '\t')
                    *cursor = ' ';
            }
            return output[0] != '\0';
        }

        void format_state(
            void* lua_state,
            char* output,
            std::size_t output_size)
        {
            if (!output || output_size == 0) return;
            output[0] = '\0';

            LuaStateView state{};
            if (!read_state(lua_state, state))
            {
                _snprintf_s(output, output_size, _TRUNCATE, "lua_state=unreadable");
                return;
            }

            const int header_size = _snprintf_s(
                output,
                output_size,
                _TRUNCATE,
                "lua_state=0x%llX slots=%zu values:",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(lua_state)),
                state.slots);
            if (header_size <= 0) return;
            std::size_t used = static_cast<std::size_t>(header_size);
            if (used >= output_size) return;

            const std::size_t first = state.slots > max_lua_slots
                ? state.slots - max_lua_slots
                : 0;
            for (std::size_t index = first; index < state.slots && used < output_size; ++index)
            {
                std::uint64_t value = 0;
                if (!read_slot(state, index, value)) break;

                char string_value[192]{};
                const auto type = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(value) >> 47);
                int written = 0;
                if (read_string_value(value, string_value, sizeof(string_value)))
                {
                    written = _snprintf_s(
                        output + used,
                        output_size - used,
                        _TRUNCATE,
                        " [%zu:string=\"%s\"]",
                        index,
                        string_value);
                }
                else
                {
                    written = _snprintf_s(
                        output + used,
                        output_size - used,
                        _TRUNCATE,
                        " [%zu:type=%d value=0x%llX]",
                        index,
                        type,
                        static_cast<unsigned long long>(value));
                }
                if (written <= 0) break;
                used += static_cast<std::size_t>(written);
            }
            if (first != 0 && used < output_size)
                _snprintf_s(output + used, output_size - used, _TRUNCATE, " [older slots omitted]");
        }

        bool read_pointer(const void* address, std::uintptr_t& value)
        {
            if (!address) return false;
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(address, &info, sizeof(info)) ||
                info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
                return false;
            std::memcpy(&value, address, sizeof(value));
            return true;
        }

        bool looks_like_state(std::uintptr_t candidate, LuaStateView& state)
        {
            if (candidate < 0x10000u) return false;
            return read_state(reinterpret_cast<void*>(candidate), state);
        }

        void* find_state(const CONTEXT* context)
        {
            if (!context) return nullptr;
            const std::uintptr_t candidates[] = {
                context->Rbx, context->Rsi, context->Rdi, context->R12,
                context->R13, context->R14, context->R15,
                context->Rcx, context->Rdx, context->R8, context->R9
            };
            for (const auto candidate : candidates)
            {
                LuaStateView state{};
                if (looks_like_state(candidate, state))
                    return reinterpret_cast<void*>(candidate);
            }
            return nullptr;
        }

        std::size_t collect_call_chain(
            const CONTEXT* exception_context,
            std::uintptr_t* frames,
            std::size_t capacity)
        {
            if (!exception_context || !frames || capacity == 0) return 0;

            CONTEXT context = *exception_context;
            std::size_t count = 0;
            while (count < capacity && context.Rip)
            {
                if (context.Rip >= game_base && context.Rip < game_end)
                    frames[count++] = static_cast<std::uintptr_t>(context.Rip);

                ULONG64 image_base = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(
                    context.Rip,
                    &image_base,
                    nullptr);
                if (function)
                {
                    CONTEXT previous = context;
                    PVOID handler_data = nullptr;
                    ULONG64 establisher_frame = 0;
                    KNONVOLATILE_CONTEXT_POINTERS nonvolatile{};
                    __try
                    {
                        RtlVirtualUnwind(
                            UNW_FLAG_NHANDLER,
                            image_base,
                            context.Rip,
                            function,
                            &context,
                            &handler_data,
                            &establisher_frame,
                            &nonvolatile);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        break;
                    }
                    if (context.Rip == previous.Rip && context.Rsp <= previous.Rsp)
                        break;
                    continue;
                }

                std::uintptr_t return_address = 0;
                if (!read_pointer(reinterpret_cast<const void*>(context.Rsp), return_address) ||
                    !return_address ||
                    return_address == context.Rip)
                    break;
                context.Rip = return_address;
                context.Rsp += sizeof(std::uintptr_t);
            }
            return count;
        }

        void format_call_chain(
            const CONTEXT* context,
            char* output,
            std::size_t output_size)
        {
            if (!output || output_size == 0) return;
            output[0] = '\0';

            std::uintptr_t frames[max_native_frames]{};
            const std::size_t count = collect_call_chain(
                context,
                frames,
                sizeof(frames) / sizeof(frames[0]));
            const int header_size = _snprintf_s(
                output,
                output_size,
                _TRUNCATE,
                "native:");
            if (header_size <= 0) return;
            std::size_t used = static_cast<std::size_t>(header_size);
            if (used >= output_size) return;
            for (std::size_t index = 0; index < count && used < output_size; ++index)
            {
                const int written = _snprintf_s(
                    output + used,
                    output_size - used,
                    _TRUNCATE,
                    " +0x%llX",
                    static_cast<unsigned long long>(frames[index] - game_base));
                if (written <= 0) break;
                used += static_cast<std::size_t>(written);
            }
            if (count == 0)
                _snprintf_s(output + used, output_size - used, _TRUNCATE, " (no game frames)");
        }

        void format_current_call_chain(char* output, std::size_t output_size)
        {
            if (!output || output_size == 0) return;
            output[0] = '\0';
            if (!capture_stack)
            {
                _snprintf_s(output, output_size, _TRUNCATE, "native:unavailable");
                return;
            }

            void* captured[48]{};
            const USHORT count = capture_stack(1, 48, captured, nullptr);
            std::size_t used = static_cast<std::size_t>(_snprintf_s(
                output,
                output_size,
                _TRUNCATE,
                "native:"));
            if (used >= output_size) return;

            std::size_t shown = 0;
            for (USHORT index = 0; index < count && used < output_size; ++index)
            {
                const auto address = reinterpret_cast<std::uintptr_t>(captured[index]);
                if (address < game_base || address >= game_end) continue;
                const int written = _snprintf_s(
                    output + used,
                    output_size - used,
                    _TRUNCATE,
                    " +0x%llX",
                    static_cast<unsigned long long>(address - game_base));
                if (written <= 0) break;
                used += static_cast<std::size_t>(written);
                ++shown;
            }
            if (shown == 0)
                _snprintf_s(output + used, output_size - used, _TRUNCATE, " (no game frames)");
        }

        void log_text(const char* label, const char* text)
        {
            if (!text || !text[0]) return;
            constexpr std::size_t chunk_size = 1500;
            const std::size_t length = std::strlen(text);
            for (std::size_t offset = 0; offset < length; offset += chunk_size)
            {
                const std::size_t size = (length - offset < chunk_size)
                    ? length - offset
                    : chunk_size;
                char chunk[chunk_size + 1]{};
                std::memcpy(chunk, text + offset, size);
                chunk[size] = '\0';
                LOG_ERROR("LuaErr", "%s%s", label ? label : "", chunk);
            }
        }

        void log_throw(
            std::uint32_t exception_code,
            const CONTEXT* context,
            const char* source)
        {
            const int status = static_cast<int>(exception_code & 0xFFu);
            void* lua_state = find_state(context);
            char state_text[8192]{};
            char call_chain[2048]{};
            format_state(lua_state, state_text, sizeof(state_text));
            format_call_chain(context, call_chain, sizeof(call_chain));
            LOG_ERROR(
                "LuaErr",
                "LuaJIT error status=%d exception=0x%08X source=%s %s",
                status,
                exception_code,
                source ? source : "VEH",
                call_chain);
            log_text("  ", state_text);
        }

        LONG WINAPI lua_exception_handler(EXCEPTION_POINTERS* info)
        {
            if (!config::hook_lua_errors || !info || !info->ExceptionRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const std::uint32_t code = info->ExceptionRecord->ExceptionCode;
            if ((code & 0xFFFFFF00u) != lua_exception_base)
                return EXCEPTION_CONTINUE_SEARCH;

            log_throw(code, info->ContextRecord, "first-chance");
            return EXCEPTION_CONTINUE_SEARCH;
        }

        int __fastcall panic_detour(void* lua_state)
        {
            char state_text[8192]{};
            char call_chain[2048]{};
            format_state(lua_state, state_text, sizeof(state_text));
            format_current_call_chain(call_chain, sizeof(call_chain));
            LOG_ERROR("LuaErr", "LUI panic callback %s", call_chain);
            log_text("  ", state_text);

            const auto callback = original_panic.load(std::memory_order_acquire);
            if (!callback || callback == reinterpret_cast<std::uintptr_t>(&panic_detour))
                return 0;
            return reinterpret_cast<game::LuaPanic>(callback)(lua_state);
        }

        std::int64_t __fastcall atpanic_detour(void* lua_state, void* panic_function)
        {
            const auto original = original_atpanic.load(std::memory_order_acquire);
            if (!original)
                return 0;

            if (panic_function &&
                panic_function != reinterpret_cast<void*>(&panic_detour) &&
                executable(panic_function))
            {
                original_panic.store(
                    reinterpret_cast<std::uintptr_t>(panic_function),
                    std::memory_order_release);
                return reinterpret_cast<game::LuaAtPanic>(original)(
                    lua_state,
                    reinterpret_cast<void*>(&panic_detour));
            }

            original_panic.store(0, std::memory_order_release);
            return reinterpret_cast<game::LuaAtPanic>(original)(lua_state, panic_function);
        }
    }

    void init()
    {
        LOG_INFO(
            "LuaErr",
            config::hook_lua_errors
                ? "Passive LuaJIT error capture is enabled"
                : "Passive LuaJIT error capture is disabled");
    }

    bool install(std::uintptr_t base)
    {
        if (!config::hook_lua_errors || !base) return true;

        if (!veh_installed.exchange(true, std::memory_order_acq_rel))
        {
            game_base = base;
            module_span(game_base, game_end);
            capture_stack = reinterpret_cast<RtlCaptureStackBackTraceFn>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlCaptureStackBackTrace"));
            if (!AddVectoredExceptionHandler(1, &lua_exception_handler))
            {
                veh_installed.store(false, std::memory_order_release);
                LOG_WARN("LuaErr", "Could not install the passive LuaJIT exception observer");
            }
            else
            {
                LOG_INFO(
                    "LuaErr",
                    "Passive LuaJIT exception observer installed; no VM re-entry is used");
            }
        }

        if (panic_hook_installed.load(std::memory_order_acquire)) return true;

        auto* target = reinterpret_cast<void*>(base + game::lua_atpanic_rva);
        if (!prologue_matches(
                target,
                game::lua_atpanic_prologue,
                sizeof(game::lua_atpanic_prologue)))
        {
            LOG_TRACE(
                "LuaErr",
                "1.69 lua_atpanic prologue is not ready or differs at %p; panic wrapper skipped",
                target);
            return true;
        }

        void* trampoline = hook::install(target, reinterpret_cast<void*>(&atpanic_detour));
        if (!trampoline)
        {
            LOG_WARN("LuaErr", "Could not install the guarded lua_atpanic observer at %p", target);
            return true;
        }

        original_atpanic.store(
            reinterpret_cast<std::uintptr_t>(trampoline),
            std::memory_order_release);
        panic_hook_installed.store(true, std::memory_order_release);
        LOG_INFO(
            "LuaErr",
            "Guarded 1.69 lua_atpanic observer installed at %p",
            target);
        return true;
    }

    void capture_compile_error(void* lua_state, const char* chunk_name, int status)
    {
        if (!config::hook_lua_errors) return;

        char state_text[8192]{};
        format_state(lua_state, state_text, sizeof(state_text));
        LOG_ERROR(
            "LuaErr",
            "Lua chunk compile failure status=%d chunk=%s",
            status,
            chunk_name ? chunk_name : "?");
        log_text("  ", state_text);
    }
}
