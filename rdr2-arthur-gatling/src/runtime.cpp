// Boot: find the game's script machinery by pattern (sheets/game_hooks.json),
// then run our own script thread every frame. No Script Hook needed; Ultimate ASI
// Loader loads this file. Layout notes mirror emcifuntik/rdr2scripthook (MIT).
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <ctime>
#include "MinHook.h"
#include "generated.h"
#include "log.h"

void gatlingFrame();     // gatling.cpp

namespace rt {
GetNativeAddressFn getNativeAddress = nullptr;
const char* volatile lastNative = nullptr;
void reportMissingNative(const char* name, uint64_t hash) { logf("native %s (0x%016llX) not found", name, (unsigned long long)hash); }
void reportFirstCall(const char* name) { logf("first call: %s", name); }
}

// ---------- log ----------
static FILE* g_log = nullptr;
void logInit(const wchar_t* path) { g_log = _wfopen(path, L"w"); }
void logf(const char* fmt, ...) {
    if (!g_log) return;
    time_t t = time(nullptr);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
    fprintf(g_log, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------- pattern scan ----------
static uintptr_t g_base = 0;
static size_t g_size = 0;
static uintptr_t g_found[sizeof(kPatterns) / sizeof(kPatterns[0])] = {};

static bool parse(const char* p, uint8_t* bytes, bool* mask, size_t& n) {
    n = 0;
    while (*p) {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') { mask[n] = false; bytes[n++] = 0; ++p; continue; }
        auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        mask[n] = true;
        bytes[n++] = uint8_t(hex(p[0]) << 4 | hex(p[1]));
        p += 2;
    }
    return n > 0;
}

static uintptr_t scan(const PatternDef& def) {
    uint8_t bytes[128];
    bool mask[128];
    size_t n;
    if (!parse(def.pattern, bytes, mask, n)) return 0;
    const uint8_t* start = reinterpret_cast<const uint8_t*>(g_base);
    for (size_t i = 0; i + n <= g_size; ++i) {
        size_t k = 0;
        while (k < n && (!mask[k] || start[i + k] == bytes[k])) ++k;
        if (k == n) {
            uintptr_t at = g_base + i;
            switch (def.resolve) {
                case Resolve::Direct: return at;
                case Resolve::Rip: return at + def.ripOffset + 4 + *reinterpret_cast<const int32_t*>(at + def.ripOffset);
                case Resolve::RipImm8: return at + def.ripOffset + 5 + *reinterpret_cast<const int32_t*>(at + def.ripOffset);
                case Resolve::Call: return at + def.ripOffset + 4 + *reinterpret_cast<const int32_t*>(at + def.ripOffset);
            }
        }
    }
    return 0;
}

template <class T> static T found(int id) { return reinterpret_cast<T>(g_found[id]); }

// ---------- our script thread (raw layout, MSVC vtable order) ----------
// scrThread is 0x788 bytes: vtable, context (0x6B0), stack, ..., exitMessage @0x6D0,
// scriptHash @0x6D8, scriptHandler @0x6E0. Built by hand so the vtable matches the
// game's MSVC layout regardless of compiler.
constexpr size_t kThreadSize = 0x788;
enum { StateIdle = 0, StateRunning = 1, StateKilled = 2 };

static uint8_t* g_thread = nullptr;
static void (*g_initThread)(void*) = nullptr;
static int (*g_tickThread)(void*, uint32_t) = nullptr;
static void* (*g_sysAlloc)(size_t) = nullptr;
static void*** g_scriptHandlerMgr = nullptr;
static void** g_currentThread = nullptr;
static bool* g_inSessionFlag = nullptr;
static std::atomic<bool> g_loadingDone{false};
static bool g_threadLogged = false;

static uint32_t& ctxU32(void* t, size_t off) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(t) + off); }

