// USER32, GDI32, WGL, userenv, dwmapi and XInput for the recompiled game (stdcall). The game's window is a
// handle of ours: the platform presents its frames (the GL layer), input comes from the platform's pad state.
#include "runtime.h"
#include <map>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

void shim_register_dynamic(const char* module, const char* name, ShimFn fn);
uint32_t gl_get_proc(const std::string& name);       // shims_gl.cpp
void gl_present();                                   // shims_gl.cpp
bool gl_create_context();
void gl_make_current();

namespace {
const uint32_t kHwnd = 0x00010010, kHdc = 0x00020020, kHglrc = 0x00030030;
uint32_t g_wndProc = 0;
int g_clientW = 1280, g_clientH = 720;
std::map<uint32_t, uint32_t> g_windowLongs;

void u_ret0_0(X86* c) { RET_STD(c, 0, 0); }
#define STDRET(name, n, v) void name(X86* c) { RET_STD(c, n, v); }
STDRET(u_GetActiveWindow, 0, kHwnd)
STDRET(u_SetForegroundWindow, 1, 1)
STDRET(u_SetFocus, 1, kHwnd)
STDRET(u_BringWindowToTop, 1, 1)
STDRET(u_ShowWindow, 2, 1)
STDRET(u_DestroyWindow, 1, 1)
STDRET(u_UnregisterClassA, 2, 1)
STDRET(u_SetWindowPos, 7, 1)
STDRET(u_ShowCursor, 1, 0)
STDRET(u_SetCursor, 1, 0)
STDRET(u_LoadCursorA, 2, 0x5000)
STDRET(u_LoadIconA, 2, 0x5004)
STDRET(u_LoadImageA, 6, 0x5008)
STDRET(u_ClipCursor, 1, 1)
STDRET(u_SetCursorPos, 2, 1)
STDRET(u_SetCapture, 1, kHwnd)
STDRET(u_ReleaseCapture, 0, 1)
STDRET(u_TrackMouseEvent, 1, 1)
STDRET(u_GetMenu, 1, 0)
STDRET(u_SetClassLongA, 3, 0)
STDRET(u_WindowFromPoint, 2, kHwnd)
STDRET(u_ReleaseDC, 2, 1)
STDRET(u_GetDC, 1, kHdc)
STDRET(u_ChangeDisplaySettingsExA, 5, 0)
STDRET(u_RegisterDeviceNotificationA, 3, 0x6000)
STDRET(u_UnregisterDeviceNotification, 1, 1)
STDRET(u_OpenClipboard, 1, 0)
STDRET(u_CloseClipboard, 0, 1)
STDRET(u_EmptyClipboard, 0, 1)
STDRET(u_GetClipboardData, 1, 0)
STDRET(u_SetClipboardData, 2, 0)
STDRET(u_SendInput, 3, 0)
STDRET(u_GetMessageTime, 0, 0)
STDRET(u_TranslateMessage, 1, 1)
STDRET(u_DispatchMessageA, 1, 0)
STDRET(u_DefWindowProcA, 4, 0)
STDRET(u_SendMessageA, 4, 0)
STDRET(u_GetKeyState, 1, 0)
STDRET(u_MapVirtualKeyA, 2, 0)
STDRET(u_GetRawInputDeviceList, 3, 0)
STDRET(u_GetRawInputDeviceInfoA, 4, 0)

void u_RegisterClassA(X86* c) { g_wndProc = RD32(ARG(c, 0) + 4); RET_STD(c, 1, 0xc001); }
void u_RegisterClassExA(X86* c) { g_wndProc = RD32(ARG(c, 0) + 8); RET_STD(c, 1, 0xc001); }
void u_CreateWindowExA(X86* c) {
    recomp_log("CreateWindowExA '%s' %dx%d", guest_str(ARG(c, 2)).c_str(), (int)ARG(c, 6), (int)ARG(c, 7));
    // BOI360_WINDOW=WxH (the console: 960x540, host_xsdk.cpp) keeps the client area at that size whatever the game
    // asks for; the present pass brings the frame to the output's size
    static const char* fixed = getenv("BOI360_WINDOW");
    if (fixed && sscanf(fixed, "%dx%d", &g_clientW, &g_clientH) == 2) {
    } else if ((int)ARG(c, 6) > 0 && (int)ARG(c, 7) > 0) {
        g_clientW = (int)ARG(c, 6);
        g_clientH = (int)ARG(c, 7);
    }
    RET_STD(c, 12, kHwnd);
}
void u_AdjustWindowRectEx(X86* c) { RET_STD(c, 4, 1); }
void u_GetClientRect(X86* c) {
    uint32_t r = ARG(c, 1);
    WR32(r, 0); WR32(r + 4, 0); WR32(r + 8, (uint32_t)g_clientW); WR32(r + 12, (uint32_t)g_clientH);
    RET_STD(c, 2, 1);
}
void u_ClientToScreen(X86* c) { RET_STD(c, 2, 1); }
void u_GetCursorPos(X86* c) { WR32(ARG(c, 0), 0); WR32(ARG(c, 0) + 4, 0); RET_STD(c, 1, 1); }
void u_GetWindowLongA(X86* c) { RET_STD(c, 2, g_windowLongs[ARG(c, 1)]); }
void u_SetWindowLongA(X86* c) {
    uint32_t old = g_windowLongs[ARG(c, 1)];
    g_windowLongs[ARG(c, 1)] = ARG(c, 2);
    if ((int32_t)ARG(c, 1) == -4) g_wndProc = ARG(c, 2);    // GWL_WNDPROC
    RET_STD(c, 3, old);
}
void u_GetSystemMetrics(X86* c) {
    int i = (int)ARG(c, 0);
    RET_STD(c, 1, i == 0 ? 1280 : i == 1 ? 720 : 0);
}
void u_SystemParametersInfoA(X86* c) { RET_STD(c, 4, 1); }
void u_PeekMessageA(X86* c) { RET_STD(c, 5, 0); }
void u_GetMessageA(X86* c) { RET_STD(c, 4, 0); }
void u_GetAsyncKeyState(X86* c) { RET_STD(c, 1, 0); }
void u_MessageBoxA(X86* c) {
    recomp_log("MessageBoxA '%s': %s", guest_str(ARG(c, 2)).c_str(), guest_str(ARG(c, 1)).c_str());
    RET_STD(c, 4, 1);
}
// EnumDisplayDevicesA(device, index, DISPLAY_DEVICEA*, flags): one display
void u_EnumDisplayDevicesA(X86* c) {
    uint32_t dd = ARG(c, 2);
    if (ARG(c, 1) != 0) { RET_STD(c, 4, 0); return; }
    guest_write_str(dd + 4, "\\\\.\\DISPLAY1", 32);
    guest_write_str(dd + 36, "Xbox 360", 128);
    WR32(dd + 164, 5);                                     // ATTACHED_TO_DESKTOP | PRIMARY_DEVICE
    RET_STD(c, 4, 1);
}
// EnumDisplaySettings(Ex)A(device, mode, DEVMODEA*, ...): 1280x720 at 60 Hz
void fill_devmode(uint32_t dm) {
    WR32(dm + 104, 32);                                    // dmBitsPerPel
    WR32(dm + 108, 1280);                                  // dmPelsWidth
    WR32(dm + 112, 720);                                   // dmPelsHeight
    WR32(dm + 120, 60);                                    // dmDisplayFrequency
    WR32(dm + 40, 0x1c0000 | 0x400000);                    // dmFields: BITSPERPEL|PELSWIDTH|PELSHEIGHT|DISPLAYFREQUENCY
}
void u_EnumDisplaySettingsA(X86* c) {
    uint32_t mode = ARG(c, 1);
    if (mode != 0xffffffffu && mode != 0xfffffffeu && mode > 0) { RET_STD(c, 3, 0); return; }
    fill_devmode(ARG(c, 2));
    RET_STD(c, 3, 1);
}
void u_EnumDisplaySettingsExA(X86* c) {
    uint32_t mode = ARG(c, 1);
    if (mode != 0xffffffffu && mode != 0xfffffffeu && mode > 0) { RET_STD(c, 4, 0); return; }
    fill_devmode(ARG(c, 2));
    RET_STD(c, 4, 1);
}

// GDI32 / WGL
STDRET(g_CreateDCA, 4, kHdc)
STDRET(g_DeleteDC, 1, 1)
STDRET(g_SetPixelFormat, 3, 1)
void g_GetDeviceCaps(X86* c) {
    int i = (int)ARG(c, 1);
    RET_STD(c, 2, i == 8 ? 1280 : i == 10 ? 720 : i == 116 ? 60 : i == 12 ? 32 : 0);   // HORZRES, VERTRES, VREFRESH, BITSPIXEL
}
void g_DescribePixelFormat(X86* c) {
    uint32_t pfd = ARG(c, 3);
    if (pfd && ARG(c, 2) >= 40) {
        memset(G(pfd), 0, 40);
        WR16(pfd, 40); WR16(pfd + 2, 1);
        WR32(pfd + 4, 0x25);                               // DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER
        WR8(pfd + 9, 32); WR8(pfd + 23, 24); WR8(pfd + 24, 8);   // colour, depth, stencil bits
    }
    RET_STD(c, 4, 1);
}
void g_SwapBuffers(X86* c) { gl_present(); RET_STD(c, 1, 1); }
void wgl_CreateContext(X86* c) { RET_STD(c, 1, gl_create_context() ? kHglrc : 0); }
STDRET(wgl_DeleteContext, 1, 1)
void wgl_MakeCurrent(X86* c) { if (ARG(c, 1)) gl_make_current(); RET_STD(c, 2, 1); }
STDRET(wgl_ShareLists, 2, 1)
void wgl_GetProcAddress(X86* c) {
    std::string name = guest_str(ARG(c, 0));
    RET_STD(c, 1, gl_get_proc(name));
}

// userenv / dwmapi (loaded by name)
void ue_GetUserProfileDirectoryA(X86* c) {
    std::string d = recomp_config().saveDir;
    uint32_t sizeAt = ARG(c, 2);
    uint32_t cap = sizeAt ? RD32(sizeAt) : 0;
    if (sizeAt) WR32(sizeAt, (uint32_t)d.size() + 1);
    if (ARG(c, 1) && cap > d.size()) guest_write_str(ARG(c, 1), d, cap);
    RET_STD(c, 3, cap > d.size() ? 1 : 0);
}
void dwm_EnableComposition(X86* c) { RET_STD(c, 1, 0); }
void dwm_IsCompositionEnabled(X86* c) { WR32(ARG(c, 0), 0); RET_STD(c, 1, 0); }

const ShimDef kUser[] = {
    {"user32.dll!GetActiveWindow", u_GetActiveWindow}, {"user32.dll!SetForegroundWindow", u_SetForegroundWindow},
    {"user32.dll!SetFocus", u_SetFocus}, {"user32.dll!BringWindowToTop", u_BringWindowToTop},
    {"user32.dll!ShowWindow", u_ShowWindow}, {"user32.dll!DestroyWindow", u_DestroyWindow},
    {"user32.dll!UnregisterClassA", u_UnregisterClassA}, {"user32.dll!SetWindowPos", u_SetWindowPos},
    {"user32.dll!ShowCursor", u_ShowCursor}, {"user32.dll!SetCursor", u_SetCursor},
    {"user32.dll!LoadCursorA", u_LoadCursorA}, {"user32.dll!LoadIconA", u_LoadIconA},
    {"user32.dll!LoadImageA", u_LoadImageA}, {"user32.dll!ClipCursor", u_ClipCursor},
    {"user32.dll!SetCursorPos", u_SetCursorPos}, {"user32.dll!SetCapture", u_SetCapture},
    {"user32.dll!ReleaseCapture", u_ReleaseCapture}, {"user32.dll!TrackMouseEvent", u_TrackMouseEvent},
    {"user32.dll!GetMenu", u_GetMenu}, {"user32.dll!SetClassLongA", u_SetClassLongA},
    {"user32.dll!WindowFromPoint", u_WindowFromPoint}, {"user32.dll!ReleaseDC", u_ReleaseDC},
    {"user32.dll!GetDC", u_GetDC}, {"user32.dll!ChangeDisplaySettingsExA", u_ChangeDisplaySettingsExA},
    {"user32.dll!RegisterDeviceNotificationA", u_RegisterDeviceNotificationA},
    {"user32.dll!UnregisterDeviceNotification", u_UnregisterDeviceNotification},
    {"user32.dll!OpenClipboard", u_OpenClipboard}, {"user32.dll!CloseClipboard", u_CloseClipboard},
    {"user32.dll!EmptyClipboard", u_EmptyClipboard}, {"user32.dll!GetClipboardData", u_GetClipboardData},
    {"user32.dll!SetClipboardData", u_SetClipboardData}, {"user32.dll!SendInput", u_SendInput},
    {"user32.dll!GetMessageTime", u_GetMessageTime}, {"user32.dll!TranslateMessage", u_TranslateMessage},
    {"user32.dll!DispatchMessageA", u_DispatchMessageA}, {"user32.dll!DefWindowProcA", u_DefWindowProcA},
    {"user32.dll!SendMessageA", u_SendMessageA}, {"user32.dll!GetKeyState", u_GetKeyState},
    {"user32.dll!MapVirtualKeyA", u_MapVirtualKeyA}, {"user32.dll!GetRawInputDeviceList", u_GetRawInputDeviceList},
    {"user32.dll!GetRawInputDeviceInfoA", u_GetRawInputDeviceInfoA},
    {"user32.dll!RegisterClassA", u_RegisterClassA}, {"user32.dll!RegisterClassExA", u_RegisterClassExA},
    {"user32.dll!CreateWindowExA", u_CreateWindowExA}, {"user32.dll!AdjustWindowRectEx", u_AdjustWindowRectEx},
    {"user32.dll!GetClientRect", u_GetClientRect}, {"user32.dll!ClientToScreen", u_ClientToScreen},
    {"user32.dll!GetCursorPos", u_GetCursorPos}, {"user32.dll!GetWindowLongA", u_GetWindowLongA},
    {"user32.dll!SetWindowLongA", u_SetWindowLongA}, {"user32.dll!GetSystemMetrics", u_GetSystemMetrics},
    {"user32.dll!SystemParametersInfoA", u_SystemParametersInfoA}, {"user32.dll!PeekMessageA", u_PeekMessageA},
    {"user32.dll!GetMessageA", u_GetMessageA}, {"user32.dll!GetAsyncKeyState", u_GetAsyncKeyState},
    {"user32.dll!MessageBoxA", u_MessageBoxA}, {"user32.dll!EnumDisplayDevicesA", u_EnumDisplayDevicesA},
    {"user32.dll!EnumDisplaySettingsA", u_EnumDisplaySettingsA},
    {"user32.dll!EnumDisplaySettingsExA", u_EnumDisplaySettingsExA},
    {"gdi32.dll!CreateDCA", g_CreateDCA}, {"gdi32.dll!DeleteDC", g_DeleteDC},
    {"gdi32.dll!SetPixelFormat", g_SetPixelFormat}, {"gdi32.dll!GetDeviceCaps", g_GetDeviceCaps},
    {"gdi32.dll!DescribePixelFormat", g_DescribePixelFormat}, {"gdi32.dll!SwapBuffers", g_SwapBuffers},
    {"opengl32.dll!wglCreateContext", wgl_CreateContext}, {"opengl32.dll!wglDeleteContext", wgl_DeleteContext},
    {"opengl32.dll!wglMakeCurrent", wgl_MakeCurrent}, {"opengl32.dll!wglShareLists", wgl_ShareLists},
    {"opengl32.dll!wglGetProcAddress", wgl_GetProcAddress},
};
}

void shims_user32_register() {
    shim_register(kUser, (int)(sizeof(kUser) / sizeof(kUser[0])));
    shim_register_dynamic("userenv", "GetUserProfileDirectoryA", ue_GetUserProfileDirectoryA);
    shim_register_dynamic("userenv.dll", "GetUserProfileDirectoryA", ue_GetUserProfileDirectoryA);
    shim_register_dynamic("dwmapi.dll", "DwmEnableComposition", dwm_EnableComposition);
    shim_register_dynamic("dwmapi.dll", "DwmIsCompositionEnabled", dwm_IsCompositionEnabled);
}

// the game's window client size (gl_vk.cpp sizes the default framebuffer by it)
void gl_window_size(int& w, int& h) {
    w = g_clientW;
    h = g_clientH;
}
