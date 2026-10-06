// Headless stand-in for RDR2.exe. Loads ArthurGatling.asi the way Ultimate ASI Loader
// does, provides the code sites its patterns look for, answers its native calls, and
// plays a scripted session (F7, firing, put away, online, riding). Exit code 0 = pass.
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

constexpr uint32_t joaat(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) { char c = *s; if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); h += uint8_t(c); h += h << 10; h ^= h >> 6; }
    h += h << 3; h ^= h >> 11; h += h << 15; return h;
}

struct NativeContext { uint64_t* retVal; uint64_t argCount; uint64_t* stackPtr; uint64_t dataCount; uint64_t space[24]; uint64_t stack[24]; };

extern "C" {
void* fake_native_table = nullptr;
void* fake_current_thread = nullptr;
uint8_t fake_in_session = 0;
uint8_t fake_loading_flag = 0;
struct { void** vtbl; } fake_script_handler_mgr;
void site_updateSingleScripts(void*);
void site_shutdownLoadingScreen();
void site_getNativeAddress(); void site_currentScriptThread(); void site_scriptHandlerManager(); void site_isInSession();
void site_initScriptThread(); void site_tickScriptThread(); void site_sysAlloc();
}

// ---------- world state ----------
static int g_frame = 0, g_time = 0;
static bool g_online = false, g_mounted = false, g_fireHeld = false, g_f7 = false, g_turretValid = true;
static int g_vehicle = 0, g_vehicleCount = 0, g_modelRequests = 0, g_deleted = 0, g_wheelsDeleted = 0, g_animStops = 0, g_fx = 0;
static bool g_animPlaying = false;
static void* g_registered = nullptr;
static bool g_wrongThread = false;
static std::vector<int> g_shotTimes;
static std::vector<uint32_t> g_shotWeapons;
static std::vector<int> g_shotDamage;
static std::set<uint32_t> g_disabledThisFrame;
static std::vector<int> g_sprintBlockedFrames;
static std::string g_attachBone, g_animDict, g_animName;
static float g_attach[6];
static bool g_frozen = false, g_attached = false;
static int g_lock = 0, g_follows = 0;
static uint32_t g_createdModel = 0, g_unarmedHash = 0;
static int g_animFlags = 0;
static std::set<uint64_t> g_unknown;
static int g_fails = 0;

static float F(NativeContext* c, int i) { float f; std::memcpy(&f, &c->stack[i], 4); return f; }
static int32_t I(NativeContext* c, int i) { return int32_t(c->stack[i]); }
static const char* S(NativeContext* c, int i) { return reinterpret_cast<const char*>(c->stack[i]); }
static void R(NativeContext* c, uint64_t v) { c->retVal[0] = v; }
static void RV(NativeContext* c, float x, float y, float z) {
    std::memset(c->retVal, 0, 24);
    std::memcpy(&c->retVal[0], &x, 4); std::memcpy(&c->retVal[1], &y, 4); std::memcpy(&c->retVal[2], &z, 4);
}

using H = void (*)(NativeContext*);
static std::map<uint64_t, H> g_natives;
static void check_thread() { if (!g_registered || fake_current_thread != g_registered) g_wrongThread = true; }
#define N(hash, body) g_natives[hash] = [](NativeContext* c) { check_thread(); (void)c; body; }

