#include "startup_marker.h"

#include "log.h"

#include <windows.h>

#include <cwchar>

namespace startup_marker
{
    void remove_game_marker_early()
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;

        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return;

        slash[1] = L'\0';
        if (wcscat_s(path, MAX_PATH, L"__ModernWarfare") != 0) return;

        // This path runs from DllMain. Keep it limited to path inspection and
        // DeleteFileW. The normal worker reports the result after logging starts.
        DeleteFileW(path);
    }

    bool remove_game_marker()
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        {
            LOG_WARN("Startup", "Could not locate the game marker");
            return false;
        }

        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash)
        {
            LOG_WARN("Startup", "Could not build the game marker path");
            return false;
        }

        slash[1] = L'\0';
        if (wcscat_s(path, MAX_PATH, L"__ModernWarfare") != 0)
        {
            LOG_WARN("Startup", "Could not build the game marker path");
            return false;
        }

        const DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                LOG_INFO("Startup", "Crash marker absent");
                return true;
            }

            LOG_WARN("Startup", "Could not inspect crash marker (%lu)", error);
            return false;
        }

        if (!DeleteFileW(path))
        {
            LOG_WARN("Startup", "Could not remove crash marker (%lu)", GetLastError());
            return false;
        }

        LOG_INFO("Startup", "Removed crash marker");
        return true;
    }
}
