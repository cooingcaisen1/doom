// PeakScoutGTA probe: proves Ultimate ASI Loader loaded this ASI in GTA V Enhanced.
// It only writes a log line and does nothing else in the game. It writes to
// %LOCALAPPDATA%\PeakScoutGTA\probe.log (always writable) and tries the GTA folder too,
// recording whether the GTA folder was writable.
#include <windows.h>
#include <shlobj.h>
#include <cstdio>

static bool AppendLine(const wchar_t* path, const wchar_t* line) {
    FILE* f = _wfopen(path, L"a");
    if (!f) return false;
    fputws(line, f);
    fclose(f);
    return true;
}

static DWORD WINAPI WriteLog(LPVOID) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[1024];
    swprintf(line, 1024, L"%04d-%02d-%02d %02d:%02d:%02d PeakScoutGTA probe 0.0.2 loaded in %ls\n",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, exe);

    wchar_t gameLog[MAX_PATH];
    lstrcpyW(gameLog, exe);
    wchar_t* slash = wcsrchr(gameLog, L'\\');
    if (slash) lstrcpyW(slash + 1, L"PeakScoutGTA.log");
    bool gameOk = AppendLine(gameLog, line);

    wchar_t local[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) {
        lstrcatW(local, L"\\PeakScoutGTA");
        CreateDirectoryW(local, nullptr);
        lstrcatW(local, L"\\probe.log");
        AppendLine(local, line);
        AppendLine(local, gameOk ? L"  GTA folder log: written\n" : L"  GTA folder log: could not write (folder not writable)\n");
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE h = CreateThread(nullptr, 0, WriteLog, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