static void install_natives() {
    N(0x096275889B8E0EE0, R(c, 1));                                     // PLAYER_PED_ID
    N(0xFD340785ADF8CFB7, R(c, joaat(S(c, 0))));                        // GET_HASH_KEY
    N(0x9DE624D2FC4B603F, R(c, g_online));                              // NETWORK_IS_SESSION_STARTED
    N(0x10FAB35428CCC9D7, R(c, 0));                                     // NETWORK_IS_GAME_IN_PROGRESS
    N(0x535384D6067BA42E, R(c, 0));                                     // IS_PAUSE_MENU_ACTIVE
    N(0x4F67E8ECA7D3F667, R(c, g_time));                                // GET_GAME_TIMER
    N(0xD42BD6EB2E0F1677, R(c, I(c, 0) != 0 && I(c, 0) == g_vehicle)); // DOES_ENTITY_EXIST
    N(0x7D5B1F88E7504BBA, R(c, 0));                                     // IS_ENTITY_DEAD
    N(0x460BC76A0E10655E, R(c, g_mounted));                             // IS_PED_ON_MOUNT
    N(0x997ABD671D25CA0B, R(c, 0));                                     // IS_PED_IN_ANY_VEHICLE
    N(0x9DE327631295B4C2, R(c, 0));                                     // IS_PED_SWIMMING
    N(0x47E4E977581C5B55, R(c, 0));                                     // IS_PED_RAGDOLL
    N(0xFA28FE3A6246FC30, ++g_modelRequests);                           // REQUEST_MODEL
    N(0x1283B8B89DD5D1B6, R(c, g_modelRequests >= 2));                  // HAS_MODEL_LOADED (loads on 2nd frame)
    N(0x4AD96EF928BD4F9A, {});                                          // SET_MODEL_AS_NO_LONGER_NEEDED
    N(0xAF35D0D2583051B0, { g_createdModel = uint32_t(c->stack[0]); g_vehicle = 77 + g_vehicleCount; ++g_vehicleCount; R(c, g_vehicle); }); // CREATE_VEHICLE
    N(0xE20A909D8C4A70F8, { int32_t* p = reinterpret_cast<int32_t*>(c->stack[0]); if (*p == g_vehicle) g_vehicle = 0; *p = 0; ++g_deleted; }); // DELETE_VEHICLE
    N(0xDC19C288082E586E, {});                                          // SET_ENTITY_AS_MISSION_ENTITY
    N(0xF66F820909453B8C, {});                                          // SET_ENTITY_COLLISION
    N(0xA5C38736C426FCB8, {});                                          // SET_ENTITY_INVINCIBLE
    N(0xD4F5EFB55769D272, R(c, 500 + I(c, 1)));                         // _BREAK_OFF_VEHICLE_WHEEL
    N(0x4CD38C78BD19A497, { *reinterpret_cast<int32_t*>(c->stack[0]) = 0; ++g_wheelsDeleted; }); // DELETE_ENTITY
    N(0xBACA8FE9C76C124E, { if (I(c, 0) == 1) g_attachBone = S(c, 1); R(c, 5); }); // GET_ENTITY_BONE_INDEX_BY_NAME
    N(0xC230DD956E2F5507, { float h = 90.0f; std::memcpy(c->retVal, &h, 4); });   // GET_ENTITY_HEADING (facing west)
    N(0x7D9EFB7AD6B19754, if (I(c, 0) == g_vehicle && I(c, 1)) g_frozen = true);  // FREEZE_ENTITY_POSITION
    N(0x96F78A6A075D55D9, if (I(c, 0) == g_vehicle) g_lock = I(c, 1));            // SET_VEHICLE_DOORS_LOCKED
    N(0x239A3351AC1DA385, { ++g_follows; for (int i = 0; i < 3; ++i) g_attach[i] = F(c, 1 + i); }); // SET_ENTITY_COORDS_NO_OFFSET
    N(0x9CC8314DFEDE441E, { for (int i = 0; i < 3; ++i) g_attach[3 + i] = F(c, 1 + i); });         // SET_ENTITY_ROTATION
    N(0x6B9BBD38AB0796DF, g_attached = true);                                                       // ATTACH_ENTITY_TO_ENTITY (must not be used)
    N(0x82CFA50E34681CA5, RV(c, 10.0f, 20.0f, 1.0f));                   // GET_WORLD_POSITION_OF_ENTITY_BONE
    N(0x1899F328B0E12848, RV(c, 0.0f, 1.0f, 0.0f));                     // GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS
    N(0x5352E025EC2B416F, RV(c, 0.0f, 0.0f, 1.5f));                     // GET_FINAL_RENDERED_CAM_COORD
    N(0x602685BD85DD26CA, RV(c, 0.0f, 0.0f, 0.0f));                     // GET_FINAL_RENDERED_CAM_ROT (facing north)
    N(0x937C71165CF334B3, R(c, uint32_t(c->stack[0]) == joaat("WEAPON_TURRET_GATLING") ? g_turretValid : 1)); // IS_WEAPON_VALID
    N(0x867654CBC7606F2C, {                                             // SHOOT_SINGLE_BULLET_BETWEEN_COORDS
        g_shotTimes.push_back(g_time); g_shotWeapons.push_back(uint32_t(c->stack[8])); g_shotDamage.push_back(I(c, 6));
        float dy = F(c, 4) - F(c, 1);
        if (dy < 100.0f) { std::printf("FAIL bullet not heading north/far enough (dy=%.1f)\n", dy); ++g_fails; }
    });
    N(0x2E80BF72EF7C87AC, { ++g_fx; R(c, 1); });                        // START_PARTICLE_FX_NON_LOOPED_AT_COORD
    N(0xF3A21BCD95725A4A, R(c, 0));                                     // IS_CONTROL_PRESSED
    N(0xE2587F8CBBD87B1D, R(c, g_fireHeld && uint32_t(c->stack[1]) == joaat("INPUT_ATTACK"))); // IS_DISABLED_CONTROL_PRESSED
    N(0xFE99B66D079CF6BC, g_disabledThisFrame.insert(uint32_t(c->stack[1]))); // DISABLE_CONTROL_ACTION
    N(0xA862A2AD321F94B4, {});                                          // REQUEST_ANIM_DICT
    N(0x27FF6FE8009B40CA, R(c, 1));                                     // HAS_ANIM_DICT_LOADED
    N(0xEA47FE3719165B94, { g_animDict = S(c, 1); g_animName = S(c, 2); g_animFlags = I(c, 6); g_animPlaying = true; }); // TASK_PLAY_ANIM
    N(0xDEE49D5CA6C49148, R(c, g_animPlaying));                         // IS_ENTITY_PLAYING_ANIM
    N(0x97FF36A1D40EA00A, { g_animPlaying = false; ++g_animStops; });   // STOP_ANIM_TASK
    N(0xADF692B254977C0C, g_unarmedHash = uint32_t(c->stack[1]));       // SET_CURRENT_PED_WEAPON
}

