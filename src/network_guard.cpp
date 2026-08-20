#include "network_guard.h"

#include "log.h"
#include "network_policy.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace network_guard
{
    namespace
    {
        using Connect = int(WSAAPI*)(SOCKET, const sockaddr*, int);
        using SendTo = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
        using GetAddrInfo = int(WSAAPI*)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
        using WinHttpConnect = HINTERNET(WINAPI*)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);

        std::atomic<Connect> original_connect = nullptr;
        std::atomic<SendTo> original_send_to = nullptr;
        std::atomic<GetAddrInfo> original_get_addr_info = nullptr;
        std::atomic<WinHttpConnect> original_win_http_connect = nullptr;
        std::atomic<unsigned int> blocked_calls = 0;

        struct ImportPatch
        {
            void** slot = nullptr;
            void* detour = nullptr;
            void* original = nullptr;
            bool active = false;
        };

        bool allow_address(const sockaddr* address, int address_size)
        {
            if (!address || address_size < static_cast<int>(sizeof(address->sa_family))) return false;
            if (address->sa_family == AF_UNSPEC) return true;

            if (address->sa_family == AF_INET && address_size >= static_cast<int>(sizeof(sockaddr_in)))
            {
                const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(&ipv4->sin_addr.S_un.S_addr);
                return network_policy::allow_ipv4(bytes);
            }

            if (address->sa_family == AF_INET6 && address_size >= static_cast<int>(sizeof(sockaddr_in6)))
            {
                const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
                return network_policy::allow_ipv6(ipv6->sin6_addr.u.Byte);
            }
            return false;
        }

        bool socket_ignores_send_to_destination(SOCKET socket)
        {
            int socket_type = 0;
            int value_size = sizeof(socket_type);
            if (getsockopt(
                    socket,
                    SOL_SOCKET,
                    SO_TYPE,
                    reinterpret_cast<char*>(&socket_type),
                    &value_size) != 0)
                return false;
            return socket_type == SOCK_STREAM || socket_type == SOCK_SEQPACKET;
        }

        void report_block(const char* api)
        {
            const unsigned int count = blocked_calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 12)
                LOG_INFO("Network", "Blocked public call %u through %s", count, api);
        }

        int WSAAPI connect_detour(SOCKET socket, const sockaddr* name, int name_length)
        {
            if (!allow_address(name, name_length))
            {
                report_block("connect");
                WSASetLastError(WSAEACCES);
                return SOCKET_ERROR;
            }
            Connect next = nullptr;
            next = original_connect.load(std::memory_order_acquire);
            if (!next)
            {
                WSASetLastError(WSAEACCES);
                return SOCKET_ERROR;
            }
            return next(socket, name, name_length);
        }

        int WSAAPI send_to_detour(
            SOCKET socket,
            const char* buffer,
            int length,
            int flags,
            const sockaddr* destination,
            int destination_length)
        {
            if (destination &&
                !socket_ignores_send_to_destination(socket) &&
                !allow_address(destination, destination_length))
            {
                report_block("sendto");
                WSASetLastError(WSAEACCES);
                return SOCKET_ERROR;
            }
            SendTo next = nullptr;
            next = original_send_to.load(std::memory_order_acquire);
            if (!next)
            {
                WSASetLastError(WSAEACCES);
                return SOCKET_ERROR;
            }
            return next(
                socket,
                buffer,
                length,
                flags,
                destination,
                destination_length);
        }

        int WSAAPI get_addr_info_detour(
            PCSTR node_name,
            PCSTR service_name,
            const ADDRINFOA* hints,
            PADDRINFOA* result)
        {
            if (!network_policy::allow_hostname(node_name))
            {
                report_block("getaddrinfo");
                if (result) *result = nullptr;
                return WSAHOST_NOT_FOUND;
            }
            GetAddrInfo next = nullptr;
            next = original_get_addr_info.load(std::memory_order_acquire);
            if (!next)
            {
                if (result) *result = nullptr;
                return WSAHOST_NOT_FOUND;
            }
            return next(
                node_name,
                service_name,
                hints,
                result);
        }

        HINTERNET WINAPI win_http_connect_detour(
            HINTERNET session,
            LPCWSTR server_name,
            INTERNET_PORT port,
            DWORD reserved)
        {
            if (!network_policy::allow_hostname(server_name))
            {
                report_block("WinHttpConnect");
                SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
                return nullptr;
            }
            WinHttpConnect next = nullptr;
            next = original_win_http_connect.load(std::memory_order_acquire);
            if (!next)
            {
                SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
                return nullptr;
            }
            return next(
                session,
                server_name,
                port,
                reserved);
        }

        std::mutex patch_mutex;
        std::array<ImportPatch, 4> active_patches{{
            {nullptr, reinterpret_cast<void*>(&connect_detour)},
            {nullptr, reinterpret_cast<void*>(&send_to_detour)},
            {nullptr, reinterpret_cast<void*>(&get_addr_info_detour)},
            {nullptr, reinterpret_cast<void*>(&win_http_connect_detour)}
        }};
        bool guard_installed = false;

        void clear_originals()
        {
            original_connect.store(nullptr, std::memory_order_release);
            original_send_to.store(nullptr, std::memory_order_release);
            original_get_addr_info.store(nullptr, std::memory_order_release);
            original_win_http_connect.store(nullptr, std::memory_order_release);
        }

        bool find_import_slot(
            std::uintptr_t game_base,
            const char* wanted_module,
            const char* wanted_name,
            unsigned short wanted_ordinal,
            void*** output)
        {
            if (!output) return false;
            *output = nullptr;
            auto* base = reinterpret_cast<std::uint8_t*>(game_base);
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
                if (_stricmp(module_name, wanted_module) != 0) continue;
                if (!descriptor->OriginalFirstThunk) return false;

                auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
                auto* functions = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
                for (std::size_t index = 0; names[index].u1.AddressOfData; ++index)
                {
                    bool matches = false;
                    if (IMAGE_SNAP_BY_ORDINAL64(names[index].u1.Ordinal))
                    {
                        matches = wanted_ordinal != 0 &&
                                  IMAGE_ORDINAL64(names[index].u1.Ordinal) == wanted_ordinal;
                    }
                    else if (wanted_name)
                    {
                        auto* import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                            base + names[index].u1.AddressOfData);
                        matches = std::strcmp(
                            reinterpret_cast<const char*>(import_name->Name),
                            wanted_name) == 0;
                    }
                    if (!matches) continue;

                    auto* slot = reinterpret_cast<void**>(&functions[index].u1.Function);
                    if (!*slot) return false;
                    *output = slot;
                    return true;
                }
                return false;
            }
            return false;
        }

        bool apply_patch(ImportPatch& patch)
        {
            DWORD old_protection = 0;
            if (!patch.slot || !patch.detour ||
                !VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &old_protection))
                return false;

            patch.original = InterlockedExchangePointer(
                reinterpret_cast<void* volatile*>(patch.slot),
                patch.detour);
            patch.active = true;

            DWORD ignored = 0;
            const bool protection_restored =
                VirtualProtect(patch.slot, sizeof(void*), old_protection, &ignored) != FALSE;
            FlushInstructionCache(GetCurrentProcess(), patch.slot, sizeof(void*));
            return patch.original && protection_restored;
        }

        bool restore_patch(ImportPatch& patch)
        {
            if (!patch.active || !patch.slot) return true;

            DWORD old_protection = 0;
            if (!VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &old_protection))
                return false;

            InterlockedExchangePointer(
                reinterpret_cast<void* volatile*>(patch.slot),
                patch.original);
            DWORD ignored = 0;
            const bool protection_restored =
                VirtualProtect(patch.slot, sizeof(void*), old_protection, &ignored) != FALSE;
            FlushInstructionCache(GetCurrentProcess(), patch.slot, sizeof(void*));
            patch.active = false;
            return protection_restored;
        }
    }

    bool install(std::uintptr_t game_base)
    {
        std::lock_guard lock(patch_mutex);
        if (guard_installed) return true;

        for (auto& patch : active_patches)
        {
            patch.slot = nullptr;
            patch.original = nullptr;
            patch.active = false;
        }

        const bool found =
            find_import_slot(game_base, "WS2_32.dll", nullptr, 4, &active_patches[0].slot) &&
            find_import_slot(game_base, "WS2_32.dll", nullptr, 20, &active_patches[1].slot) &&
            find_import_slot(game_base, "WS2_32.dll", "getaddrinfo", 0, &active_patches[2].slot) &&
            find_import_slot(game_base, "WINHTTP.dll", "WinHttpConnect", 0, &active_patches[3].slot);
        if (!found) return false;

        for (std::size_t index = 0; index < active_patches.size(); ++index)
        {
            const bool applied = apply_patch(active_patches[index]);

            if (active_patches[index].active)
            {
                switch (index)
                {
                    case 0:
                        original_connect.store(
                            reinterpret_cast<Connect>(active_patches[index].original),
                            std::memory_order_release);
                        break;
                    case 1:
                        original_send_to.store(
                            reinterpret_cast<SendTo>(active_patches[index].original),
                            std::memory_order_release);
                        break;
                    case 2:
                        original_get_addr_info.store(
                            reinterpret_cast<GetAddrInfo>(active_patches[index].original),
                            std::memory_order_release);
                        break;
                    default:
                        original_win_http_connect.store(
                            reinterpret_cast<WinHttpConnect>(active_patches[index].original),
                            std::memory_order_release);
                        break;
                }
            }

            if (!applied)
            {
                for (std::size_t restore = index + 1; restore > 0; --restore)
                    restore_patch(active_patches[restore - 1]);

                bool any_active = false;
                for (const auto& patch : active_patches)
                    any_active = any_active || patch.active;
                if (!any_active) clear_originals();
                return false;
            }
        }
        guard_installed = true;
        return true;
    }

    bool uninstall()
    {
        std::lock_guard lock(patch_mutex);
        bool restored = true;
        for (std::size_t index = active_patches.size(); index > 0; --index)
            restored = restore_patch(active_patches[index - 1]) && restored;

        bool any_active = false;
        for (const auto& patch : active_patches)
            any_active = any_active || patch.active;

        if (!any_active)
        {
            clear_originals();
            guard_installed = false;
        }
        return restored && !any_active;
    }
}
