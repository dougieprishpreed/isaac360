// ISAAC360_TRACE_LOOKS=1: the player's appearance changes in the log, for look bugs only the console shows (boots 24
// and 25: a pill's or a spider web's colour stayed on Isaac until another colour change; Money = Power's costume was
// missing, then shown after the next pickup, then gone again). The same lines from a PC run of the same pickups are
// the reference.
// - Entity::SetColor (0x677f50: colour, duration, priority, fadeout, share; ret 0x14) with a duration of 0 or less
//   makes the colour the entity's base colour, the one each reset goes back to, so it stays. The player's vtable slot
//   (0xb2cd60 + 0x30) points at a thunk that logs those calls (the caller, the colour) and runs the game's function.
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
unsigned g_lines = 0;

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
    if (duration <= 0 && lines_left(1)) {
        uint32_t col = RD32(sp + 4);
        float f[11];
        for (int i = 0; i < 11; ++i) f[i] = U2F(RD32(col + 4 * i));
        recomp_log("looks: base colour from %08x at frame %u: rgba %.2f %.2f %.2f %.2f offset %.2f %.2f %.2f colorize "
                   "%.2f %.2f %.2f %.2f (duration %d, priority %d, fadeout %u, share %u)", RD32(sp), gl_frame_count(),
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

// the game logged a room it enters (shims_crt.cpp): the costume map after the last add
void looks_room_entered() {
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
    }
    return 0;
}
