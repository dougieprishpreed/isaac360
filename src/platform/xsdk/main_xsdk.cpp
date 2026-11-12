// The console's entry point on the open xsdk (docs/XSDK_PLAN.md). The title's folder is the current directory:
// isaac360.pak holds the data (tools/stage_console.py), saves go to save/, the log to isaac360.log. Options come
// from isaac360_args.txt beside the XEX (whitespace-separated, as on the PC's command line).
// The display is vulkan4xenon's one 1280x720 mode; FIFO presents pace the loop at 60 Hz and the game ticks at
// its original 30. Back + Start held for two seconds returns to the dashboard. xsdk has no fault handler: a
// watchdog thread logs where the game stood when frames stop.
#include "../../app/port_app.h"
#include "../../render/sprite_vk.h"
#include <xsdk/kernel.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);
const NativeAudioBackend* NativeXsdkAudioBackend();
void NativeXsdkAudioStop();

#ifndef ISAAC_GIT_COMMIT
#define ISAAC_GIT_COMMIT "unknown"
#endif

// The console's one display at its one mode (vulkan4xenon docs/SUPPORT.md, "Display and swapchain").
static VkResult CreateSurface(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, VkSurfaceKHR* out) {
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceDisplayPropertiesKHR displays =
        (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)gipa(instance, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    PFN_vkGetDisplayModePropertiesKHR modes = (PFN_vkGetDisplayModePropertiesKHR)gipa(instance, "vkGetDisplayModePropertiesKHR");
    PFN_vkCreateDisplayPlaneSurfaceKHR create = (PFN_vkCreateDisplayPlaneSurfaceKHR)gipa(instance, "vkCreateDisplayPlaneSurfaceKHR");
    if (!enumerate || !displays || !modes || !create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    uint32_t n = 1;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    enumerate(instance, &n, &gpu);
    if (!gpu) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t dn = 1;
    VkDisplayPropertiesKHR display;
    memset(&display, 0, sizeof(display));
    if (displays(gpu, &dn, &display) < 0 || !dn) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t mn = 1;
    VkDisplayModePropertiesKHR mode;
    memset(&mode, 0, sizeof(mode));
    if (modes(gpu, display.display, &mn, &mode) < 0 || !mn) return VK_ERROR_INITIALIZATION_FAILED;
    VkDisplaySurfaceCreateInfoKHR ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    ci.displayMode = mode.displayMode;
    ci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.globalAlpha = 1.0f;
    ci.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    ci.imageExtent = mode.parameters.visibleRegion;
    return create(instance, &ci, 0, out);
}

static void ReadArgs(std::vector<std::string>& args) {
    FILE* f = fopen("isaac360_args.txt", "rb");
    if (!f) return;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    for (char* t = strtok(buf, " \t\r\n"); t; t = strtok(0, " \t\r\n")) args.push_back(t);
}

static PortPad ReadPad(unsigned short* allHeld) {
    PortPad p;
    memset(&p, 0, sizeof(p));
    *allHeld = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        INPUT_STATE st;
        memset(&st, 0, sizeof(st));
        if (XamInputGetState(i, 0, &st) != 0) continue;
        const INPUT_GAMEPAD& g = st.gamepad;
        *allHeld |= g.buttons;
        if (p.connected) continue;          // the first connected pad plays
        p.connected = true;
        p.buttons = g.buttons;              // INPUT_GAMEPAD_* are XInput's bits, as PortPadButton
        p.lx = g.thumb_lx;
        p.ly = g.thumb_ly;
        p.rx = g.thumb_rx;
        p.ry = g.thumb_ry;
        p.lt = g.left_trigger;
        p.rt = g.right_trigger;
    }
    return p;
}

static double Now() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------------------------------------ watchdog
static volatile int gWatchFrame = 0;
static const char* volatile gWatchStage = "start";

static void* Watchdog(void*) {
    xsdk_thread_move(5);
    int last = gWatchFrame, still = 0;
    for (;;) {
        timespec ts = {0, 500000000};
        nanosleep(&ts, 0);
        int f = gWatchFrame;
        if (f != last) {
            last = f;
            still = 0;
            continue;
        }
        still += 500;
        if (still % 5000 == 0) PortLog("watchdog: no frame for %d ms at frame %d, stage '%s'", still, f, gWatchStage);
    }
    return 0;
}

// The game runs on a thread of its own with the 8 MB of stack the Windows binary links with (/STACK): the
// XEX's main thread has 1 MB, and the native room and player code keeps large tables on the stack.
static int gExitCode = 0;
static void* GameMain(void*);

int main() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u * 1024u * 1024u);
    pthread_t game;
    if (pthread_create(&game, &attr, GameMain, 0) != 0) GameMain(0);
    else pthread_join(game, 0);
    fflush(stdout);
    // static destructors would run under the kernel's threads
    _exit(gExitCode);
}