static void* __cdecl vDtor(void* self, int) { return self; }
static int __cdecl vReset(void* self, uint32_t hash, const void*, int);
static int __cdecl vRun(void* self, int) {
    void* prev = *g_currentThread;
    *g_currentThread = self;
    if (ctxU32(self, 0x8 + 0x8) != StateKilled) {
        if (!g_threadLogged) { g_threadLogged = true; logf("script thread running"); }
        gatlingFrame();
    }
    *g_currentThread = prev;
    return int(ctxU32(self, 0x8 + 0x8));
}
static int __cdecl vUpdate(void* self, int ops) { return g_tickThread(self, uint32_t(ops)); }
static void __cdecl vKill(void* self) { ctxU32(self, 0x8 + 0x8) = StateKilled; }
static int64_t __cdecl vStub() { return 0; }
static void* g_vtableSlots[16];

static int __cdecl vReset(void* self, uint32_t hash, const void*, int) {
    uint8_t* t = static_cast<uint8_t*>(self);
    std::memset(t + 0x8, 0, 0x6B0);     // context
    ctxU32(t, 0x8 + 0x08) = StateIdle;  // scriptState
    ctxU32(t, 0x8 + 0x04) = hash;       // scriptHash
    ctxU32(t, 0x8 + 0x24) = 0xFFFFFFFF; // unk1
    ctxU32(t, 0x8 + 0x28) = 0xFFFFFFFF; // unk2
    ctxU32(t, 0x8 + 0x60) = 1;          // unk3
    std::memset(t + 0x6E0, 0, 8);
    for (size_t off = 0x720; off <= 0x760; off += 0x10) {
        std::memset(t + off, 0, 8);
        std::memset(t + off + 8, 0, 4);
    }
    t[0x770] = 0;
    t[0x71A] = 0;
    g_initThread(self);
    *reinterpret_cast<const char**>(t + 0x6D0) = "Not aborted yet?";
    ctxU32(t, 0x8 + 0x00) = hash;  // threadId
    // scriptHandlerMgr vtable: dtor, Init, Update, Shutdown, NetworkInit, NetworkUpdate,
    // NetworkShutdown, GetScriptId, CreateScriptHandler, GetScriptHandler, RegisterScript(10)
    auto registerScript = reinterpret_cast<void (*)(void*, void*)>((*g_scriptHandlerMgr)[10]);
    registerScript(g_scriptHandlerMgr, self);
    return int(ctxU32(t, 0x8 + 0x08));
}

static bool startThread() {
    for (auto& s : g_vtableSlots) s = reinterpret_cast<void*>(&vStub);
    g_vtableSlots[0] = reinterpret_cast<void*>(&vDtor);
    g_vtableSlots[1] = reinterpret_cast<void*>(&vReset);
    g_vtableSlots[2] = reinterpret_cast<void*>(&vRun);
    g_vtableSlots[3] = reinterpret_cast<void*>(&vUpdate);
    g_vtableSlots[4] = reinterpret_cast<void*>(&vKill);
    uint8_t* t = static_cast<uint8_t*>(g_sysAlloc(kThreadSize));
    if (!t) { logf("could not allocate script thread"); return false; }
    std::memset(t, 0, kThreadSize);
    *reinterpret_cast<void***>(t) = g_vtableSlots;
    constexpr uint32_t kScriptId = 0x0A57A71u;  // our own id, outside the game's script range
    *reinterpret_cast<uint32_t*>(t + 0x6D8) = kScriptId;
    vReset(t, kScriptId, nullptr, 0);
    g_thread = t;
    logf("script thread registered (id 0x%X)", kScriptId);
    return true;
}

// ---------- detours ----------
static bool (*oUpdateSingleScripts)(void*) = nullptr;
static bool hkUpdateSingleScripts(void* collection) {
    bool orig = oUpdateSingleScripts(collection);
    if (!g_thread && g_loadingDone.load()) {
        if (!startThread()) return orig;
    }
    if (!g_thread) return orig;
    bool ours = g_tickThread(g_thread, 13000000) != StateKilled;
    return orig || ours;
}

