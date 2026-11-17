// Loads the game makes at a run's first door, made at the end of the frame that entered the run's first room instead:
// that frame is the run's start-up load (~1.7 s on the console), which the player waits through anyway, where the
// first door was a hitch in play (boots 24 and 25: 219 + 364 ms, against 100-200 for later new rooms; the PC's first
// door ran 544,000 translated calls against 185,000 for a later new room, its reads mostly the boss intro's).
//
// The boss intro ("versus" screen) at Game+0x1B470: F_7d6ed0 loads gfx/ui/boss/versusscreen.anm2 into its sprite
// (+0x38) unless that sprite is loaded (its flag +0x109, so +0x141), and the run's first door is the first to call
// it. Here the same Sprite::Load (F_406d50: thiscall, a const std::string& path and loadGraphics; ret 8) runs with
// the same arguments when the run's first floor (the game's "Level::Init m_Stage 1," line) enters its start room,
// so the first door finds the sprite loaded, as every later door does. ISAAC360_WARM=0 turns it off.
#include "runtime.h"
#include <chrono>
#include <stdlib.h>
#include <string.h>

namespace {
const uint32_t kGamePtr = 0xbc9aec, kIntro = 0x1b470, kSprite = 0x38, kLoaded = 0x109, kSpriteLoad = 0x406d50;
const char kVersusScreen[] = "gfx/ui/boss/versusscreen.anm2";
bool g_firstFloor = false;                          // the run's first floor began; its start room comes next
bool g_pending = false;                             // its start room was entered: warm at this frame's end

bool enabled() {
    static const bool on = !getenv("ISAAC360_WARM") || atoi(getenv("ISAAC360_WARM")) != 0;
    return on;
}

// Sprite::Load(path, true) on the boss intro's sprite, as F_7d6ed0 makes it, with the CPU state kept as it was
void load_versus_screen(X86* c) {
    uint32_t game = RD32(kGamePtr);
    if (!game) return;
    uint32_t sprite = game + kIntro + kSprite;
    if (RD8(sprite + kLoaded)) return;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // an MSVC std::string: over 15 characters, a pointer to them, the size at +0x10 and the capacity at +0x14
    const uint32_t n = (uint32_t)sizeof(kVersusScreen) - 1;
    uint32_t str = guest_malloc(24), chars = guest_malloc(n + 1);
    if (!str || !chars) {
        if (str) guest_free(str);
        if (chars) guest_free(chars);
        return;
    }
    memcpy(g_mem + chars, kVersusScreen, n + 1);
    WR32(str, chars);
    WR32(str + 0x10, n);
    WR32(str + 0x14, n);
    X86 saved;
    memcpy(&saved, c, sizeof(X86));
    c->r[1] = sprite;
    const uint32_t args[2] = {str, 1};
    guest_call(c, kSpriteLoad, 2, args, true);
    memcpy(c, &saved, sizeof(X86));
    guest_free(chars);
    guest_free(str);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    recomp_log("warm: the boss intro's sprite (%s) loaded with the run's first room, %.1f ms%s", kVersusScreen, ms,
               RD8(sprite + kLoaded) ? "" : " (it did not take)");
}
}  // namespace

// each line the game formats (shims_crt.cpp): the run's first floor, then its start room
void warm_log_line(const char* s) {
    if (!enabled()) return;
    if (!strncmp(s, "Level::Init m_Stage ", 20)) {
        g_firstFloor = !strncmp(s + 20, "1,", 2);
        return;
    }
    if (g_firstFloor && !strncmp(s, "Room 1.", 7) && strstr(s, "(Start Room)")) {
        g_firstFloor = false;
        g_pending = true;
    }
}

// the frame's end, before its picture is shown (shims_gl.cpp's gl_present, on the game's main thread)
void warm_frame_end() {
    if (!g_pending) return;
    g_pending = false;
    if (X86* c = current_cpu()) load_versus_screen(c);
}
