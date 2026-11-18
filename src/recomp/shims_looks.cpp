// ISAAC360_TRACE_LOOKS=1: the player's appearance changes in the log, for look bugs only the console shows (boots 24
// and 25: a pill's or a spider web's colour stayed on Isaac until another colour change; Money = Power's costume was
// missing, then shown after the next pickup, then gone again). The same lines from a PC run of the same pickups are
// the reference.
// - Entity::SetColor (0x677f50: colour, duration, priority, fadeout, share; ret 0x14) with a duration of 0 or less
//   makes the colour the entity's base colour, the one each reset goes back to, so it stays. The player's vtable slot
//   (0xb2cd60 + 0x30) points at a thunk that logs those calls (the caller, the colour) and runs the game's function.
// - At each room the game enters: the player's colour against its base colour (+0x1b0, what a reset copies to the
//   sprite's +0xe4) while it has no colour entries, and a base colour unlike the first seen; either still the same at
//   a room 30 s later is a colour that stays, logged with the player's 16 colour changes before it (caller, duration,
//   colour), whatever set it.
// - AddCostume (0x710f70, a HOST hook in translate.py): each add (the item, the caller) with the player's costume
//   records (+0x1168..+0x116c, 0x130 bytes each) and its 16 layer slots (+0x1174, 16 bytes each) as they are before
//   it, and once more when the game next enters a room (after the add).
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void F_677f50(X86* c);
uint32_t gl_frame_count();                          // shims_gl.cpp

namespace {
const uint32_t kPlayerVtable = 0xb2cd60, kSetColorSlot = kPlayerVtable + 0x30, kSetColor = 0x677f50;
const unsigned kMaxLines = 4000;                    // the trace's share of the log (the console's USB drive)
bool g_on = false;
uint32_t g_player = 0;                              // the player whose costume map is due at the next room
uint32_t g_lastPlayer = 0;                          // the player last seen (SetColor, AddCostume)
unsigned g_lines = 0;
// the player's last colour changes (each SetColor through its vtable), logged when a colour stays
struct ColorCall {
    uint32_t from, frame;
    int32_t duration, priority;
    float c[11];                                    // a Color: tint rgba, colorize rgba, offset rgb
};
ColorCall g_recent[16];
unsigned g_recentAt = 0;
unsigned g_staysLogged = 0;

bool lines_left(unsigned n) {
    if (g_lines + n > kMaxLines) return false;
    g_lines += n;
    return true;
}

void dump_costumes(uint32_t p, const char* when) {
    uint32_t begin = RD32(p + 0x1168), end = RD32(p + 0x116c);
    uint32_t n = end >= begin && end - begin <= 64 * 0x130 ? (end - begin) / 0x130 : 0;
    if (!lines_left(5 + n)) return;
    recomp_log("looks: costume map %s: %u records (%08x..%08x)", when, n, begin, end);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t r = begin + i * 0x130;
        uint32_t cfg = RD32(r);
        int id = cfg && cfg + 8 <= g_mem_size ? (int)RD32(cfg + 4) : -1;
        recomp_log("looks:  %u: +00 %08x (id %d) +04 %08x +08 %08x +0c %08x +10 %08x; +110 %08x +114 %08x +118 %08x "
                   "+11c %08x +120 %08x +124 %08x +128 %08x +12c %08x", i, cfg, id, RD32(r + 4), RD32(r + 8),
                   RD32(r + 0xc), RD32(r + 0x10), RD32(r + 0x110), RD32(r + 0x114), RD32(r + 0x118), RD32(r + 0x11c),
                   RD32(r + 0x120), RD32(r + 0x124), RD32(r + 0x128), RD32(r + 0x12c));
    }
    for (int row = 0; row < 4; ++row) {
        char buf[256];
        int at = snprintf(buf, sizeof(buf), "looks:  layers %2d-%2d:", row * 4, row * 4 + 3);
        for (int k = 0; k < 4; ++k) {
            uint32_t s = p + 0x1174 + 16 * (row * 4 + k);
            at += snprintf(buf + at, sizeof(buf) - at, " [%d %08x %08x %08x]", (int)RD32(s), RD32(s + 4), RD32(s + 8),
                           RD32(s + 12));
        }
        recomp_log("%s", buf);
    }
}

