// Arthur's Gatling: every system here is a row in sheets/systems.json.
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include "generated.h"
#include "log.h"

using namespace natives;
static constexpr const GunDef& kGun = guns::arthur_gatling;

bool rtInSessionFlag();  // runtime.cpp, logged only

namespace {
struct State {
    bool wantOut = false;
    bool held = false;
    Vehicle prop = 0;
    int requestedAt = 0;
    Hash bulletHash = 0;
    int spinStart = -1;
    int nextShot = 0;
    int burstShots = 0;
    bool keyWasDown = false;
    bool onlineLogged = false;
    bool sessionFlagLogged = false;
    bool sanityLogged = false;
} S;

const Hash kModel = joaat(kGun.propModel);

bool keyDown(const ControlDef& c) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg != FindWindowA("sgaWindow", nullptr)) return false;
    return (GetAsyncKeyState(int(c.code)) & 0x8000) != 0;
}

float frand(float lo, float hi) { return lo + (hi - lo) * (std::rand() / float(RAND_MAX)); }

// --- auto_put_away / cleanup ---
void putAway(Ped ped, const char* why) {
    if (S.prop && DOES_ENTITY_EXIST(S.prop)) DELETE_VEHICLE(&S.prop);
    S.prop = 0;
    if (ped) STOP_ANIM_TASK(ped, kGun.holdAnimDict, kGun.holdAnimName, 1.0f);
    if (S.held) logf("Gatling put away (%s)", why);
    S.held = false;
    S.wantOut = false;
    S.spinStart = -1;
}

void playHoldPose(Ped ped) {
    REQUEST_ANIM_DICT(kGun.holdAnimDict);
    if (!HAS_ANIM_DICT_LOADED(kGun.holdAnimDict)) return;
    if (IS_ENTITY_PLAYING_ANIM(ped, kGun.holdAnimDict, kGun.holdAnimName, 3)) return;
    TASK_PLAY_ANIM(ped, kGun.holdAnimDict, kGun.holdAnimName, 8.0f, -8.0f, -1, kGun.holdAnimFlags, 0.0f, false, 0, false, "", false);
}

// --- spawn_hold ---
void trySpawn(Ped ped) {
    REQUEST_MODEL(kModel, false);
    if (!HAS_MODEL_LOADED(kModel)) {
        if (GET_GAME_TIMER() - S.requestedAt > 5000) {
            logf("model %s did not load in 5 s - giving up", kGun.propModel);
            S.wantOut = false;
        }
        return;
    }
    Vector3 at = GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ped, 0.0f, 1.0f, 0.0f);
    S.prop = CREATE_VEHICLE(kModel, at.x, at.y, at.z, 0.0f, false, false, true, false);
    SET_MODEL_AS_NO_LONGER_NEEDED(kModel);
    if (!S.prop) { logf("CREATE_VEHICLE(%s) failed", kGun.propModel); S.wantOut = false; return; }
    SET_ENTITY_AS_MISSION_ENTITY(S.prop, true, true);
    SET_ENTITY_COLLISION(S.prop, false, false);
    SET_ENTITY_INVINCIBLE(S.prop, true);
    for (int i = 0; i < kGun.stripWheelCount; ++i) {
        Entity wheel = BREAK_OFF_VEHICLE_WHEEL(S.prop, kGun.stripWheels[i]);
        if (wheel) { SET_ENTITY_AS_MISSION_ENTITY(wheel, true, true); DELETE_ENTITY(&wheel); }
    }
    int bone = GET_ENTITY_BONE_INDEX_BY_NAME(ped, kGun.attachBone);
    ATTACH_ENTITY_TO_ENTITY(S.prop, ped, bone,
                            kGun.attachOffset[0], kGun.attachOffset[1], kGun.attachOffset[2],
                            kGun.attachRot[0], kGun.attachRot[1], kGun.attachRot[2],
                            false, false, false, false, 2, true, false, false);
    SET_CURRENT_PED_WEAPON(ped, joaat("WEAPON_UNARMED"), true, 0, false, false);
    S.bulletHash = joaat(kGun.bulletWeapon);
    if (!IS_WEAPON_VALID(S.bulletHash)) {
        logf("%s not valid here, using %s", kGun.bulletWeapon, kGun.bulletWeaponFallback);
        S.bulletHash = joaat(kGun.bulletWeaponFallback);
    }
    S.held = true;
    logf("Gatling out: prop %d on bone %s (%d), bullets %08X", S.prop, kGun.attachBone, bone, S.bulletHash);
}