static void (*oShutdownLoadingScreen)() = nullptr;
static void hkShutdownLoadingScreen() {
    if (!g_loadingDone.exchange(true)) logf("loading screen done");
    oShutdownLoadingScreen();
}

bool rtInSessionFlag() { return g_inSessionFlag && *g_inSessionFlag; }

// ---------- crash log ----------
// Logs fatal-looking exceptions with the last native the mod called, so a crash
// report from a player names the culprit. Never handles anything itself.
static volatile LONG g_crashLines = 0;
static LONG CALLBACK crashLogger(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != 0xC0000409)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_crashLines) > 3) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t at = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(at), &mod);
    wchar_t name[MAX_PATH] = L"?";
    if (mod) GetModuleFileNameW(mod, name, MAX_PATH);
    const wchar_t* base = wcsrchr(name, L'\\');
    logf("exception %08lX at %ls+0x%llX (on game thread: %s), last native: %s, data address %p",
         code, base ? base + 1 : name, (unsigned long long)(at - reinterpret_cast<uintptr_t>(mod)),
         (g_currentThread && g_thread && *g_currentThread == g_thread) ? "ours" : "other",
         rt::lastNative ? rt::lastNative : "none",
         info->ExceptionRecord->NumberParameters > 1 ? (void*)info->ExceptionRecord->ExceptionInformation[1] : nullptr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void boot() {
    AddVectoredExceptionHandler(0, crashLogger);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    logf("game image at %p, %zu bytes", (void*)g_base, g_size);

    bool ok = true;
    for (size_t i = 0; i < sizeof(kPatterns) / sizeof(kPatterns[0]); ++i) {
        g_found[i] = scan(kPatterns[i]);
        logf("pattern %-22s %s (%p)", kPatterns[i].id, g_found[i] ? "found" : "MISSING", (void*)g_found[i]);
        if (!g_found[i] && i != hooks::isInSession) ok = false;
    }
    if (!ok) {
        logf("a pattern is missing (game update?) - Arthur's Gatling stays off, the game is untouched");
        return;
    }
    rt::getNativeAddress = found<GetNativeAddressFn>(hooks::getNativeAddress);
    g_currentThread = found<void**>(hooks::currentScriptThread);
    g_scriptHandlerMgr = found<void***>(hooks::scriptHandlerManager);
    g_inSessionFlag = found<bool*>(hooks::isInSession);
    g_initThread = found<void (*)(void*)>(hooks::initScriptThread);
    g_tickThread = found<int (*)(void*, uint32_t)>(hooks::tickScriptThread);
    g_sysAlloc = found<void* (*)(size_t)>(hooks::sysAlloc);

    if (MH_CreateHook(found<void*>(hooks::updateSingleScripts), (void*)&hkUpdateSingleScripts, (void**)&oUpdateSingleScripts) != MH_OK ||
        MH_CreateHook(found<void*>(hooks::shutdownLoadingScreen), (void*)&hkShutdownLoadingScreen, (void**)&oShutdownLoadingScreen) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        logf("could not hook the script update - staying off");
        return;
    }
    logf("hooks in place; waiting for the loading screen");
}

// Init after the game has unpacked itself: its first GetCommandLineA call.
static LPSTR(WINAPI* oGetCommandLineA)() = nullptr;
static std::atomic<bool> g_booted{false};
static LPSTR WINAPI hkGetCommandLineA() {
    if (!g_booted.exchange(true)) boot();
    return oGetCommandLineA();
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) wcscpy(slash + 1, L"ArthurGatling.log");
        logInit(path);
        logf("Arthur's Gatling loaded (Story Mode only)");
        if (MH_Initialize() != MH_OK) { logf("MinHook init failed"); return TRUE; }
        void* target = (void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetCommandLineA");
        if (!target) target = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCommandLineA");
        if (MH_CreateHook(target, (void*)&hkGetCommandLineA, (void**)&oGetCommandLineA) != MH_OK || MH_EnableHook(target) != MH_OK)
            logf("could not hook GetCommandLineA - staying off");
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
