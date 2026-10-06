// Calling RDR2 natives without Script Hook: look up the handler by hash with the
// game's own resolver (game_hooks.json: getNativeAddress) and call it with a
// native context laid out the way the game expects.
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

using Ped = int32_t;
using Player = int32_t;
using Entity = int32_t;
using Vehicle = int32_t;
using BOOL = int32_t;
using Hash = uint32_t;

// RDR2 script vectors are three floats, each padded to 8 bytes.
struct Vector3 {
    float x; uint32_t _px;
    float y; uint32_t _py;
    float z; uint32_t _pz;
};
static_assert(sizeof(Vector3) == 24, "script vector layout");

struct NativeContext {
    uint64_t* retVal;
    uint64_t argCount;
    uint64_t* stackPtr;
    uint64_t dataCount;
    uint64_t spaceForResults[24];
    uint64_t stack[24];
};
static_assert(offsetof(NativeContext, stack) == 0xE0, "native context layout");

using NativeHandler = void (*)(NativeContext*);
using GetNativeAddressFn = uintptr_t (*)(uint64_t);

namespace rt {
extern GetNativeAddressFn getNativeAddress;
void reportMissingNative(uint64_t hash);
}

constexpr uint32_t joaat(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        h += uint8_t(c);
        h += h << 10;
        h ^= h >> 6;
    }
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

template <class T>
inline void pushArg(NativeContext& ctx, T value) {
    uint64_t slot = 0;
    static_assert(sizeof(T) <= sizeof(slot), "argument too large");
    std::memcpy(&slot, &value, sizeof(T));
    ctx.stack[ctx.argCount++] = slot;
}

template <class R, uint64_t HASH, class... A>
inline R invoke(A... args) {
    static NativeHandler handler = nullptr;
    if (!handler) {
        handler = reinterpret_cast<NativeHandler>(rt::getNativeAddress ? rt::getNativeAddress(HASH) : 0);
        if (!handler) {
            rt::reportMissingNative(HASH);
            if constexpr (!std::is_void_v<R>) return R{};
            else return;
        }
    }
    NativeContext ctx{};
    ctx.retVal = ctx.stack;
    ctx.stackPtr = ctx.stack;
    (pushArg(ctx, args), ...);
    handler(&ctx);
    if constexpr (!std::is_void_v<R>) {
        R result{};
        std::memcpy(&result, ctx.stack, sizeof(R));
        return result;
    }
}