static void* GameMain(void*) {
    xsdk_thread_move(0);
    PortLogOpen("isaac360.log", "X360");
    PortLog("isaac360 (xSDK) %s", ISAAC_GIT_COMMIT);
    std::vector<std::string> args;
    args.push_back("isaac360");
    ReadArgs(args);
    std::vector<char*> argv;
    for (size_t i = 0; i < args.size(); ++i) argv.push_back(&args[i][0]);
    argv.push_back(0);
    PortAppOptions options;
    options.Parse((int)args.size(), &argv[0]);
    mkdir("save", 0777);

    pthread_t watchdog;
    if (pthread_create(&watchdog, 0, Watchdog, 0) == 0) pthread_detach(watchdog);
    xsdk_system_ui_open();                  // starts listening for the Guide

    gWatchStage = "renderer";
    PortVkHooks hooks;
    hooks.getInstanceProcAddr = vkGetInstanceProcAddr;
    hooks.surfaceExtension = VK_KHR_DISPLAY_EXTENSION_NAME;
    hooks.createSurface = CreateSurface;
    PortRenderer* renderer = PortCreateRenderer(hooks, 1280, 720);
    if (!renderer) PortLog("no renderer");

    gWatchStage = "init";
    static PortApp app;
    PortPlatformInfo platform;
    platform.name = "X360";
    platform.dataRoot = "";
    platform.saveDirectory = "save/";
    platform.audio = NativeXsdkAudioBackend();
    platform.entropy = (unsigned int)(Now() * 1000.0);
    app.Init(options, platform);
    if (renderer && !app.frontendAtlas.empty()) renderer->SetTexture(PT_FRONTEND, app.frontendAtlasW, app.frontendAtlasH, &app.frontendAtlas[0]);
    if (renderer && !app.gameplayAtlas.empty()) renderer->SetTexture(PT_GAMEPLAY, app.gameplayAtlasW, app.gameplayAtlasH, &app.gameplayAtlas[0]);
    // the renderer holds its copies now
    std::vector<unsigned char>().swap(app.frontendAtlas);
    std::vector<unsigned char>().swap(app.gameplayAtlas);

    PortDrawList list;
    double last = Now(), accumulator = 0;
    int quitHeld = 0;
    bool running = true;
    while (running) {
        unsigned short held = 0;
        const PortPad pad = ReadPad(&held);
        const bool systemUi = xsdk_system_ui_open();
        quitHeld = !systemUi && (held & PAD_BACK) && (held & PAD_START) ? quitHeld + 1 : 0;
        if (quitHeld > 120) {
            PortLog("Back + Start held, quitting");
            break;
        }
        const double now = Now();
        double elapsed = now - last;
        last = now;
        if (elapsed > 0.25) elapsed = 0.25;
        if (!systemUi) accumulator += elapsed;     // the Guide pauses the game
        while (running && accumulator >= 1.0 / 30.0) {
            accumulator -= 1.0 / 30.0;
            gWatchStage = "tick";
            running = app.Tick(pad);
        }
        gWatchStage = "draw";
        if (renderer) {
            app.BuildFrame(list);
            renderer->Draw(list);
            renderer->Present();
        }
        ++gWatchFrame;
    }
    app.Shutdown();
    NativeXsdkAudioStop();
    PortLog("exit %d", app.ExitCode());
    PortLogClose();
    gExitCode = app.ExitCode();
    return 0;
}
