#include "crash_diagnostics.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crash_diagnostics
{
    namespace
    {
        constexpr DWORD fail_fast_exception = 0xC0000602u;

        HMODULE self_module = nullptr;
        std::uintptr_t game_base = 0;
        std::uintptr_t game_end = 0;
        std::uintptr_t proxy_base = 0;
        std::uintptr_t proxy_end = 0;
        HANDLE crash_file = INVALID_HANDLE_VALUE;
        std::atomic<bool> installed = false;
        std::atomic<bool> report_in_progress = false;

        struct KnownSymbol
        {
            std::uintptr_t start;
            std::uintptr_t end;
            const char* name;
        };

        // These are reviewed retail 1.69 function ranges. They are not
        // development addresses and they are not runtime patch targets.
        constexpr KnownSymbol known_symbols[] = {
            {0x2D69A90, 0x2D69AA1, "mw169_PartyClientTaskHostResolver_HostAssigned"},
            {0x2D6AA90, 0x2D6AA98, "mw169_PartyClientTask_Active"},
            {0x2D6B040, 0x2D6B048, "mw169_PartyClientTask_Completed"},
            {0x2D6B050, 0x2D6B058, "mw169_PartyClientTask_Failed"},
            {0x2D6B620, 0x2D6B62B, "mw169_PartyClientTask_GetHost"},
            {0x2D6BA40, 0x2D6BA4A, "mw169_PartyClientTask_Init"},
            {0x2D6C040, 0x2D6C048, "mw169_PartyClientTask_Reset"},
            {0x2D6C2A0, 0x2D6C2A9, "mw169_PartyClientTask_Stop"},
            {0x30143D0, 0x30143EA, "mw169_SignInRecordForController"},
            {0x3014400, 0x3014408, "mw169_SignInStateReady"},
            {0x3074AA0, 0x3074AB4, "mw169_OnlineErrorManager_DoesErrorExist"},
            {0x3074F80, 0x3074F91, "mw169_OnlineErrorManager_GetFailureAction"},
            {0x3075030, 0x3075038, "mw169_OnlineErrorManager_GetShouldReportLastRecordedError"},
            {0x3075040, 0x3075051, "mw169_OnlineErrorManager_GetShouldShowConfirmationPopup"},
            {0x3374E60, 0x3374E72, "mw169_NetAddress_ctor"},
            {0x3374F00, 0x3374F42, "mw169_NetAddress_Clear"},
            {0x33751C0, 0x33751CA, "mw169_NetAddress_IsBacklog"},
            {0x3375290, 0x3375299, "mw169_NetAddress_IsNull"},
            {0x3377BF0, 0x3377C0D, "mw169_NetChannel_ctor"},
            {0x3377CA0, 0x3377CA4, "mw169_NetChannel_GetLocalId"},
            {0x3377CB0, 0x3377CB4, "mw169_NetChannel_GetRemoteId"},
            {0x3377CC0, 0x3377CC7, "mw169_NetChannel_IsClosed"},
            {0x3377CD0, 0x3377CD7, "mw169_NetChannel_IsOpen"},
            {0x3942580, 0x3942613, "mw169_bdSocketRouter_shouldUseRelay"},
            {0x3942620, 0x39426B3, "mw169_bdSocketRouter_shouldUseRelayIfNatTravFails"},
            {0x3944EE0, 0x3944EE9, "mw169_NetPing_Clear"},
            {0x3944EF0, 0x3944EFE, "mw169_NetPingInfo_Clear"},
            {0x3EB7430, 0x3EB7443, "mw169_Live_IsUserSignedIn"},
            {0x4076B30, 0x4076D4C, "mw169_dwGetRetailDebug"},
            {0x4076DE0, 0x4076E33, "mw169_dwNetGetErrorString"},
            {0x41E35C0, 0x41E35ED, "mw169_CL_GetLocalClientSignInState"},
            {0x421E5A0, 0x421E5BA, "mw169_Live_GetShouldDemonwareRetry"},
            {0x421E5C0, 0x421E5D5, "mw169_Live_GetShouldDemonwareRetryWithBase"},
            {0x4224A80, 0x4224AE0, "mw169_Live_IsUserSignedInToDemonware"},
            {0x48F040, 0x48FF6B, "mw169_DemonwareHostTableInit"},
            {0x4BA7E80, 0x4BA7E88, "mw169_BattleNetStateBase"},
            {0x6236C50, 0x6236C5B, "mw169_dwLogonHSM_ClearConnectInterval"},
            {0x6236C60, 0x6236C6B, "mw169_dwLogonHSM_ClearLogonAttemptCount"},
            {0x67A0160, 0x67A01EA, "mw169_LUI_IsUserSignedIn"},
            {0x67A8D50, 0x67A8D8A, "mw169_LUI_IsBattleNet"},
            {0x67A9340, 0x67A9377, "mw169_LUI_IsBattleNetLanOnly"},
            {0x71363C0, 0x71364C7, "mw169_BNet_DecodePlatform"},
            {0x7136540, 0x7136893, "mw169_BNet_ConfigurePlatform"},
            {0x7136AE0, 0x7136F4C, "mw169_BNet_ParseUnoAccountResponse"},
            {0x7136F50, 0x7136FF0, "mw169_BNet_ParseAccessToken"},
            {0x7137150, 0x7137416, "mw169_BNet_ParseLoginResult"},
            {0x7137610, 0x7137CE0, "mw169_BNet_ParseAccountProfile"},
            {0x713BBB0, 0x713BC26, "mw169_BNet_LoginDriverStart"},
            {0x713BC90, 0x713BE3C, "mw169_BNet_LoginServiceStart"},
            {0x713C9B0, 0x713CA99, "mw169_BNet_ParseProviderAccount"},
            {0x713CB20, 0x713CCA7, "mw169_BNet_ParseTokenObject"},
            {0x713D2E0, 0x713D670, "mw169_BNet_MapLoginServiceEvent"},
            {0x713D670, 0x713E50C, "mw169_BNet_LoginStateMachine"},
            {0x713E5C0, 0x713E644, "mw169_BNet_ResetLoginServiceFlow"},
            {0x713E650, 0x713E6FA, "mw169_BNet_ResumeLoginServiceFlow"},
            {0x71413E0, 0x7141CAF, "mw169_BNet_UmbrellaRefreshStateMachine"},
            {0x7142F70, 0x714392D, "mw169_BNet_BuildTokenRequest"},
            {0x7143930, 0x7143D89, "mw169_BNet_BuildLoginServiceResumeRequest"},
            {0x7143D90, 0x714416C, "mw169_BNet_BuildLoginQueueAndTwoFactorRequest"},
            {0x7144170, 0x71443DD, "mw169_BNet_DiscoverLoginServiceEnvironment"},
            {0x71457C0, 0x7145F1C, "mw169_BNet_ParseLoginServiceResult"},
            {0x7146150, 0x714677F, "mw169_BNet_HandleLoginServiceResponse"},
            {0x7146780, 0x7146977, "mw169_BNet_ParseLoginServiceResponseJson"},
            {0x7146980, 0x714718C, "mw169_BNet_ParseLoginTicketAndTitleData"},
            {0x7147690, 0x71478AB, "mw169_BNet_BuildLoginInstanceParameters"},
            {0x71478B0, 0x7147BF3, "mw169_BNet_BuildUnoFirstPartyConnection"},
            {0x71487C0, 0x7148BAA, "mw169_BNet_DispatchUnoAccountOperation"},
            {0x71491F0, 0x714937E, "mw169_BNet_BuildUnoLoginRequest"},
        };

        struct CallFrame
        {
            std::uintptr_t pc;
            std::uintptr_t rsp;
        };

        bool module_span(std::uintptr_t base, std::uintptr_t& end)
        {
            if (!base) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

            const std::uintptr_t size = nt->OptionalHeader.SizeOfImage;
            if (!size || base > UINTPTR_MAX - size) return false;
            end = base + size;
            return true;
        }

        HANDLE open_report_file(HMODULE self)
        {
            wchar_t path[MAX_PATH]{};
            if (!self || !GetModuleFileNameW(self, path, MAX_PATH)) return INVALID_HANDLE_VALUE;

            wchar_t* slash = wcsrchr(path, L'\\');
            if (!slash) return INVALID_HANDLE_VALUE;
            slash[1] = L'\0';
            if (wcscat_s(path, MAX_PATH, L"mw169proxy.crash.log") != 0)
                return INVALID_HANDLE_VALUE;

            return CreateFileW(
                path,
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
        }

        void write_text(const char* text)
        {
            if (crash_file == INVALID_HANDLE_VALUE || !text) return;
            DWORD written = 0;
            WriteFile(crash_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
        }

        void describe_pc(char* output, std::size_t size, std::uintptr_t pc)
        {
            if (!output || size == 0) return;
            if (pc >= game_base && pc < game_end)
            {
                _snprintf_s(output, size, _TRUNCATE, "game+0x%llX", static_cast<unsigned long long>(pc - game_base));
                return;
            }
            if (pc >= proxy_base && pc < proxy_end)
            {
                _snprintf_s(output, size, _TRUNCATE, "proxy+0x%llX", static_cast<unsigned long long>(pc - proxy_base));
                return;
            }
            _snprintf_s(output, size, _TRUNCATE, "0x%llX", static_cast<unsigned long long>(pc));
        }

        const KnownSymbol* known_symbol_for_pc(std::uintptr_t pc, std::uintptr_t& rva)
        {
            if (pc < game_base || pc >= game_end) return nullptr;

            rva = pc - game_base;
            for (const auto& symbol : known_symbols)
            {
                if (rva >= symbol.start && rva < symbol.end)
                    return &symbol;
            }
            return nullptr;
        }

        void describe_symbolized_pc(char* output, std::size_t size, std::uintptr_t pc)
        {
            if (!output || size == 0) return;

            std::uintptr_t rva = 0;
            const KnownSymbol* symbol = known_symbol_for_pc(pc, rva);
            if (symbol)
            {
                _snprintf_s(
                    output,
                    size,
                    _TRUNCATE,
                    "game+0x%llX <%s+0x%llX>",
                    static_cast<unsigned long long>(rva),
                    symbol->name,
                    static_cast<unsigned long long>(rva - symbol->start));
                return;
            }

            describe_pc(output, size, pc);
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

        std::size_t collect_call_chain(
            const CONTEXT* exception_context,
            CallFrame* frames,
            std::size_t capacity)
        {
            if (!exception_context || !frames || capacity == 0) return 0;

            CONTEXT context = *exception_context;
            std::size_t count = 0;
            while (count < capacity && context.Rip)
            {
                frames[count++] = {
                    static_cast<std::uintptr_t>(context.Rip),
                    static_cast<std::uintptr_t>(context.Rsp)};

                ULONG64 image_base = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(
                    context.Rip,
                    &image_base,
                    nullptr);

                if (function)
                {
                    PVOID handler_data = nullptr;
                    ULONG64 establisher_frame = 0;
                    KNONVOLATILE_CONTEXT_POINTERS nonvolatile{};
                    CONTEXT previous = context;
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

        void write_call_chain(const CallFrame* frames, std::size_t count)
        {
            if (!frames) return;

            for (std::size_t index = 0; index < count; ++index)
            {
                char location[64]{};
                char line[128]{};
                describe_pc(location, sizeof(location), frames[index].pc);
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "  #%02u %s rsp=0x%llX\r\n",
                    static_cast<unsigned int>(index),
                    location,
                    static_cast<unsigned long long>(frames[index].rsp));
                write_text(line);
            }
        }

        void format_symbolized_call_chain(
            char* output,
            std::size_t output_size,
            const CallFrame* frames,
            std::size_t count)
        {
            if (!output || output_size == 0 || !frames) return;

            std::size_t used = 0;
            int written = _snprintf_s(
                output,
                output_size,
                _TRUNCATE,
                "Crash callchain (symbols):");
            if (written < 0) return;
            used = static_cast<std::size_t>(written);

            for (std::size_t index = 0; index < count && used < output_size; ++index)
            {
                char location[160]{};
                describe_symbolized_pc(location, sizeof(location), frames[index].pc);
                written = _snprintf_s(
                    output + used,
                    output_size - used,
                    _TRUNCATE,
                    " #%02u %s",
                    static_cast<unsigned int>(index),
                    location);
                if (written < 0) break;
                used += static_cast<std::size_t>(written);
            }
        }

        LONG report(EXCEPTION_POINTERS* info)
        {
            if (!info || !info->ExceptionRecord || !info->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            const DWORD code = info->ExceptionRecord->ExceptionCode;
            if (code != EXCEPTION_ACCESS_VIOLATION &&
                code != EXCEPTION_ILLEGAL_INSTRUCTION &&
                code != EXCEPTION_STACK_OVERFLOW &&
                code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
                code != EXCEPTION_BREAKPOINT &&
                code != fail_fast_exception)
                return EXCEPTION_CONTINUE_SEARCH;

            bool expected = false;
            if (!report_in_progress.compare_exchange_strong(expected, true))
                return EXCEPTION_CONTINUE_SEARCH;

            char header[320]{};
            _snprintf_s(
                header,
                sizeof(header),
                _TRUNCATE,
                "\r\n=== mw169proxy exception ===\r\ncode=0x%08lX address=0x%llX thread=%lu\r\n",
                static_cast<unsigned long>(code),
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress)),
                static_cast<unsigned long>(GetCurrentThreadId()));
            write_text(header);

            char location[64]{};
            describe_pc(
                location,
                sizeof(location),
                reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress));
            logging::emergency(
                "Crash: code=0x%08lX at %s",
                static_cast<unsigned long>(code),
                location);

            CallFrame frames[64]{};
            const std::size_t frame_count = collect_call_chain(
                info->ContextRecord,
                frames,
                sizeof(frames) / sizeof(frames[0]));
            write_call_chain(frames, frame_count);

            char symbolized_chain[8192]{};
            format_symbolized_call_chain(
                symbolized_chain,
                sizeof(symbolized_chain),
                frames,
                frame_count);
            write_text(symbolized_chain);
            write_text("\r\n");
            logging::emergency("%s", symbolized_chain);
            write_text("=== end exception ===\r\n");
            FlushFileBuffers(crash_file);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        LONG WINAPI vectored_handler(EXCEPTION_POINTERS* info)
        {
            return report(info);
        }

        LONG WINAPI unhandled_handler(EXCEPTION_POINTERS* info)
        {
            return report(info);
        }
    }

    void install(HMODULE self)
    {
        bool expected = false;
        if (!installed.compare_exchange_strong(expected, true)) return;

        self_module = self;
        proxy_base = reinterpret_cast<std::uintptr_t>(self_module);
        module_span(proxy_base, proxy_end);
        game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        module_span(game_base, game_end);
        crash_file = open_report_file(self_module);

        // Install first. The retail executable has its own exception machinery and
        // can terminate before a later VEH receives the fault.
        AddVectoredExceptionHandler(1, &vectored_handler);
        SetUnhandledExceptionFilter(&unhandled_handler);
    }
}
