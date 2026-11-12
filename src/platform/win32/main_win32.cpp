// The Windows binary (docs/XSDK_PLAN.md): the console's code on the PC for fast iteration, drawing through the
// same Vulkan renderer. Test runs are headless (--hidden: no window at all, so nothing can take the keyboard
// focus), muted and driven by --script; results come from build/windows/port.log and --shot-file frames.
//
//   isaac360.exe                                   title menu, keyboard or controller, sound on
//   isaac360.exe --direct --seed 12352 --hidden --fast --frames 300 --script "0:70:W" --shot-frame 68
//
// Keyboard: WASD move, arrows fire, X bomb, Z item / menu accept, C card, V drop / menu back, Enter accept,
// Esc back, Space start. A controller works too (XInput).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include "../../app/port_app.h"
#include "../../render/sprite_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

const NativeAudioBackend* NativeWindowsAudioBackend();

static HWND gWindow = 0;
static HMODULE gVulkan = 0;
static const int kWidth = 1280, kHeight = 720;

typedef struct VkWin32SurfaceCreateInfoKHR_ {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
} VkWin32SurfaceCreateInfoKHR_;
typedef VkResult(VKAPI_PTR* CreateWin32Surface)(VkInstance, const VkWin32SurfaceCreateInfoKHR_*, const VkAllocationCallbacks*, VkSurfaceKHR*);

static VkResult CreateSurface(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, VkSurfaceKHR* out) {
    CreateWin32Surface create = (CreateWin32Surface)gipa(instance, "vkCreateWin32SurfaceKHR");
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkWin32SurfaceCreateInfoKHR_ ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = (VkStructureType)1000009000;      // VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
    ci.hinstance = GetModuleHandleA(0);
    ci.hwnd = gWindow;
    return create(instance, &ci, 0, out);
}

