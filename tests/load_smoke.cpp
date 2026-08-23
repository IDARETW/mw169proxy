#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <cstdint>

int wmain(int argument_count, wchar_t** arguments)
{
    if (argument_count != 2)
    {
        std::fwprintf(stderr, L"Usage: load_smoke <proxy DLL>\n");
        return 2;
    }

    HMODULE proxy = LoadLibraryW(arguments[1]);
    if (!proxy)
    {
        std::fwprintf(stderr, L"LoadLibraryW failed with error %lu\n", GetLastError());
        return 3;
    }

    constexpr const char* exports[] = {
        "DiscordCreate",
        "DiscordVersion",
        "rust_eh_personality"
    };
    for (const char* name : exports)
    {
        if (!GetProcAddress(proxy, name))
        {
            std::fprintf(stderr, "Missing export: %s\n", name);
            return 4;
        }
    }

    wchar_t original_path[MAX_PATH]{};
    if (wcsncpy_s(original_path, arguments[1], _TRUNCATE) == 0)
    {
        wchar_t* slash = wcsrchr(original_path, L'\\');
        if (slash && wcscpy_s(slash + 1, MAX_PATH - static_cast<std::size_t>(slash + 1 - original_path), L"discord_game_sdks.dll") == 0 &&
            GetFileAttributesW(original_path) != INVALID_FILE_ATTRIBUTES)
        {
            HMODULE original = LoadLibraryW(original_path);
            if (!original)
            {
                std::fwprintf(stderr, L"Original Discord DLL load failed with error %lu\n", GetLastError());
                return 5;
            }

            for (const char* name : exports)
            {
                if (!GetProcAddress(original, name))
                {
                    std::fprintf(stderr, "Original DLL missing export: %s\n", name);
                    FreeLibrary(original);
                    return 6;
                }
            }
            FreeLibrary(original);
            std::printf("Original Discord export table load passed.\n");
        }
    }

    Sleep(1000);
    LoadImageA(
        nullptr,
        MAKEINTRESOURCEA(32512),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE | LR_SHARED);
    Sleep(250);

    std::printf("Proxy load and LoadImageA trigger smoke test passed.\n");
    return 0;
}
