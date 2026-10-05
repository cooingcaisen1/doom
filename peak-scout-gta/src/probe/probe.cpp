// PeakScoutGTA probe: proves Ultimate ASI Loader loaded this ASI in GTA V Enhanced.
// It only writes one log line next to the game exe and does nothing else in the game.
#include <windows.h>
#include <cstdio>

static DWORD WINAPI WriteLog(LPVOID) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t log[MAX_PATH];
    lstrcpyW(log, exe);
    wchar_t* slash = wcsrchr(log, L'\\');
    if (slash) lstrcpyW(slash + 1, L"PeakScoutGTA.log");
    FILE* f = _wfopen(log, L"a");
    if (!f) return 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d PeakScoutGTA probe loaded in %ls\n",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, exe);
    fclose(f);
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