// the player's SetColor through its vtable: a base colour (duration 0 or less) logged, then the game's function
void set_color_traced(X86* c) {
    uint32_t sp = c->r[4];
    int32_t duration = (int32_t)RD32(sp + 8);
    g_lastPlayer = c->r[1];
    ColorCall& r = g_recent[g_recentAt++ % 16];
    r.from = RD32(sp);
    r.frame = gl_frame_count();
    r.duration = duration;
    r.priority = (int32_t)RD32(sp + 12);
    for (int i = 0; i < 11; ++i) r.c[i] = U2F(RD32(RD32(sp + 4) + 4 * i));
    if (duration <= 0 && lines_left(1)) {
        uint32_t col = RD32(sp + 4);
        float f[11];
        for (int i = 0; i < 11; ++i) f[i] = U2F(RD32(col + 4 * i));
        recomp_log("looks: base colour from %08x at frame %u: rgba %.2f %.2f %.2f %.2f colorize %.2f %.2f %.2f %.2f offset "
                   "%.2f %.2f %.2f (duration %d, priority %d, fadeout %u, share %u)", RD32(sp), gl_frame_count(),
                   f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], duration, (int32_t)RD32(sp + 12),
                   RD8(sp + 16), RD8(sp + 20));
    }
    F_677f50(c);                                    // pops the return address and its five arguments
}
}  // namespace

void looks_trace_init() {
    g_on = getenv("ISAAC360_TRACE_LOOKS") != 0;
    if (!g_on) return;
    if (RD32(kSetColorSlot) != kSetColor) {
        recomp_log("looks: the player's vtable slot holds %08x, not SetColor: colours not traced", RD32(kSetColorSlot));
        return;
    }
    WR32(kSetColorSlot, thunk_for(set_color_traced, "isaac360 SetColor trace"));
    recomp_log("looks: tracing the player's base colours and costumes");
}

namespace {
// the player's colour against its base with no colour entries left (the reset copies the base colour at +0x1b0 to the
// sprite's at +0xe4 when the last entry ends), and the base against the first seen on this player: either, seen at a
// room and still the same at a room 30 s (1800 frames) or more later, is a colour that stays (an effect's own colour
// ends sooner: Power Pill's green base, its flashes), logged with the player's colour changes from when it was first
// seen (its cause among them)
struct Pending {
    bool active;
    int kind;                                       // 1 the colour without entries, 2 the base colour
    uint32_t since;
    float now[11], base[11];
    ColorCall recent[16];
    unsigned recentAt;
};
Pending g_pending;

void check_colour(uint32_t p) {
    if (!p || RD32(p) != kPlayerVtable || g_staysLogged >= 20) return;
    float now[11], base[11];
    for (int i = 0; i < 11; ++i) {
        now[i] = U2F(RD32(p + 0xe4 + 4 * i));
        base[i] = U2F(RD32(p + 0x1b0 + 4 * i));
    }
    static uint32_t basePlayer = 0;
    static float firstBase[11];
    if (p != basePlayer) {
        basePlayer = p;
        memcpy(firstBase, base, sizeof(firstBase));
        g_pending.active = false;
    }
    bool entries = RD32(p + 0x1a4) != RD32(p + 0x1a8);
    int kind = !entries && memcmp(now, base, sizeof(now)) ? 1 : memcmp(base, firstBase, sizeof(base)) ? 2 : 0;
    uint32_t frame = gl_frame_count();
    if (!kind) {
        g_pending.active = false;
        return;
    }
    Pending& q = g_pending;
    if (!q.active || q.kind != kind || memcmp(q.now, now, sizeof(now)) || memcmp(q.base, base, sizeof(base))) {
        q.active = true;
        q.kind = kind;
        q.since = frame;
        memcpy(q.now, now, sizeof(now));
        memcpy(q.base, base, sizeof(base));
        memcpy(q.recent, g_recent, sizeof(g_recent));
        q.recentAt = g_recentAt;
        return;
    }
    if (frame - q.since < 1800 || !lines_left(2 + 16)) return;
    ++g_staysLogged;
    q.active = false;
    recomp_log("looks: the player's colour %s from frame %u to %u: now rgba %.2f %.2f %.2f %.2f colorize %.2f %.2f %.2f "
               "%.2f offset %.2f %.2f %.2f; base rgba %.2f %.2f %.2f %.2f colorize %.2f %.2f %.2f %.2f offset %.2f %.2f %.2f",
               kind == 1 ? "STAYED with no colour entries" : "kept a changed base colour", q.since, frame, now[0], now[1],
               now[2], now[3], now[4], now[5], now[6], now[7], now[8], now[9], now[10], base[0], base[1], base[2], base[3],
               base[4], base[5], base[6], base[7], base[8], base[9], base[10]);
    recomp_log("looks:  its last colour changes before frame %u, oldest first:", q.since);
    for (unsigned i = 0; i < 16; ++i) {
        const ColorCall& r = q.recent[(q.recentAt + i) % 16];
        if (!r.from) continue;
        recomp_log("looks:   frame %u from %08x: duration %d priority %d rgba %.2f %.2f %.2f %.2f colorize %.2f %.2f %.2f "
                   "%.2f offset %.2f %.2f %.2f", r.frame, r.from, r.duration, r.priority, r.c[0], r.c[1], r.c[2], r.c[3],
                   r.c[4], r.c[5], r.c[6], r.c[7], r.c[8], r.c[9], r.c[10]);
    }
}
}  // namespace