static void unknown_native(NativeContext*) {}

extern "C" uintptr_t fake_get_native_address(uint64_t hash) {
    auto it = g_natives.find(hash);
    if (it == g_natives.end()) { g_unknown.insert(hash); return reinterpret_cast<uintptr_t>(&unknown_native); }
    return reinterpret_cast<uintptr_t>(it->second);
}
extern "C" uint8_t fake_noop() { return 0; }
extern "C" bool fake_update_single_scripts(void*) { return false; }
extern "C" void fake_shutdown_loading_screen() {}
extern "C" void fake_init_script_thread(void* t) { *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(t) + 0x8 + 0x14) = 0xC0FFEE; }
extern "C" int fake_tick_script_thread(void* t, uint32_t ops) {
    auto vt = *reinterpret_cast<void***>(t);
    return reinterpret_cast<int (*)(void*, int)>(vt[2])(t, int(ops));
}
extern "C" void* fake_sys_alloc(size_t n) { return std::calloc(1, n); }
static void fake_register_script(void* mgr, void* thread) {
    if (mgr != &fake_script_handler_mgr) { std::printf("FAIL RegisterScript got wrong this\n"); ++g_fails; }
    g_registered = thread;
}
static void* g_mgrVtbl[16];

// ---------- user32 seen by the mod ----------
static HWND WINAPI fakeGetForegroundWindow() { return reinterpret_cast<HWND>(0x1234); }
static HWND WINAPI fakeFindWindowA(LPCSTR cls, LPCSTR) { return cls && !std::strcmp(cls, "sgaWindow") ? reinterpret_cast<HWND>(0x1234) : nullptr; }
static SHORT WINAPI fakeGetAsyncKeyState(int vk) { return (vk == VK_F7 && g_f7) ? SHORT(0x8000) : 0; }

static void patch_iat(HMODULE mod, const char* dll, const char* fn, void* repl) {
    auto base = reinterpret_cast<uint8_t*>(mod);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<char*>(base + d->Name), dll)) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(ibn->Name), fn)) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, 8, PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<ULONGLONG>(repl);
            VirtualProtect(&slots->u1.Function, 8, old, &old);
            return;
        }
    }
    std::printf("FAIL could not patch %s!%s\n", dll, fn); ++g_fails;
}

static void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fails;
}

