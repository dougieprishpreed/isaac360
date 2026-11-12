// Pads for the recompiled game: XInput 1.4/1.3 (loaded by name) reports the platform's pad, DirectInput 8 is a
// COM object of ours that enumerates no devices (the game requires it to load). Test runs script the pad by
// presented frame: ISAAC360_PAD="first:last:keys;..." with the keys of the Windows binary's scripts (docs).
#include "runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

void shim_register_dynamic(const char* module, const char* name, ShimFn fn);
uint32_t gl_frame_count();                          // shims_gl.cpp

struct HostPad {
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
    bool connected;
};
HostPad g_hostPad = {0, 0, 0, 0, 0, 0, 0, false};   // the platform writes this (interactive runs)
// or reads its pads here when asked (the console: XamInputGetState); index 0..3
bool (*g_hostPadSource)(int index, HostPad* out) = 0;

namespace {
struct Seg { uint32_t first, last; HostPad pad; };
std::vector<Seg> g_script;
bool g_scriptParsed = false;

void key(char c, HostPad& p) {
    switch (c >= 'a' && c <= 'z' ? c - 32 : c) {
    case 'W': p.ly = 32767; break;
    case 'S': p.ly = -32767; break;
    case 'A': p.lx = -32767; break;
    case 'D': p.lx = 32767; break;
    case 'I': p.ry = 32767; break;
    case 'K': p.ry = -32767; break;
    case 'J': p.rx = -32767; break;
    case 'L': p.rx = 32767; break;
    case 'X': p.buttons |= 0x4000; break;
    case 'Z': case 'E': p.buttons |= 0x1000; break;
    case 'C': p.buttons |= 0x0100; break;
    case 'R': p.buttons |= 0x0200; break;
    case 'V': p.buttons |= 0x2000; break;
    case 'Q': p.buttons |= 0x0020; break;
    case 'N': p.buttons |= 0x0010; break;
    case 'Y': p.buttons |= 0x8000; break;
    case 'U': p.buttons |= 0x0001; break;
    case 'H': p.buttons |= 0x0002; break;
    case 'F': p.buttons |= 0x0004; break;
    case 'G': p.buttons |= 0x0008; break;
    }
}

// The pads' sticks through a round dead zone: the game reads them raw through GLFW's XInput joysticks, so a stick
// that drifts at rest walks Isaac slowly. ISAAC360_DEADZONE (percent of full tilt, default 24: XInput's own
// XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 7849) clears a stick inside it and scales the rest so full tilt stays full;
// 0 passes them raw, 100 turns the sticks off (the d-pad still moves).
void apply_deadzone(HostPad& p) {
    static const int percent = getenv("ISAAC360_DEADZONE") ? atoi(getenv("ISAAC360_DEADZONE")) : 24;
    if (percent <= 0) return;
    const double dz = percent >= 100 ? 32768.0 : 32767.0 * percent / 100.0;
    int16_t* sticks[2][2] = {{&p.lx, &p.ly}, {&p.rx, &p.ry}};
    for (int s = 0; s < 2; ++s) {
        double x = *sticks[s][0], y = *sticks[s][1];
        double mag = sqrt(x * x + y * y);
        if (mag <= dz) {
            *sticks[s][0] = *sticks[s][1] = 0;
            continue;
        }
        double out = (mag > 32767.0 ? 32767.0 : mag) - dz;
        double scale = out / (32767.0 - dz) * 32767.0 / mag;
        double nx = x * scale, ny = y * scale;
        *sticks[s][0] = (int16_t)(nx > 32767.0 ? 32767 : nx < -32768.0 ? -32768 : nx);
        *sticks[s][1] = (int16_t)(ny > 32767.0 ? 32767 : ny < -32768.0 ? -32768 : ny);
    }
}

HostPad current_pad() {
    if (!g_scriptParsed) {
        g_scriptParsed = true;
        const char* s = getenv("ISAAC360_PAD");
        for (const char* p = s; p && *p;) {
            Seg g;
            memset(&g.pad, 0, sizeof(g.pad));
            g.first = (uint32_t)atoi(p);
            while (*p && *p != ':') ++p;
            if (*p) ++p;
            g.last = (uint32_t)atoi(p);
            while (*p && *p != ':') ++p;
            if (*p) ++p;
            while (*p && *p != ';') key(*p++, g.pad);
            if (*p) ++p;
            g.pad.connected = true;
            g_script.push_back(g);
        }
    }
    HostPad p = g_hostPad;
    if (g_hostPadSource) {
        HostPad q;
        memset(&q, 0, sizeof(q));
        p = g_hostPadSource(0, &q) ? q : p;
    }
    apply_deadzone(p);
    if (!g_script.empty()) {
        p.connected = true;
        uint32_t f = gl_frame_count();
        for (size_t i = 0; i < g_script.size(); ++i)
            if (f >= g_script[i].first && f < g_script[i].last) {
                p.buttons |= g_script[i].pad.buttons;
                if (g_script[i].pad.lx) p.lx = g_script[i].pad.lx;
                if (g_script[i].pad.ly) p.ly = g_script[i].pad.ly;
                if (g_script[i].pad.rx) p.rx = g_script[i].pad.rx;
                if (g_script[i].pad.ry) p.ry = g_script[i].pad.ry;
            }
    }
    return p;
}

uint32_t g_packet = 0;
// XInputGetState(index, XINPUT_STATE*)
void xi_GetState(X86* c) {
    uint32_t i = ARG(c, 0), st = ARG(c, 1);
    HostPad p = current_pad();
    if (i != 0 && g_hostPadSource) {                    // co-op: the console's other pads
        memset(&p, 0, sizeof(p));
        if (i > 3 || !g_hostPadSource((int)i, &p)) p.connected = false;
        else apply_deadzone(p);
    } else if (i != 0) {
        p.connected = false;
    }
    if (!p.connected) { RET_STD(c, 2, 1167); return; }   // ERROR_DEVICE_NOT_CONNECTED
    WR32(st, ++g_packet);
    WR16(st + 4, p.buttons);
    WR8(st + 6, p.lt);
    WR8(st + 7, p.rt);
    WR16(st + 8, (uint16_t)p.lx);
    WR16(st + 10, (uint16_t)p.ly);
    WR16(st + 12, (uint16_t)p.rx);
    WR16(st + 14, (uint16_t)p.ry);
    RET_STD(c, 2, 0);
}
void xi_SetState(X86* c) { RET_STD(c, 2, current_pad().connected && ARG(c, 0) == 0 ? 0 : 1167); }
// XInputGetCapabilities(index, flags, XINPUT_CAPABILITIES*)
void xi_GetCapabilities(X86* c) {
    uint32_t i = ARG(c, 0), caps = ARG(c, 2);
    HostPad other;
    memset(&other, 0, sizeof(other));
    bool connected = i == 0 ? current_pad().connected : i <= 3 && g_hostPadSource && g_hostPadSource((int)i, &other) && other.connected;
    if (!connected) { RET_STD(c, 3, 1167); return; }
    memset(g_mem + caps, 0, 20);
    WR8(caps, 1);                                   // XINPUT_DEVTYPE_GAMEPAD
    WR8(caps + 1, 1);                               // XINPUT_DEVSUBTYPE_GAMEPAD
    WR16(caps + 4, 0xf3ff);
    RET_STD(c, 3, 0);
}
void xi_Enable(X86* c) { RET_STD(c, 1, 0); }
// winmm joystick API (GLFW requires it to load): no joystick is plugged in, pads come through XInput
void joy_GetDevCapsW(X86* c) { RET_STD(c, 3, 167); }   // JOYERR_UNPLUGGED
void joy_GetPos(X86* c) { RET_STD(c, 2, 167); }
void joy_GetPosEx(X86* c) { RET_STD(c, 2, 167); }
void u_SetProcessDPIAware(X86* c) { RET_STD(c, 0, 1); }

// DirectInput 8: an object with the IDirectInput8A vtable; no devices
uint32_t g_dinput = 0;
void di_QueryInterface(X86* c) { WR32(ARG(c, 2), 0); RET_STD(c, 3, 0x80004002); }
void di_AddRef(X86* c) { RET_STD(c, 1, 1); }
void di_Release(X86* c) { RET_STD(c, 1, 0); }
void di_CreateDevice(X86* c) { RET_STD(c, 4, 0x80004005); }
void di_EnumDevices(X86* c) { RET_STD(c, 5, 0); }
void di_GetDeviceStatus(X86* c) { RET_STD(c, 2, 1); }
void di_RunControlPanel(X86* c) { RET_STD(c, 3, 0); }
void di_Initialize(X86* c) { RET_STD(c, 3, 0); }
void di_FindDevice(X86* c) { RET_STD(c, 4, 0x80004005); }
void di_EnumDevicesBySemantics(X86* c) { RET_STD(c, 6, 0); }
void di_ConfigureDevices(X86* c) { RET_STD(c, 5, 0); }
// DirectInput8Create(hinst, version, riid, out**, outer)
void di_Create(X86* c) {
    if (!g_dinput) {
        ShimFn v[11] = {di_QueryInterface, di_AddRef, di_Release, di_CreateDevice, di_EnumDevices, di_GetDeviceStatus,
                        di_RunControlPanel, di_Initialize, di_FindDevice, di_EnumDevicesBySemantics, di_ConfigureDevices};
        uint32_t vt = guest_calloc(11, 4);
        for (int i = 0; i < 11; ++i) WR32(vt + 4 * i, thunk_for(v[i], "IDirectInput8A"));
        g_dinput = guest_calloc(1, 16);
        WR32(g_dinput, vt);
    }
    WR32(ARG(c, 3), g_dinput);
    RET_STD(c, 5, 0);
}
}

void shims_input_register() {
    const char* xinput[] = {"xinput1_4.dll", "xinput1_3.dll", "bin\\xinput1_3.dll"};
    for (int i = 0; i < 3; ++i) {
        shim_register_dynamic(xinput[i], "XInputGetState", xi_GetState);
        shim_register_dynamic(xinput[i], "XInputSetState", xi_SetState);
        shim_register_dynamic(xinput[i], "XInputGetCapabilities", xi_GetCapabilities);
        shim_register_dynamic(xinput[i], "XInputEnable", xi_Enable);
    }
    shim_register_dynamic("dinput8.dll", "DirectInput8Create", di_Create);
    shim_register_dynamic("winmm.dll", "joyGetDevCapsW", joy_GetDevCapsW);
    shim_register_dynamic("winmm.dll", "joyGetPos", joy_GetPos);
    shim_register_dynamic("winmm.dll", "joyGetPosEx", joy_GetPosEx);
    shim_register_dynamic("user32.dll", "SetProcessDPIAware", u_SetProcessDPIAware);
}