uint32_t looks_player() { return g_lastPlayer; }   // the player last seen (host_win32.cpp's ISAAC360_WATCH)

// a test script's "looks: probe <what>" (Isaac.DebugString, shims_crt.cpp): the player's colour and base colour now
void looks_probe(const char* what) {
    uint32_t p = g_lastPlayer;
    if (!g_on || !p || RD32(p) != kPlayerVtable) return;
    float now[11], base[11];
    for (int i = 0; i < 11; ++i) {
        now[i] = U2F(RD32(p + 0xe4 + 4 * i));
        base[i] = U2F(RD32(p + 0x1b0 + 4 * i));
    }
    recomp_log("looks: probe %s: now rgba %.2f %.2f %.2f %.2f colorize %.2f %.2f %.2f %.2f offset %.2f %.2f %.2f; base "
               "colorize %.2f %.2f %.2f %.2f; %u colour entries", what, now[0], now[1], now[2], now[3], now[4], now[5],
               now[6], now[7], now[8], now[9], now[10], base[4], base[5], base[6], base[7],
               (RD32(p + 0x1a8) - RD32(p + 0x1a4)) / 0x3c);
}

// the game logged a room it enters (shims_crt.cpp): the costume map after the last add, and the colour check
void looks_room_entered() {
    if (!g_on) return;
    check_colour(g_lastPlayer);
    if (!g_player) return;
    dump_costumes(g_player, "after the add");
    g_player = 0;
}

// AddCostume(this ecx, item config, flag): logged with the map as it was, then the game's code runs
extern "C" int hle_trace_costume(X86* c) {
    if (!g_on) return 0;
    uint32_t p = c->r[1], sp = c->r[4], cfg = RD32(sp + 4);
    int id = cfg && cfg + 8 <= g_mem_size ? (int)RD32(cfg + 4) : -1;
    if (lines_left(1))
        recomp_log("looks: AddCostume item %d (config %08x, flag %u) for %08x (vtable %08x) from %08x at frame %u", id,
                   cfg, RD32(sp + 8), p, RD32(p), RD32(sp), gl_frame_count());
    if (RD32(p) == kPlayerVtable) {
        dump_costumes(p, "before the add");
        g_player = p;
        g_lastPlayer = p;
    }
    return 0;
}