// --- fire ---
void fire(Ped ped) {
    const int pad = controls::fire.pad;
    DISABLE_CONTROL_ACTION(pad, controls::attack_block.code, false);
    DISABLE_CONTROL_ACTION(controls::melee.pad, controls::melee.code, false);
    bool trigger = IS_DISABLED_CONTROL_PRESSED(pad, controls::fire.code);
    bool aiming = IS_CONTROL_PRESSED(controls::aim.pad, controls::aim.code);
    int now = GET_GAME_TIMER();
    if (!trigger || IS_PED_RAGDOLL(ped)) {
        if (S.spinStart >= 0) logf("burst: %d shots in %d ms", S.burstShots, now - S.spinStart);
        S.spinStart = -1;
        return;
    }
    if (S.spinStart < 0) { S.spinStart = now; S.nextShot = now; S.burstShots = 0; }

    float t = float(now - S.spinStart) / float(kGun.spinUpMs);
    if (t > 1.0f) t = 1.0f;
    int interval = int(kGun.fireIntervalStartMs + (kGun.fireIntervalMinMs - kGun.fireIntervalStartMs) * t);

    int shots = 0;
    while (now >= S.nextShot && shots < 3) {
        S.nextShot += interval;
        ++shots;
        ++S.burstShots;
        Vector3 cam = GET_FINAL_RENDERED_CAM_COORD();
        Vector3 rot = GET_FINAL_RENDERED_CAM_ROT(2);
        float spread = aiming ? kGun.spreadDeg : kGun.spreadDeg * 2.0f;
        const float d2r = 3.14159265f / 180.0f;
        float pitch = (rot.x + frand(-spread, spread)) * d2r;
        float yaw = (rot.z + frand(-spread, spread)) * d2r;
        float dx = -std::sin(yaw) * std::cos(pitch), dy = std::cos(yaw) * std::cos(pitch), dz = std::sin(pitch);

        Vector3 muzzle{};
        int mb = GET_ENTITY_BONE_INDEX_BY_NAME(S.prop, kGun.muzzleBone);
        if (mb >= 0) muzzle = GET_WORLD_POSITION_OF_ENTITY_BONE(S.prop, mb);
        else muzzle = GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(S.prop, kGun.muzzleFallbackOffset[0], kGun.muzzleFallbackOffset[1], kGun.muzzleFallbackOffset[2]);
        float sx = muzzle.x + dx * 0.3f, sy = muzzle.y + dy * 0.3f, sz = muzzle.z + dz * 0.3f;
        float tx = cam.x + dx * kGun.range, ty = cam.y + dy * kGun.range, tz = cam.z + dz * kGun.range;
        SHOOT_SINGLE_BULLET_BETWEEN_COORDS(sx, sy, sz, tx, ty, tz, kGun.damagePerBullet, true, S.bulletHash, ped, true, false, kGun.bulletSpeed, false);
        START_PARTICLE_FX_NON_LOOPED_AT_COORD(kGun.muzzleFx, sx, sy, sz, rot.x, 0.0f, rot.z, 1.0f, false, false, false);
    }
    if (now >= S.nextShot) S.nextShot = now + interval;  // never build a backlog
}
}  // namespace

void gatlingFrame() {
    if (!S.sanityLogged) {
        S.sanityLogged = true;
        Hash h = GET_HASH_KEY("INPUT_ATTACK");
        logf("native call check: GET_HASH_KEY(INPUT_ATTACK) = %08X (%s)", h, h == controls::fire.code ? "ok" : "MISMATCH");
    }
    Ped ped = PLAYER_PED_ID();

    // online_guard
    if (NETWORK_IS_SESSION_STARTED() || NETWORK_IS_GAME_IN_PROGRESS()) {
        if (!S.onlineLogged) { S.onlineLogged = true; logf("online session -> mod idle"); }
        if (S.held || S.prop) putAway(ped, "online");
        return;
    }
    S.onlineLogged = false;
    if (rtInSessionFlag() && !S.sessionFlagLogged) { S.sessionFlagLogged = true; logf("note: game's in-session flag reads true"); }

    // toggle
    bool down = keyDown(controls::toggle);
    if (down && !S.keyWasDown && !IS_PAUSE_MENU_ACTIVE()) {
        if (S.held || S.wantOut) putAway(ped, "F7");
        else { S.wantOut = true; S.requestedAt = GET_GAME_TIMER(); logf("F7: bringing the Gatling out"); }
    }
    S.keyWasDown = down;

    if (S.wantOut && !S.held) {
        if (IS_ENTITY_DEAD(ped) || IS_PED_ON_MOUNT(ped) || IS_PED_IN_ANY_VEHICLE(ped, false) || IS_PED_SWIMMING(ped)) {
            S.wantOut = false;
            logf("can't bring it out right now (dead, riding or swimming)");
        } else {
            trySpawn(ped);
        }
    }
    if (!S.held) return;

    // auto_put_away
    if (!DOES_ENTITY_EXIST(S.prop)) { S.prop = 0; putAway(ped, "prop gone"); return; }
    if (IS_ENTITY_DEAD(ped)) { putAway(ped, "Arthur died"); return; }
    if (IS_PED_ON_MOUNT(ped) || IS_PED_IN_ANY_VEHICLE(ped, false)) { putAway(ped, "mounted"); return; }
    if (IS_PED_SWIMMING(ped)) { putAway(ped, "swimming"); return; }

    // sprint_lock
    DISABLE_CONTROL_ACTION(controls::sprint.pad, controls::sprint.code, false);

    playHoldPose(ped);
    fire(ped);
}