static void frame() {
    g_disabledThisFrame.clear();
    site_updateSingleScripts(nullptr);
    if (g_disabledThisFrame.count(joaat("INPUT_SPRINT"))) g_sprintBlockedFrames.push_back(g_frame);
    ++g_frame;
    g_time += 16;
}
static void run_until(int f) { while (g_frame < f) frame(); }

int main(int argc, char** argv) {
    install_natives();
    for (auto& s : g_mgrVtbl) s = reinterpret_cast<void*>(&fake_noop);
    g_mgrVtbl[10] = reinterpret_cast<void*>(&fake_register_script);
    fake_script_handler_mgr.vtbl = g_mgrVtbl;
    fake_native_table = &g_natives;
    volatile void* keep[] = {(void*)&site_getNativeAddress, (void*)&site_currentScriptThread, (void*)&site_scriptHandlerManager,
                             (void*)&site_isInSession, (void*)&site_initScriptThread, (void*)&site_tickScriptThread, (void*)&site_sysAlloc};
    (void)keep;

    const char* asi = argc > 1 ? argv[1] : "ArthurGatling.asi";
    HMODULE mod = LoadLibraryA(asi);
    expect(mod != nullptr, "ASI loads like Ultimate ASI Loader loads it");
    if (!mod) return 1;
    patch_iat(mod, "USER32.dll", "GetForegroundWindow", (void*)&fakeGetForegroundWindow);
    patch_iat(mod, "USER32.dll", "FindWindowA", (void*)&fakeFindWindowA);
    patch_iat(mod, "USER32.dll", "GetAsyncKeyState", (void*)&fakeGetAsyncKeyState);

    GetCommandLineA();  // the game's first call after unpacking -> mod boots
    run_until(5);
    expect(g_registered == nullptr, "no script thread before the loading screen ends");
    site_shutdownLoadingScreen();
    run_until(8);
    expect(g_registered != nullptr, "script thread registered with the game after the loading screen");

    // F7 out: staged spawn (1 spawn, 2 follow after 15 frames, 3 wheels after 15 more)
    g_f7 = true; run_until(10); g_f7 = false; run_until(16);
    expect(g_vehicleCount == 1 && g_createdModel == joaat("gatling_gun"), "F7 spawns the gatling_gun model");
    expect(g_frozen && g_lock == 2, "stage 1: spawned frozen and locked");
    expect(g_follows == 0 && g_wheelsDeleted == 0 && g_animDict.empty(), "stage 1 waits: no follow, wheels or pose yet");
    run_until(30);
    expect(g_follows > 0 && g_wheelsDeleted == 0, "stage 2: follows the hand, wheels not touched yet");
    // heading 90 (west): forward = (-1,0), right = (0,1); offset (0,0.35,-0.10) from hand (10,20,1)
    expect(std::fabs(g_attach[0] - 9.65f) < 1e-3 && std::fabs(g_attach[1] - 20.0f) < 1e-3 && std::fabs(g_attach[2] - 0.9f) < 1e-3,
           "held at hand + holdOffset in Arthur's frame");
    expect(std::fabs(g_attach[5] - 90.0f) < 1e-3, "faces Arthur's heading");
    run_until(48);
    expect(g_wheelsDeleted == 2, "stage 3: carriage wheels 0 and 1 stripped");
    expect(!g_attached, "never attaches the vehicle to Arthur (the 1.0.0 crash suspect)");
    expect(g_attachBone == "PH_R_Hand", "follows Arthur's right hand bone");
    expect(g_animDict == "mech_carry_box" && g_animName == "idle" && g_animFlags == 49, "hip-hold pose from the weapon sheet");
    expect(g_unarmedHash == joaat("WEAPON_UNARMED"), "Arthur's own gun holstered");
    expect(g_shotTimes.empty(), "no shots without the trigger");
    expect(!g_sprintBlockedFrames.empty() && g_sprintBlockedFrames.back() == g_frame - 1, "sprint blocked while held");

    // hold fire for ~1.5 s
    int fireStart = g_time;
    g_fireHeld = true; run_until(48 + 94); g_fireHeld = false; run_until(147);
    int shots = int(g_shotTimes.size());
    std::printf("     %d shots in %d ms of trigger\n", shots, g_time - fireStart - 5 * 16);
    expect(shots >= 18 && shots <= 30, "spray rate in the expected band (spin-up 160 ms -> 55 ms)");
    bool spinsUp = shots > 6 && (g_shotTimes[2] - g_shotTimes[1]) > (g_shotTimes[shots - 1] - g_shotTimes[shots - 2]);
    expect(spinsUp, "barrel spins up: later shots come faster than early ones");
    expect(int(g_shotTimes.size()) == g_fx, "a muzzle flash for every shot");
    bool dmg = true; for (int d : g_shotDamage) dmg &= d == 35;
    expect(dmg, "35 damage per bullet from the weapon sheet");
    bool turret = true; for (auto w : g_shotWeapons) turret &= w == joaat("WEAPON_TURRET_GATLING");
    expect(turret, "bullets are WEAPON_TURRET_GATLING");
    int after = int(g_shotTimes.size()); run_until(157);
    expect(int(g_shotTimes.size()) == after, "firing stops when the trigger is released");

    // F7 away
    g_f7 = true; run_until(159); g_f7 = false; run_until(162);
    expect(g_deleted == 1 && g_vehicle == 0 && !g_animPlaying, "F7 again puts it away (prop deleted, pose stopped)");
    expect(g_sprintBlockedFrames.back() < g_frame - 2, "sprint free again once put away");

    // online: nothing happens
    g_online = true; g_f7 = true; run_until(165); g_f7 = false; run_until(172);
    expect(g_vehicleCount == 1, "online session: F7 does nothing");
    g_online = false; run_until(174);

    // out again, then online mid-hold -> put away
    g_turretValid = false;
    g_f7 = true; run_until(176); g_f7 = false; run_until(196);
    expect(g_vehicleCount == 2, "back in story mode F7 works again");
    g_fireHeld = true; run_until(206); g_fireHeld = false;
    expect(g_shotWeapons.back() == joaat("WEAPON_REPEATER_CARBINE"), "falls back to WEAPON_REPEATER_CARBINE if the turret weapon is invalid");
    g_online = true; run_until(208); g_online = false;
    expect(g_vehicle == 0 && g_deleted == 2, "going online puts the Gatling away");

    // riding a horse puts it away
    g_f7 = true; run_until(210); g_f7 = false; run_until(216);
    g_mounted = true; run_until(218); g_mounted = false;
    expect(g_vehicle == 0 && g_deleted == 3, "mounting a horse puts it away");

    // every pattern must land exactly on its planted site (read back from the mod's log)
    struct Want { const char* id; const void* addr; } wants[] = {
        {"getNativeAddress", (void*)&site_getNativeAddress}, {"currentScriptThread", &fake_current_thread},
        {"scriptHandlerManager", &fake_script_handler_mgr}, {"isInSession", &fake_in_session},
        {"updateSingleScripts", (void*)&site_updateSingleScripts}, {"shutdownLoadingScreen", (void*)&site_shutdownLoadingScreen},
        {"initScriptThread", (void*)&site_initScriptThread}, {"tickScriptThread", (void*)&site_tickScriptThread},
        {"sysAlloc", (void*)&fake_sys_alloc}};
    for (auto& w : wants) {
        FILE* f = std::fopen("ArthurGatling.log", "r");
        char line[512]; void* got = nullptr;
        while (f && std::fgets(line, sizeof line, f)) {
            char id[64]; void* a;
            const char* p = std::strstr(line, "pattern ");
            if (p && std::sscanf(p, "pattern %63s found (%p)", id, &a) == 2 && !std::strcmp(id, w.id)) got = a;
        }
        if (f) std::fclose(f);
        char what[128]; std::snprintf(what, sizeof what, "pattern %s resolves to its exact site", w.id);
        expect(got == w.addr, what);
    }
    expect(!g_wrongThread, "every native ran on the mod's registered script thread");
    expect(g_unknown.empty(), "no native outside the natives sheet was called");
    for (auto h : g_unknown) std::printf("     unknown native 0x%016llX\n", (unsigned long long)h);
    std::printf(g_fails ? "RESULT: %d failure(s)\n" : "RESULT: all checks passed\n", g_fails);
    return g_fails ? 1 : 0;
}
