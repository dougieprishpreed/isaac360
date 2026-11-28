// The Windows imports Repentance+ (v1.9.7.17) added to the game's program:
// - kernel32's virtual memory, system and FLS calls for the mimalloc it links (VirtualAlloc over runtime.cpp's
//   reservations), and a few more file and process calls;
// - the wide (W) USER32 and GDI32 calls of the newer GLFW it builds on, as shims_user32.cpp answers the A ones: one
//   window, one monitor, no messages;
// - Epic Online Services and libcurl (online play, crash uploads): not on the console. EOS_Initialize fails, so the
//   game keeps to its offline path, as with Steam (shims_steam.cpp);
// - the new CRT, OpenAL, Steam, advapi32, bcrypt, dbghelp and shell32 calls.
#include "runtime.h"
#include "vfile.h"
#include <math.h>
#include <vector>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <wctype.h>

void shim_register_dynamic(const char* module, const char* name, ShimFn fn);
void gl_window_size(int& w, int& h);                 // shims_user32.cpp
void shims_user32_set_client(int w, int h, uint32_t wndProc);

namespace {
const uint32_t kHwnd = 0x00010010, kHdc = 0x00020020, kHglrc = 0x00030030, kHmonitor = 0x00070070;

#define STDRET(name, n, v) void name(X86* c) { RET_STD(c, n, v); }
#define CDECLRET(name, v) void name(X86* c) { RET_CDECL(c, v); }

// a string of ours in guest memory, made once
uint32_t guest_static_str(const char* s) {
    static std::mutex lock;
    static std::map<std::string, uint32_t> made;
    std::lock_guard<std::mutex> l(lock);
    uint32_t& a = made[s];
    if (!a) {
        size_t n = strlen(s) + 2;                        // two NULs: a list's end too
        a = guest_calloc(1, (uint32_t)n);
        if (a) memcpy(g_mem + a, s, n - 2);
    }
    return a;
}

void write_wstr(uint32_t a, const char* s, uint32_t cap) {
    uint32_t i = 0;
    for (; s[i] && i + 1 < cap; ++i) WR16(a + 2 * i, (uint8_t)s[i]);
    WR16(a + 2 * i, 0);
}

// ------------------------------------------------------------------------------------------------ virtual memory
const uint32_t MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_DECOMMIT = 0x4000, MEM_RELEASE = 0x8000,
               MEM_RESET = 0x80000, MEM_RESET_UNDO = 0x1000000, MEM_FREE = 0x10000, MEM_PRIVATE = 0x20000;

// VirtualAlloc(address, size, type, protect)
void k_VirtualAlloc(X86* c) {
    uint32_t a = ARG(c, 0), size = ARG(c, 1), type = ARG(c, 2), r = 0;
    if (type & (MEM_RESET | MEM_RESET_UNDO)) {
        r = a;                                           // contents may go: they stay
    } else if (type & MEM_RESERVE) {
        r = guest_vm_reserve(a, size);
        if (r && (type & MEM_COMMIT) && !guest_vm_commit(r, size)) {
            guest_vm_release(r);
            r = 0;
        }
    } else if (type & MEM_COMMIT) {
        uint32_t start = a & ~0xfffu;
        r = guest_vm_commit(start, (a + size + 0xfff) / 0x1000 * 0x1000 - start) ? start : 0;
    }
    if (!r) {
        static int logged = 0;
        if (logged++ < 20)
            recomp_log("VirtualAlloc(%08x, %u KB, %x) failed (guest heap top %u MB, %u MB committed by VirtualAlloc)", a,
                       size >> 10, type, guest_heap_top() >> 20, guest_vm_committed_bytes() >> 20);
    }
    RET_STD(c, 4, r);
}
// VirtualFree(address, size, type)
void k_VirtualFree(X86* c) {
    uint32_t a = ARG(c, 0), size = ARG(c, 1), type = ARG(c, 2);
    bool ok = (type & MEM_RELEASE) ? guest_vm_release(a) : (type & MEM_DECOMMIT) ? guest_vm_decommit(a, size) : false;
    RET_STD(c, 3, ok ? 1 : 0);
}
// VirtualQuery(address, MEMORY_BASIC_INFORMATION*, length)
void k_VirtualQuery(X86* c) {
    uint32_t a = ARG(c, 0), mbi = ARG(c, 1);
    uint32_t allocBase, base, size;
    int state = guest_vm_query(a, &allocBase, &base, &size);
    memset(g_mem + mbi, 0, 28);
    WR32(mbi, base);
    WR32(mbi + 4, allocBase);
    WR32(mbi + 8, state ? 4 : 0);                        // PAGE_READWRITE
    WR32(mbi + 12, size);
    WR32(mbi + 16, state == 2 ? MEM_COMMIT : state == 1 ? MEM_RESERVE : MEM_FREE);
    WR32(mbi + 20, state == 2 ? 4 : 0);
    WR32(mbi + 24, state ? MEM_PRIVATE : 0);
    RET_STD(c, 3, 28);
}
STDRET(k_VirtualUnlock, 2, 1)
// GetSystemInfo(SYSTEM_INFO*): x86, 4 KB pages, 64 KB granularity, the guest's address range
void k_GetSystemInfo(X86* c) {
    uint32_t si = ARG(c, 0);
    memset(g_mem + si, 0, 36);
    WR32(si + 4, 4096);                                  // dwPageSize
    WR32(si + 8, 0x10000);                               // lpMinimumApplicationAddress
    WR32(si + 12, g_mem_size - 0x10001);                 // lpMaximumApplicationAddress
    WR32(si + 16, 0x3f);                                 // dwActiveProcessorMask
    WR32(si + 20, 6);                                    // dwNumberOfProcessors (the console's hardware threads)
    WR32(si + 24, 586);                                  // dwProcessorType
    WR32(si + 28, 0x10000);                              // dwAllocationGranularity
    WR16(si + 32, 6);                                    // wProcessorLevel
    RET_STD(c, 1, 0);
}
STDRET(k_GetLargePageMinimum, 0, 0)
// GetNumaHighestNodeNumber(ULONG*): one node
void k_GetNumaHighestNodeNumber(X86* c) { WR32(ARG(c, 0), 0); RET_STD(c, 1, 1); }
void k_GetNumaProcessorNode(X86* c) { WR8(ARG(c, 1), 0); RET_STD(c, 2, 1); }
void k_GetNumaNodeProcessorMask(X86* c) { WR32(ARG(c, 1), 0x3f); WR32(ARG(c, 1) + 4, 0); RET_STD(c, 2, 1); }
// FLS: mimalloc registers its thread-exit callback here; slots behave as TLS slots, the callbacks never run (the
// game's threads live as long as it does)
std::mutex g_flsLock;
uint32_t g_flsNext = 200;
void k_FlsAlloc(X86* c) {
    std::lock_guard<std::mutex> l(g_flsLock);
    RET_STD(c, 1, g_flsNext++);
}
STDRET(k_FlsFree, 1, 1)
STDRET(k_FlsSetValue, 2, 1)
// K32GetProcessMemoryInfo(process, PROCESS_MEMORY_COUNTERS*, cb): the guest heap and reservations as its use
void k_GetProcessMemoryInfo(X86* c) {
    uint32_t pmc = ARG(c, 1), cb = ARG(c, 2);
    memset(g_mem + pmc, 0, cb);
    WR32(pmc, cb);
    uint32_t used = guest_heap_used() + guest_vm_committed_bytes();
    if (cb >= 40) {
        WR32(pmc + 12, used);                            // WorkingSetSize
        WR32(pmc + 8, used);                             // PeakWorkingSetSize
        WR32(pmc + 32, used);                            // PagefileUsage
        WR32(pmc + 36, used);
    }
    RET_STD(c, 3, 1);
}
// GetProcessTimes(process, creation, exit, kernel, user): all zero
void k_GetProcessTimes(X86* c) {
    for (int i = 1; i <= 4; ++i) {
        WR32(ARG(c, i), 0);
        WR32(ARG(c, i) + 4, 0);
    }
    RET_STD(c, 5, 1);
}
// GetEnvironmentVariableA(name, buffer, size): none set (mimalloc's MIMALLOC_ options)
void k_GetEnvironmentVariableA(X86* c) { RET_STD(c, 3, 0); }
STDRET(k_GetStdHandle, 1, 0)
void k_WriteConsoleA(X86* c) {
    if (ARG(c, 3)) WR32(ARG(c, 3), ARG(c, 2));
    RET_STD(c, 5, 1);
}
void k_OutputDebugStringA(X86* c) {
    static int lines = 0;
    if (lines++ < 200) recomp_log("debug> %s", guest_str(ARG(c, 0), 400).c_str());
    RET_STD(c, 1, 0);
}
// VerSetConditionMask(mask(8), type, condition) -> ULONGLONG: the mask as given (no version checks pass or fail here)
void k_VerSetConditionMask(X86* c) {
    c->r[0] = ARG(c, 0);
    c->r[2] = ARG(c, 1);
    c->r[4] += 4 + 16;
}
void k_FormatMessageW(X86* c) { RET_STD(c, 7, 0); }
// GetModuleFileNameW(module, buffer, size)
void k_GetModuleFileNameW(X86* c) {
    const char* path = "game\\isaac-ng.exe";
    uint32_t cap = ARG(c, 2);
    if (cap) write_wstr(ARG(c, 1), path, cap);
    RET_STD(c, 3, (uint32_t)strlen(path));
}
void k_GetProcessImageFileNameA(X86* c) {
    guest_write_str(ARG(c, 1), "isaac-ng.exe", ARG(c, 2));
    RET_STD(c, 3, 12);
}
STDRET(k_OpenProcess, 3, 0)
void k_GetTempPathA(X86* c) {
    std::string d = recomp_config().saveDir + "\\";
    if (ARG(c, 0) > d.size()) guest_write_str(ARG(c, 1), d, ARG(c, 0));
    RET_STD(c, 2, (uint32_t)d.size());
}
void k_GetStartupInfoW(X86* c) {
    memset(g_mem + ARG(c, 0), 0, 68);
    WR32(ARG(c, 0), 68);
    RET_STD(c, 1, 0);
}
void k_DeleteFileA(X86* c) {
    vf_drain();
    RET_STD(c, 1, remove(recomp_host_path(guest_str(ARG(c, 0)), true).c_str()) == 0 ? 1 : 0);
}
STDRET(k_RemoveDirectoryA, 1, 0)
void k_MoveFileExA(X86* c) {
    vf_drain();
    std::string from = recomp_host_path(guest_str(ARG(c, 0)), true), to = recomp_host_path(guest_str(ARG(c, 1)), true);
    if (ARG(c, 2) & 1) remove(to.c_str());               // MOVEFILE_REPLACE_EXISTING
    RET_STD(c, 3, rename(from.c_str(), to.c_str()) == 0 ? 1 : 0);
}
STDRET(k_GetFileTime, 4, 0)
void k_GetModuleInformation(X86* c) {
    uint32_t mi = ARG(c, 2);
    WR32(mi, kGuestImageBase);
    WR32(mi + 4, 0x92c000);
    WR32(mi + 8, 0);
    RET_STD(c, 4, 1);
}
STDRET(k_GlobalFree, 1, 0)

// ------------------------------------------------------------------------------------------------ USER32 (W)
std::map<std::string, uint32_t> g_props;
std::map<uint32_t, uint32_t> g_longs;
uint32_t g_wndProc = 0;

void u_RegisterClassExW(X86* c) { g_wndProc = RD32(ARG(c, 0) + 8); RET_STD(c, 1, 0xc001); }
void u_CreateWindowExW(X86* c) {
    std::u16string t = ARG(c, 2) ? guest_wstr(ARG(c, 2)) : std::u16string();
    recomp_log("CreateWindowExW '%s' %dx%d", std::string(t.begin(), t.end()).c_str(), (int)ARG(c, 6), (int)ARG(c, 7));
    shims_user32_set_client((int)ARG(c, 6), (int)ARG(c, 7), g_wndProc);
    RET_STD(c, 12, kHwnd);
}
STDRET(u_UnregisterClassW, 2, 1)
STDRET(u_DefWindowProcW, 4, 0)
STDRET(u_DispatchMessageW, 1, 0)
STDRET(u_PeekMessageW, 5, 0)
STDRET(u_WaitMessage, 0, 1)
STDRET(u_MsgWaitForMultipleObjects, 5, 0x102)        // WAIT_TIMEOUT
STDRET(u_PostMessageW, 4, 1)
STDRET(u_SendMessageW, 4, 0)
void u_SendMessageTimeoutA(X86* c) {
    if (ARG(c, 6)) WR32(ARG(c, 6), 0);
    RET_STD(c, 7, 1);
}
void u_GetWindowLongW(X86* c) { RET_STD(c, 2, g_longs[ARG(c, 1)]); }
void u_SetWindowLongW(X86* c) {
    uint32_t old = g_longs[ARG(c, 1)];
    g_longs[ARG(c, 1)] = ARG(c, 2);
    RET_STD(c, 3, old);
}
STDRET(u_GetClassLongW, 2, 0)
std::string prop_key(uint32_t a) {
    if (a < 0x10000) return "#" + std::to_string(a);     // an atom
    std::u16string w = guest_wstr(a);
    return std::string(w.begin(), w.end());
}
void u_SetPropW(X86* c) { g_props[prop_key(ARG(c, 1))] = ARG(c, 2); RET_STD(c, 3, 1); }
void u_GetPropW(X86* c) { RET_STD(c, 2, g_props[prop_key(ARG(c, 1))]); }
void u_RemovePropW(X86* c) {
    std::string k = prop_key(ARG(c, 1));
    uint32_t v = g_props[k];
    g_props.erase(k);
    RET_STD(c, 2, v);
}
STDRET(u_SetWindowTextW, 2, 1)
STDRET(u_LoadCursorW, 2, 0x5000)
STDRET(u_LoadImageW, 6, 0x5008)
STDRET(u_CreateIconIndirect, 1, 0x5010)
STDRET(u_DestroyIcon, 1, 1)
STDRET(u_IsWindow, 1, 1)
STDRET(u_IsWindowVisible, 1, 1)
STDRET(u_IsIconic, 1, 0)
STDRET(u_IsZoomed, 1, 0)
STDRET(u_FlashWindow, 2, 0)
STDRET(u_MoveWindow, 6, 1)
STDRET(u_ScreenToClient, 2, 1)
STDRET(u_ChangeWindowMessageFilterEx, 4, 1)
STDRET(u_RegisterRawInputDevices, 3, 1)
STDRET(u_GetRawInputData, 5, 0)
STDRET(u_RegisterDeviceNotificationW, 3, 0x6000)
STDRET(u_ToUnicode, 6, 0)
STDRET(u_MapVirtualKeyW, 2, 0)
STDRET(u_SetLayeredWindowAttributes, 4, 1)
STDRET(u_GetLayeredWindowAttributes, 4, 0)
STDRET(u_SetWindowPlacement, 2, 1)
STDRET(u_EnumWindows, 2, 1)
STDRET(u_MonitorFromWindow, 2, kHmonitor)
void u_GetAncestor(X86* c) { RET_STD(c, 2, ARG(c, 0)); }
void u_GetLastActivePopup(X86* c) { RET_STD(c, 1, ARG(c, 0)); }
void u_GetWindowThreadProcessId(X86* c) {
    if (ARG(c, 1)) WR32(ARG(c, 1), 1);
    RET_STD(c, 2, 1);
}
void u_GetTitleBarInfo(X86* c) { RET_STD(c, 2, 0); }
void u_ChangeDisplaySettingsExW(X86* c) { RET_STD(c, 5, 0); }   // DISP_CHANGE_SUCCESSFUL
void write_rect(uint32_t r, int l, int t, int rr, int b) {
    WR32(r, (uint32_t)l);
    WR32(r + 4, (uint32_t)t);
    WR32(r + 8, (uint32_t)rr);
    WR32(r + 12, (uint32_t)b);
}
void u_GetWindowRect(X86* c) {
    int w, h;
    gl_window_size(w, h);
    write_rect(ARG(c, 1), 0, 0, w, h);
    RET_STD(c, 2, 1);
}
void u_GetWindowPlacement(X86* c) {
    int w, h;
    gl_window_size(w, h);
    uint32_t p = ARG(c, 1);
    memset(g_mem + p, 0, 44);
    WR32(p, 44);
    WR32(p + 8, 1);                                      // SW_SHOWNORMAL
    write_rect(p + 28, 0, 0, w, h);
    RET_STD(c, 2, 1);
}
void u_SetRect(X86* c) { write_rect(ARG(c, 0), (int)ARG(c, 1), (int)ARG(c, 2), (int)ARG(c, 3), (int)ARG(c, 4)); RET_STD(c, 5, 1); }
void u_OffsetRect(X86* c) {
    uint32_t r = ARG(c, 0);
    int dx = (int)ARG(c, 1), dy = (int)ARG(c, 2);
    write_rect(r, (int)RD32(r) + dx, (int)RD32(r + 4) + dy, (int)RD32(r + 8) + dx, (int)RD32(r + 12) + dy);
    RET_STD(c, 3, 1);
}
void u_PtInRect(X86* c) {
    uint32_t r = ARG(c, 0);
    int x = (int)ARG(c, 1), y = (int)ARG(c, 2);
    RET_STD(c, 3, x >= (int)RD32(r) && x < (int)RD32(r + 8) && y >= (int)RD32(r + 4) && y < (int)RD32(r + 12));
}
// SystemParametersInfoW(action, param, pv, winini): nothing to report beyond zeroes the caller set
STDRET(u_SystemParametersInfoW, 4, 1)

// one display adapter with one monitor, 1280x720 at 60 Hz (as shims_user32.cpp's A calls)
const char* const kAdapter = "\\\\.\\DISPLAY1";
// EnumDisplayDevicesW(device, index, DISPLAY_DEVICEW*, flags)
void u_EnumDisplayDevicesW(X86* c) {
    uint32_t dd = ARG(c, 2);
    if (ARG(c, 1) != 0) {
        RET_STD(c, 4, 0);
        return;
    }
    bool monitor = ARG(c, 0) != 0;                       // a monitor of the adapter named
    write_wstr(dd + 4, monitor ? "\\\\.\\DISPLAY1\\Monitor0" : kAdapter, 32);
    write_wstr(dd + 68, monitor ? "Television" : "Xbox 360", 128);
    WR32(dd + 324, monitor ? 3 : 5);                     // ACTIVE|ATTACHED, or ATTACHED_TO_DESKTOP|PRIMARY_DEVICE
    write_wstr(dd + 328, "", 128);
    write_wstr(dd + 584, "", 128);
    RET_STD(c, 4, 1);
}
void fill_devmodew(uint32_t dm) {
    write_wstr(dm, kAdapter, 32);
    WR16(dm + 68, 220);                                  // dmSize
    WR32(dm + 72, 0x1c0000 | 0x400000 | 0x20);           // BITSPERPEL|PELSWIDTH|PELSHEIGHT|DISPLAYFREQUENCY|POSITION
    WR32(dm + 76, 0);                                    // dmPosition
    WR32(dm + 80, 0);
    WR32(dm + 168, 32);                                  // dmBitsPerPel
    WR32(dm + 172, 1280);                                // dmPelsWidth
    WR32(dm + 176, 720);                                 // dmPelsHeight
    WR32(dm + 184, 60);                                  // dmDisplayFrequency
}
// EnumDisplaySettings(Ex)W(device, mode, DEVMODEW*, ...): the current mode (ENUM_CURRENT/REGISTRY_SETTINGS or 0)
void u_EnumDisplaySettingsW(X86* c) {
    uint32_t mode = ARG(c, 1);
    if (mode != 0xffffffffu && mode != 0xfffffffeu && mode > 0) {
        RET_STD(c, 3, 0);
        return;
    }
    fill_devmodew(ARG(c, 2));
    RET_STD(c, 3, 1);
}
void u_EnumDisplaySettingsExW(X86* c) {
    uint32_t mode = ARG(c, 1);
    if (mode != 0xffffffffu && mode != 0xfffffffeu && mode > 0) {
        RET_STD(c, 4, 0);
        return;
    }
    fill_devmodew(ARG(c, 2));
    RET_STD(c, 4, 1);
}
// GetMonitorInfoW(monitor, MONITORINFOEXW*)
void u_GetMonitorInfoW(X86* c) {
    uint32_t mi = ARG(c, 1);
    write_rect(mi + 4, 0, 0, 1280, 720);
    write_rect(mi + 20, 0, 0, 1280, 720);
    WR32(mi + 36, 1);                                    // MONITORINFOF_PRIMARY
    if (RD32(mi) >= 104) write_wstr(mi + 40, kAdapter, 32);
    RET_STD(c, 2, 1);
}
// EnumDisplayMonitors(dc, clip, callback, data): the one monitor to the callback (stdcall: monitor, dc, rect, data)
void u_EnumDisplayMonitors(X86* c) {
    uint32_t cb = ARG(c, 2), data = ARG(c, 3);
    uint32_t rect = guest_malloc(16);
    write_rect(rect, 0, 0, 1280, 720);
    const uint32_t args[4] = {kHmonitor, 0, rect, data};
    guest_call(c, cb, 4, args, true);
    guest_free(rect);
    RET_STD(c, 4, 1);
}

// ------------------------------------------------------------------------------------------------ GDI32
STDRET(g_CreateDCW, 4, kHdc)
STDRET(g_ChoosePixelFormat, 2, 1)
STDRET(g_GetDeviceGammaRamp, 2, 0)
STDRET(g_SetDeviceGammaRamp, 2, 1)
STDRET(g_CreateBitmap, 5, 0x8000)
STDRET(g_DeleteObject, 1, 1)
STDRET(g_CreateRectRgn, 4, 0x8004)
// CreateDIBSection(dc, BITMAPINFO*, usage, void** bits, section, offset): pixels in guest memory (GLFW's icons and
// cursors write theirs there)
void g_CreateDIBSection(X86* c) {
    uint32_t bi = ARG(c, 1);
    int w = (int)RD32(bi + 4), h = (int)RD32(bi + 8);
    uint32_t bpp = RD16(bi + 14);
    if (h < 0) h = -h;
    uint32_t bytes = (uint32_t)(((w * (int)bpp + 31) / 32) * 4 * h);
    uint32_t bits = guest_calloc(1, bytes ? bytes : 4);
    if (ARG(c, 3)) WR32(ARG(c, 3), bits);
    RET_STD(c, 6, bits ? 0x8008 : 0);
}

// ntdll's RtlVerifyVersionInfo(OSVERSIONINFOEXW*, type mask, condition mask(8)): GLFW calls it (unchecked) for each
// Windows version test; STATUS_REVISION_MISMATCH says older, so it takes its plain paths (AdjustWindowRectEx, no
// per-monitor DPI calls), which the shims answer
void nt_RtlVerifyVersionInfo(X86* c) { RET_STD(c, 4, 0xC0000059u); }

// WGL (GLFW 3.3 looks these up in opengl32.dll): the one window's DC and the one context
STDRET(wgl_GetCurrentDC, 0, kHdc)
STDRET(wgl_GetCurrentContext, 0, kHglrc)

// ------------------------------------------------------------------------------------------------ others
STDRET(a_AdjustTokenPrivileges, 6, 0)
STDRET(a_LookupPrivilegeValueA, 3, 0)
// BCryptGenRandom(algorithm, buffer, size, flags) -> NTSTATUS
void b_GenRandom(X86* c) {
    static std::mt19937 rng((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
    uint32_t p = ARG(c, 1), n = ARG(c, 2);
    for (uint32_t i = 0; i < n; ++i) WR8(p + i, (uint8_t)rng());
    RET_STD(c, 4, 0);
}
STDRET(d_SymGetLineFromAddr64, 5, 0)
STDRET(d_SymGetSymFromAddr64, 5, 0)
void d_UnDecorateSymbolName(X86* c) {
    if (ARG(c, 2)) WR8(ARG(c, 1), 0);
    RET_STD(c, 4, 0);
}
void sh_DragAcceptFiles(X86* c) { RET_STD(c, 2, 0); }
STDRET(sh_DragQueryFileW, 4, 0)
STDRET(sh_DragQueryPoint, 2, 0)
STDRET(sh_DragFinish, 1, 0)
void sh_ShellExecuteA(X86* c) {
    recomp_log("ShellExecuteA %s refused", guest_str(ARG(c, 2), 300).c_str());
    RET_STD(c, 6, 2);                                    // ERROR_FILE_NOT_FOUND (under 32: failed)
}

// OpenAL (cdecl): the strings a device and context report
void al_GetString(X86* c) {
    uint32_t p = ARG(c, 0);
    const char* s = p == 0xB001 ? "OpenAL Community" : p == 0xB002 ? "1.1 ALSOFT" : p == 0xB003 ? "OpenAL Soft" : "";
    RET_CDECL(c, guest_static_str(s));
}
void alc_GetString(X86* c) {
    uint32_t p = ARG(c, 1);
    const char* s = (p == 0x1004 || p == 0x1005 || p == 0x1012 || p == 0x1013) ? "OpenAL Soft" : "";
    RET_CDECL(c, guest_static_str(s));
}
CDECLRET(alc_GetProcAddress, 0)
CDECLRET(alc_IsExtensionPresent, 0)

// Steam's newer entry points: initialisation fails (offline), as SteamAPI_Init does in shims_steam.cpp
void st_InternalInit(X86* c) {
    recomp_log("SteamInternal_SteamAPI_Init -> failed (offline)");
    if (ARG(c, 1)) guest_write_str(ARG(c, 1), "Steam is not running", 1024);
    RET_CDECL(c, 1);                                     // k_ESteamAPIInitResult_FailedGeneric
}
CDECLRET(st_FindOrCreateUserInterface, 0)

// libcurl (cdecl): no network
CDECLRET(curl_easy_init, 0)
CDECLRET(curl_mime_init, 0)
CDECLRET(curl_mime_addpart, 0)
CDECLRET(curl_ret_error, 1)                              // CURLE_UNSUPPORTED_PROTOCOL
CDECLRET(curl_ret0, 0)
void curl_easy_strerror(X86* c) { RET_CDECL(c, guest_static_str("no network")); }

// Epic Online Services (stdcall): EOS_Initialize fails, every handle is null, every query empty
void eos_Initialize(X86* c) {
    recomp_log("EOS_Initialize -> failed (offline)");
    RET_STD(c, 1, 0x1A);                                 // EOS_UnexpectedError
}
void eos_ToString(X86* c) { RET_STD(c, 1, guest_static_str("EOS_UnexpectedError")); }
void eos_ByteArray_ToString(X86* c) {
    if (ARG(c, 3)) WR32(ARG(c, 3), 0);
    RET_STD(c, 4, 0x1A);
}
#define EOS0(n) void eos_ret0_##n(X86* c) { RET_STD(c, n, 0); }
EOS0(0) EOS0(1) EOS0(2) EOS0(3) EOS0(4) EOS0(7)
#define EOSERR(n) void eos_err_##n(X86* c) { RET_STD(c, n, 0x1A); }
EOSERR(2) EOSERR(3)

// the CRT additions (cdecl)
void s_strtol(X86* c) {
    char* end = 0;
    const char* s = (const char*)G(ARG(c, 0));
    long v = strtol(s, &end, (int)ARG(c, 2));
    if (ARG(c, 1)) WR32(ARG(c, 1), ARG(c, 0) + (uint32_t)(end - s));
    RET_CDECL(c, (uint32_t)(int32_t)v);
}
void s_strtoul(X86* c) {
    char* end = 0;
    const char* s = (const char*)G(ARG(c, 0));
    unsigned long v = strtoul(s, &end, (int)ARG(c, 2));
    if (ARG(c, 1)) WR32(ARG(c, 1), ARG(c, 0) + (uint32_t)(end - s));
    RET_CDECL(c, (uint32_t)v);
}
void s_fminf(X86* c) { RET_ST0(c, fminf(ARGF(c, 0), ARGF(c, 1))); }
void s_nextafterf(X86* c) { RET_ST0(c, nextafterf(ARGF(c, 0), ARGF(c, 1))); }
void s_stricmp(X86* c) {
    const char* a = (const char*)G(ARG(c, 0));
    const char* b = (const char*)G(ARG(c, 1));
    int r = 0;
    for (size_t i = 0;; ++i) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x) {
            r = x - y;
            break;
        }
    }
    RET_CDECL(c, (uint32_t)r);
}
void s_iswspace(X86* c) { RET_CDECL(c, iswspace((wint_t)(ARG(c, 0) & 0xffff)) ? 1 : 0); }

const ShimDef kRplus[] = {
    {"kernel32.dll!VirtualAlloc", k_VirtualAlloc}, {"kernel32.dll!VirtualFree", k_VirtualFree},
    {"kernel32.dll!VirtualQuery", k_VirtualQuery}, {"kernel32.dll!VirtualUnlock", k_VirtualUnlock},
    {"kernel32.dll!GetSystemInfo", k_GetSystemInfo}, {"kernel32.dll!GetLargePageMinimum", k_GetLargePageMinimum},
    {"kernel32.dll!GetNumaHighestNodeNumber", k_GetNumaHighestNodeNumber},
    {"kernel32.dll!GetNumaProcessorNode", k_GetNumaProcessorNode},
    {"kernel32.dll!GetNumaNodeProcessorMask", k_GetNumaNodeProcessorMask},
    {"kernel32.dll!FlsAlloc", k_FlsAlloc}, {"kernel32.dll!FlsFree", k_FlsFree}, {"kernel32.dll!FlsSetValue", k_FlsSetValue},
    {"kernel32.dll!K32GetProcessMemoryInfo", k_GetProcessMemoryInfo}, {"kernel32.dll!GetProcessTimes", k_GetProcessTimes},
    {"kernel32.dll!GetEnvironmentVariableA", k_GetEnvironmentVariableA}, {"kernel32.dll!GetStdHandle", k_GetStdHandle},
    {"kernel32.dll!WriteConsoleA", k_WriteConsoleA}, {"kernel32.dll!OutputDebugStringA", k_OutputDebugStringA},
    {"kernel32.dll!VerSetConditionMask", k_VerSetConditionMask}, {"kernel32.dll!FormatMessageW", k_FormatMessageW},
    {"kernel32.dll!GetModuleFileNameW", k_GetModuleFileNameW},
    {"kernel32.dll!K32GetProcessImageFileNameA", k_GetProcessImageFileNameA}, {"kernel32.dll!OpenProcess", k_OpenProcess},
    {"kernel32.dll!GetTempPathA", k_GetTempPathA}, {"kernel32.dll!GetStartupInfoW", k_GetStartupInfoW},
    {"kernel32.dll!DeleteFileA", k_DeleteFileA}, {"kernel32.dll!RemoveDirectoryA", k_RemoveDirectoryA},
    {"kernel32.dll!MoveFileExA", k_MoveFileExA}, {"kernel32.dll!GetFileTime", k_GetFileTime},
    {"kernel32.dll!K32GetModuleInformation", k_GetModuleInformation}, {"kernel32.dll!GlobalFree", k_GlobalFree},
    {"user32.dll!RegisterClassExW", u_RegisterClassExW}, {"user32.dll!CreateWindowExW", u_CreateWindowExW},
    {"user32.dll!UnregisterClassW", u_UnregisterClassW}, {"user32.dll!DefWindowProcW", u_DefWindowProcW},
    {"user32.dll!DispatchMessageW", u_DispatchMessageW}, {"user32.dll!PeekMessageW", u_PeekMessageW},
    {"user32.dll!WaitMessage", u_WaitMessage}, {"user32.dll!MsgWaitForMultipleObjects", u_MsgWaitForMultipleObjects},
    {"user32.dll!PostMessageW", u_PostMessageW}, {"user32.dll!SendMessageW", u_SendMessageW},
    {"user32.dll!SendMessageTimeoutA", u_SendMessageTimeoutA}, {"user32.dll!GetWindowLongW", u_GetWindowLongW},
    {"user32.dll!SetWindowLongW", u_SetWindowLongW}, {"user32.dll!GetClassLongW", u_GetClassLongW},
    {"user32.dll!SetPropW", u_SetPropW}, {"user32.dll!GetPropW", u_GetPropW}, {"user32.dll!RemovePropW", u_RemovePropW},
    {"user32.dll!SetWindowTextW", u_SetWindowTextW}, {"user32.dll!LoadCursorW", u_LoadCursorW},
    {"user32.dll!LoadImageW", u_LoadImageW}, {"user32.dll!CreateIconIndirect", u_CreateIconIndirect},
    {"user32.dll!DestroyIcon", u_DestroyIcon}, {"user32.dll!IsWindow", u_IsWindow},
    {"user32.dll!IsWindowVisible", u_IsWindowVisible}, {"user32.dll!IsIconic", u_IsIconic}, {"user32.dll!IsZoomed", u_IsZoomed},
    {"user32.dll!FlashWindow", u_FlashWindow}, {"user32.dll!MoveWindow", u_MoveWindow},
    {"user32.dll!ScreenToClient", u_ScreenToClient}, {"user32.dll!ChangeWindowMessageFilterEx", u_ChangeWindowMessageFilterEx},
    {"user32.dll!RegisterRawInputDevices", u_RegisterRawInputDevices}, {"user32.dll!GetRawInputData", u_GetRawInputData},
    {"user32.dll!RegisterDeviceNotificationW", u_RegisterDeviceNotificationW}, {"user32.dll!ToUnicode", u_ToUnicode},
    {"user32.dll!MapVirtualKeyW", u_MapVirtualKeyW}, {"user32.dll!SetLayeredWindowAttributes", u_SetLayeredWindowAttributes},
    {"user32.dll!GetLayeredWindowAttributes", u_GetLayeredWindowAttributes},
    {"user32.dll!SetWindowPlacement", u_SetWindowPlacement}, {"user32.dll!GetWindowPlacement", u_GetWindowPlacement},
    {"user32.dll!EnumWindows", u_EnumWindows}, {"user32.dll!MonitorFromWindow", u_MonitorFromWindow},
    {"user32.dll!GetAncestor", u_GetAncestor}, {"user32.dll!GetLastActivePopup", u_GetLastActivePopup},
    {"user32.dll!GetWindowThreadProcessId", u_GetWindowThreadProcessId}, {"user32.dll!GetTitleBarInfo", u_GetTitleBarInfo},
    {"user32.dll!ChangeDisplaySettingsExW", u_ChangeDisplaySettingsExW}, {"user32.dll!GetWindowRect", u_GetWindowRect},
    {"user32.dll!SetRect", u_SetRect}, {"user32.dll!OffsetRect", u_OffsetRect}, {"user32.dll!PtInRect", u_PtInRect},
    {"user32.dll!SystemParametersInfoW", u_SystemParametersInfoW},
    {"user32.dll!EnumDisplayDevicesW", u_EnumDisplayDevicesW}, {"user32.dll!EnumDisplaySettingsW", u_EnumDisplaySettingsW},
    {"user32.dll!EnumDisplaySettingsExW", u_EnumDisplaySettingsExW}, {"user32.dll!GetMonitorInfoW", u_GetMonitorInfoW},
    {"user32.dll!EnumDisplayMonitors", u_EnumDisplayMonitors},
    {"gdi32.dll!CreateDCW", g_CreateDCW}, {"gdi32.dll!ChoosePixelFormat", g_ChoosePixelFormat},
    {"gdi32.dll!GetDeviceGammaRamp", g_GetDeviceGammaRamp}, {"gdi32.dll!SetDeviceGammaRamp", g_SetDeviceGammaRamp},
    {"gdi32.dll!CreateBitmap", g_CreateBitmap}, {"gdi32.dll!DeleteObject", g_DeleteObject},
    {"gdi32.dll!CreateRectRgn", g_CreateRectRgn}, {"gdi32.dll!CreateDIBSection", g_CreateDIBSection},
    {"opengl32.dll!wglGetCurrentDC", wgl_GetCurrentDC}, {"opengl32.dll!wglGetCurrentContext", wgl_GetCurrentContext},
    {"advapi32.dll!AdjustTokenPrivileges", a_AdjustTokenPrivileges},
    {"advapi32.dll!LookupPrivilegeValueA", a_LookupPrivilegeValueA},
    {"bcrypt.dll!BCryptGenRandom", b_GenRandom},
    {"dbghelp.dll!SymGetLineFromAddr64", d_SymGetLineFromAddr64}, {"dbghelp.dll!SymGetSymFromAddr64", d_SymGetSymFromAddr64},
    {"dbghelp.dll!UnDecorateSymbolName", d_UnDecorateSymbolName},
    {"shell32.dll!DragAcceptFiles", sh_DragAcceptFiles}, {"shell32.dll!DragQueryFileW", sh_DragQueryFileW},
    {"shell32.dll!DragQueryPoint", sh_DragQueryPoint}, {"shell32.dll!DragFinish", sh_DragFinish},
    {"shell32.dll!ShellExecuteA", sh_ShellExecuteA},
    {"openal32.dll!alGetString", al_GetString}, {"openal32.dll!alcGetString", alc_GetString},
    {"openal32.dll!alcGetProcAddress", alc_GetProcAddress}, {"openal32.dll!alcIsExtensionPresent", alc_IsExtensionPresent},
    {"steam_api.dll!SteamInternal_SteamAPI_Init", st_InternalInit},
    {"steam_api.dll!SteamInternal_FindOrCreateUserInterface", st_FindOrCreateUserInterface},
    {"libcurl.dll!curl_easy_init", curl_easy_init}, {"libcurl.dll!curl_mime_init", curl_mime_init},
    {"libcurl.dll!curl_mime_addpart", curl_mime_addpart}, {"libcurl.dll!curl_mime_name", curl_ret_error},
    {"libcurl.dll!curl_mime_data", curl_ret_error}, {"libcurl.dll!curl_mime_filedata", curl_ret_error},
    {"libcurl.dll!curl_easy_setopt", curl_ret_error}, {"libcurl.dll!curl_easy_perform", curl_ret_error},
    {"libcurl.dll!curl_easy_cleanup", curl_ret0}, {"libcurl.dll!curl_mime_free", curl_ret0},
    {"libcurl.dll!curl_easy_strerror", curl_easy_strerror},
    {"api-ms-win-crt-convert-l1-1-0.dll!strtol", s_strtol}, {"api-ms-win-crt-convert-l1-1-0.dll!strtoul", s_strtoul},
    {"api-ms-win-crt-math-l1-1-0.dll!fminf", s_fminf}, {"api-ms-win-crt-math-l1-1-0.dll!nextafterf", s_nextafterf},
    {"api-ms-win-crt-string-l1-1-0.dll!_stricmp", s_stricmp}, {"api-ms-win-crt-string-l1-1-0.dll!iswspace", s_iswspace},
};

// EOS by name and stdcall argument bytes (the import names carry them: _EOS_Platform_Tick@4)
struct EosDef {
    const char* name;
    ShimFn fn;
};
const EosDef kEos[] = {
    {"_EOS_Initialize@4", eos_Initialize}, {"_EOS_EResult_ToString@4", eos_ToString},
    {"_EOS_ByteArray_ToString@16", eos_ByteArray_ToString},
};
}  // namespace