static volatile bool gQuit = false;
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        gQuit = true;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static bool CreateGameWindow() {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(0);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "Isaac360Window";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    RECT r = {0, 0, kWidth, kHeight};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    gWindow = CreateWindowExA(0, "Isaac360Window", "The Binding of Isaac: Rebirth", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    if (!gWindow) return false;
    ShowWindow(gWindow, SW_SHOWNORMAL);
    return true;
}

static bool Key(int vk) { return gWindow && GetForegroundWindow() == gWindow && GetAsyncKeyState(vk) < 0; }

// Keyboard and the first connected controller as one pad.
static PortPad ReadPad(bool interactive) {
    PortPad p;
    memset(&p, 0, sizeof(p));
    if (!interactive) return p;
    XINPUT_STATE st;
    memset(&st, 0, sizeof(st));
    for (DWORD i = 0; i < 4; ++i)
        if (XInputGetState(i, &st) == ERROR_SUCCESS) {
            const XINPUT_GAMEPAD& g = st.Gamepad;
            p.buttons = g.wButtons;
            p.lx = g.sThumbLX;
            p.ly = g.sThumbLY;
            p.rx = g.sThumbRX;
            p.ry = g.sThumbRY;
            p.lt = g.bLeftTrigger;
            p.rt = g.bRightTrigger;
            p.connected = true;
            break;
        }
    if (Key('A')) p.lx = -32767;
    if (Key('D')) p.lx = 32767;
    if (Key('W')) p.ly = 32767;
    if (Key('S')) p.ly = -32767;
    if (Key(VK_LEFT)) p.rx = -32767;
    if (Key(VK_RIGHT)) p.rx = 32767;
    if (Key(VK_UP)) p.ry = 32767;
    if (Key(VK_DOWN)) p.ry = -32767;
    if (Key('X')) p.buttons |= PAD_X;
    if (Key('Z') || Key(VK_RETURN)) p.buttons |= PAD_A;
    if (Key('C')) p.buttons |= PAD_LB;
    if (Key('V')) p.buttons |= PAD_B;
    if (Key(VK_ESCAPE)) p.buttons |= PAD_BACK;
    if (Key(VK_SPACE)) p.buttons |= PAD_START;
    // the menus read the D-pad: arrows and WASD move the cursor too
    if (Key(VK_UP) || Key('W')) p.buttons |= PAD_UP;
    if (Key(VK_DOWN) || Key('S')) p.buttons |= PAD_DOWN;
    if (Key(VK_LEFT) || Key('A')) p.buttons |= PAD_LEFT;
    if (Key(VK_RIGHT) || Key('D')) p.buttons |= PAD_RIGHT;
    return p;
}

static bool SaveBMP(const char* path, const unsigned char* rgba, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    BITMAPFILEHEADER bf;
    BITMAPINFOHEADER bi;
    memset(&bf, 0, sizeof(bf));
    memset(&bi, 0, sizeof(bi));
    bf.bfType = 0x4d42;
    bf.bfOffBits = sizeof(bf) + sizeof(bi);
    bf.bfSize = bf.bfOffBits + w * h * 4;
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = w * h * 4;
    fwrite(&bf, 1, sizeof(bf), f);
    fwrite(&bi, 1, sizeof(bi), f);
    std::vector<unsigned char> row((size_t)w * 4);
    for (int y = h - 1; y >= 0; --y) {
        const unsigned char* s = rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 4 + 0] = s[x * 4 + 2];
            row[x * 4 + 1] = s[x * 4 + 1];
            row[x * 4 + 2] = s[x * 4 + 0];
            row[x * 4 + 3] = 255;
        }
        fwrite(&row[0], 1, row.size(), f);
    }
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    bool hidden = false, fast = false, mute = false, validate = false;
    std::vector<int> shotFrames;
    std::string shotFile = "build/windows/frame.bmp";
    std::string dataRoot = "runtime/";          // --data build/console/isaac360 reads the console's pack
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--hidden")) hidden = true;
        else if (!strcmp(argv[i], "--fast")) fast = true;
        else if (!strcmp(argv[i], "--mute")) mute = true;
        else if (!strcmp(argv[i], "--validate")) validate = true;
        else if (!strcmp(argv[i], "--shot-frame") && i + 1 < argc) shotFrames.push_back(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--shot-file") && i + 1 < argc) shotFile = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) dataRoot = argv[++i];
    }
    if (getenv("ISAAC360_MUTE")) mute = true;
    CreateDirectoryA("build", 0);
    CreateDirectoryA("build\\windows", 0);
    CreateDirectoryA("build\\windows\\profile", 0);
    PortLogOpen("build/windows/port.log", "WIN");
    PortAppOptions options;
    options.Parse(argc, argv);

    gVulkan = LoadLibraryA("vulkan-1.dll");
    PortVkHooks hooks;
    hooks.getInstanceProcAddr = gVulkan ? (PFN_vkGetInstanceProcAddr)GetProcAddress(gVulkan, "vkGetInstanceProcAddr") : 0;
    hooks.validation = validate;
    if (!hidden) {
        if (!CreateGameWindow()) {
            PortLog("window creation failed");
            return 2;
        }
        hooks.surfaceExtension = "VK_KHR_win32_surface";
        hooks.createSurface = CreateSurface;
    }
    PortRenderer* renderer = hooks.getInstanceProcAddr ? PortCreateRenderer(hooks, kWidth, kHeight) : 0;
    if (!renderer) PortLog("no renderer: frames are not drawn");
    else if (!shotFrames.empty()) renderer->KeepLastFrame(true);

    static PortApp app;     // large: keep it off the stack
    PortPlatformInfo platform;
    platform.name = "WIN";
    platform.dataRoot = dataRoot.c_str();
    platform.saveDirectory = "build/windows/profile/";
    // hidden runs are tests: muted, the mixer still runs for the counters
    platform.audio = (hidden || mute) ? 0 : NativeWindowsAudioBackend();
    platform.entropy = GetTickCount();
    app.Init(options, platform);
    if (renderer && !app.frontendAtlas.empty()) renderer->SetTexture(PT_FRONTEND, app.frontendAtlasW, app.frontendAtlasH, &app.frontendAtlas[0]);
    if (renderer && !app.gameplayAtlas.empty()) renderer->SetTexture(PT_GAMEPLAY, app.gameplayAtlasW, app.gameplayAtlasH, &app.gameplayAtlas[0]);

    PortDrawList list;
    std::vector<unsigned char> pixels;
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    double accumulator = 0;
    const bool interactive = !hidden && options.script.empty();
    for (;;) {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (gQuit) break;
        int ticks = 1;
        if (!fast && !hidden) {
            QueryPerformanceCounter(&now);
            double elapsed = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
            last = now;
            if (elapsed > 0.25) elapsed = 0.25;
            accumulator += elapsed;
            ticks = 0;
            while (accumulator >= 1.0 / 30.0) {
                accumulator -= 1.0 / 30.0;
                ++ticks;
            }
        }
        bool running = true;
        for (int t = 0; t < ticks && running; ++t) {
            const int frame = app.Frame();
            running = app.Tick(ReadPad(interactive));
            for (size_t s = 0; s < shotFrames.size() && running; ++s) {
                if (shotFrames[s] != frame || !renderer) continue;
                app.BuildFrame(list);
                renderer->Draw(list);
                renderer->Present();
                pixels.resize((size_t)kWidth * kHeight * 4);
                std::string path = shotFile;
                if (shotFrames.size() > 1) {
                    size_t dot = path.rfind('.');
                    char suffix[32];
                    sprintf(suffix, "_%05d", frame);
                    path.insert(dot == std::string::npos ? path.size() : dot, suffix);
                }
                const bool ok = renderer->ReadPixels(&pixels[0]) && SaveBMP(path.c_str(), &pixels[0], kWidth, kHeight);
                PortLog("screenshot frame=%d file=%s ok=%d vertices=%u", frame, path.c_str(), ok ? 1 : 0,
                        (unsigned int)list.vertices.size());
            }
        }
        if (!running) break;
        if (renderer && !hidden) {
            app.BuildFrame(list);
            renderer->Draw(list);
            renderer->Present();       // FIFO: paces the loop to the display
        }
        if (!fast && hidden) Sleep(1);
    }
    app.Shutdown();
    delete renderer;
    PortLogClose();
    return app.ExitCode();
}