void shims_rplus_register() {
    shim_register(kRplus, (int)(sizeof(kRplus) / sizeof(kRplus[0])));
    shim_register_dynamic("ntdll.dll", "RtlVerifyVersionInfo", nt_RtlVerifyVersionInfo);
    // every other EOS import: zero (a null handle, no result, a count of 0), its arguments popped
    static const char* const kEosNames[] = {
        "_EOS_Auth_CopyUserAuthToken@16", "_EOS_Auth_LinkAccount@16", "_EOS_Auth_Login@16", "_EOS_Auth_Token_Release@4",
        "_EOS_Connect_AddNotifyAuthExpiration@16", "_EOS_Connect_CopyProductUserInfo@12", "_EOS_Connect_CreateUser@16",
        "_EOS_Connect_ExternalAccountInfo_Release@4", "_EOS_Connect_GetExternalAccountMapping@8", "_EOS_Connect_Login@16",
        "_EOS_Connect_QueryExternalAccountMappings@16", "_EOS_Connect_QueryProductUserIdMappings@16",
        "_EOS_EpicAccountId_IsValid@4", "_EOS_EpicAccountId_ToString@12", "_EOS_Friends_AddNotifyBlockedUsersUpdate@16",
        "_EOS_Friends_AddNotifyFriendsUpdate@16", "_EOS_Friends_GetBlockedUserAtIndex@8", "_EOS_Friends_GetBlockedUsersCount@8",
        "_EOS_Friends_GetFriendAtIndex@8", "_EOS_Friends_GetFriendsCount@8", "_EOS_Friends_QueryFriends@16",
        "_EOS_LobbyDetails_CopyAttributeByKey@12", "_EOS_LobbyDetails_CopyInfo@12",
        "_EOS_LobbyDetails_CopyMemberAttributeByKey@12", "_EOS_LobbyDetails_GetLobbyOwner@8",
        "_EOS_LobbyDetails_GetMemberByIndex@8", "_EOS_LobbyDetails_GetMemberCount@8", "_EOS_LobbyDetails_Info_Release@4",
        "_EOS_LobbyDetails_Release@4", "_EOS_LobbyModification_AddAttribute@8", "_EOS_LobbyModification_AddMemberAttribute@8",
        "_EOS_LobbyModification_Release@4", "_EOS_LobbyModification_RemoveAttribute@8",
        "_EOS_LobbyModification_RemoveMemberAttribute@8", "_EOS_LobbyModification_SetBucketId@8",
        "_EOS_LobbyModification_SetPermissionLevel@8", "_EOS_LobbySearch_CopySearchResultByIndex@12",
        "_EOS_LobbySearch_Find@16", "_EOS_LobbySearch_GetSearchResultCount@8", "_EOS_LobbySearch_Release@4",
        "_EOS_LobbySearch_SetLobbyId@8", "_EOS_LobbySearch_SetParameter@8", "_EOS_Lobby_AddNotifyJoinLobbyAccepted@16",
        "_EOS_Lobby_AddNotifyLobbyInviteAccepted@16", "_EOS_Lobby_AddNotifyLobbyMemberStatusReceived@16",
        "_EOS_Lobby_Attribute_Release@4", "_EOS_Lobby_CopyLobbyDetailsHandle@12",
        "_EOS_Lobby_CopyLobbyDetailsHandleByInviteId@12", "_EOS_Lobby_CopyLobbyDetailsHandleByUiEventId@12",
        "_EOS_Lobby_CreateLobby@16", "_EOS_Lobby_CreateLobbySearch@12", "_EOS_Lobby_JoinLobby@16", "_EOS_Lobby_LeaveLobby@16",
        "_EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived@12", "_EOS_Lobby_UpdateLobby@16",
        "_EOS_Lobby_UpdateLobbyModification@12", "_EOS_P2P_GetNextReceivedPacketSize@12", "_EOS_P2P_ReceivePacket@28",
        "_EOS_P2P_SendPacket@8", "_EOS_Platform_Create@4", "_EOS_Platform_GetAuthInterface@4",
        "_EOS_Platform_GetConnectInterface@4", "_EOS_Platform_GetFriendsInterface@4", "_EOS_Platform_GetLobbyInterface@4",
        "_EOS_Platform_GetP2PInterface@4", "_EOS_Platform_GetPresenceInterface@4", "_EOS_Platform_GetUserInfoInterface@4",
        "_EOS_Platform_Release@4", "_EOS_Platform_Tick@4", "_EOS_PresenceModification_Release@4",
        "_EOS_PresenceModification_SetData@8", "_EOS_Presence_CopyPresence@12", "_EOS_Presence_CreatePresenceModification@12",
        "_EOS_Presence_HasPresence@8", "_EOS_Presence_Info_Release@4", "_EOS_Presence_QueryPresence@16",
        "_EOS_Presence_SetPresence@16", "_EOS_Shutdown@0", "_EOS_UserInfo_QueryUserInfoByExternalAccount@16",
    };
    std::vector<ShimDef> defs;
    static std::vector<std::string> names;
    names.reserve(128);
    for (size_t i = 0; i < sizeof(kEos) / sizeof(kEos[0]); ++i) {
        names.push_back(std::string("eossdk-win32-shipping.dll!") + kEos[i].name);
        defs.push_back(ShimDef{names.back().c_str(), kEos[i].fn});
    }
    for (size_t i = 0; i < sizeof(kEosNames) / sizeof(kEosNames[0]); ++i) {
        const char* at = strrchr(kEosNames[i], '@');
        int bytes = atoi(at + 1);
        // the Copy/Find/Query/Login/Create calls with an out parameter report an error; the rest return 0
        ShimFn fn = bytes == 0 ? eos_ret0_0 : bytes == 4 ? eos_ret0_1 : bytes == 8 ? eos_ret0_2 : bytes == 12 ? eos_ret0_3
                  : bytes == 16 ? eos_ret0_4 : eos_ret0_7;
        if (strstr(kEosNames[i], "_Copy") && bytes == 12) fn = eos_err_3;
        if (strstr(kEosNames[i], "_Copy") && bytes == 8) fn = eos_err_2;
        names.push_back(std::string("eossdk-win32-shipping.dll!") + kEosNames[i]);
        defs.push_back(ShimDef{names.back().c_str(), fn});
    }
    shim_register(defs.data(), (int)defs.size());
}
