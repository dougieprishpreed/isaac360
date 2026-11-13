// Console host of the recompiled game on the open xsdk (docs/RECOMP_PLAN.md). The title's folder is the current
// directory, staged by tools/stage_console.py: game/ holds the user's game files (isaac-ng.exe, resources/ with the
// converted sound archives), dlls/ the rebased runtime DLLs, save/ the game's saves and anything it writes beside
// itself, isaac360.log the log. Options from isaac360_args.txt beside the XEX: --mem MB (guest address space), and
// NAME=value settings the PC takes from the environment (ISAAC360_SHOTS=600,1200 etc.; screenshots land beside it).
//
// * Guest memory is reserved whole and committed as the game's heap grows.
// * The game draws through the GL-on-Vulkan layer (gl_vk.cpp) on vulkan4xenon's display: the output's own size 1:1
//   (a 1080p output's 1920x1080; tools/xsdk/patches), else 1280x720 scaled to it.
// * XInput reads the console's pads (index 0..3); OpenAL's mixer feeds the kernel's audio frames through a ring.
// * Back + Start held for two seconds returns to the dashboard; a watchdog logs when frames stop (xsdk has no
//   fault handler).
#include "runtime.h"
#include "gl_vk.h"
#include "vfile.h"
#include <vulkan4xenon.h>
#include <xsdk/audio.h>
#include <xsdk/kernel.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);
void shims_register_all();
uint32_t gl_frame_count();                           // shims_gl.cpp

struct HostPad {                                     // shims_input.cpp
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
    bool connected;
};
extern bool (*g_hostPadSource)(int index, HostPad* out);
extern uint32_t (*g_hostFreeMemory)();             // shims_gl.cpp
extern uint32_t (*g_hostHeapBytes)();
extern "C" size_t spire_big_bytes(void);            // src/recomp/big_alloc.c
extern std::atomic<bool> g_quitRequested;           // shims_gl.cpp: the game thread quits at its frame's end
extern void (*g_hostQuit)();

#ifndef ISAAC_GIT_COMMIT
#define ISAAC_GIT_COMMIT "unknown"
#endif

namespace {

// ------------------------------------------------------------------------------------------------ display
VkResult CreateSurface(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, VkSurfaceKHR* out) {
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

// ------------------------------------------------------------------------------------------------ memory
uint8_t* ReserveMemory(uint32_t size) {
    void* p = 0;
    uint32_t n = size;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(&p, &n, MEM_RESERVE, PAGE_READWRITE, 0))) return 0;
    return (uint8_t*)p;
}

bool CommitMemory(uint8_t* base, uint32_t offset, uint32_t size) {
    void* p = base + offset;
    uint32_t n = size;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(&p, &n, MEM_COMMIT, PAGE_READWRITE, 0))) return false;
    memset(base + offset, 0, size);                  // the guest heap expects zeroed fresh memory
    return true;
}

// ------------------------------------------------------------------------------------------------ pads
volatile uint16_t gHeld = 0;                         // every pad's buttons (the quit chord)

bool ReadPad(int index, HostPad* out) {
    INPUT_STATE st;
    memset(&st, 0, sizeof(st));
    if (XamInputGetState((uint32_t)index, 0, &st) != 0) return false;
    const INPUT_GAMEPAD& g = st.gamepad;
    out->connected = true;
    out->buttons = g.buttons;                        // INPUT_GAMEPAD_* are XInput's bits
    out->lx = g.thumb_lx;
    out->ly = g.thumb_ly;
    out->rx = g.thumb_rx;
    out->ry = g.thumb_ry;
    out->lt = g.left_trigger;
    out->rt = g.right_trigger;
    return true;
}

// ------------------------------------------------------------------------------------------------ threads
// hardware threads: the game alone on core 0 (thread 0; its twin, 1, holds nothing busy, as two threads on one core
// share its pipeline), the game's other threads and the sound effects loader on core 1 (2 and 3), the OpenAL mixer
// on 2, the audio feeder on 4 (where the kernel's audio callback runs), the watchdog and the profiler on 5
void ThreadStarted(const char* role) {
    static volatile int next = 0;
    if (!strcmp(role, "main")) xsdk_thread_move(0);
    else if (!strcmp(role, "audio")) xsdk_thread_move(2);
    else if (!strcmp(role, "profiler")) xsdk_thread_move(5);
    else xsdk_thread_move(2 + (__sync_fetch_and_add(&next, 1) % 2));
}

// ------------------------------------------------------------------------------------------------ audio
// The mixer (shims_al.cpp) runs at 44.1 kHz stereo and hands blocks to audio_out_submit; a feeder thread fills the
// kernel's render frames (256 samples of 6 channels at 48 kHz, floats, a channel after another) from a ring,
// resampled, 12 frames (64 ms) ahead.
const int kRing = 16384;                             // stereo frames, ~370 ms at 44.1 kHz
short gRing[kRing * 2];
volatile unsigned int gWrite = 0, gRead = 0;
volatile bool gAudioRunning = false;
double gSourcePos = 0;
double gStep = 44100.0 / 48000.0;

unsigned int Filled() { return gWrite - gRead; }

void* Feed(void*) {
    xsdk_thread_move(4);
    unsigned int frames = 0, starved = 0;
    while (gAudioRunning) {
        float* out = 0;
        if (xsdk_audio_next_frame(100, &out) != 0 || !out) continue;
        memset(out, 0, XSDK_AUDIO_FRAME_FLOATS * sizeof(float));
        float* left = out + XSDK_AUDIO_FRONT_LEFT * XSDK_AUDIO_FRAME_SAMPLES;
        float* right = out + XSDK_AUDIO_FRONT_RIGHT * XSDK_AUDIO_FRAME_SAMPLES;
        const float scale = 1.0f / 32768.0f;
        __sync_synchronize();
        for (int i = 0; i < XSDK_AUDIO_FRAME_SAMPLES; ++i) {
            if (Filled() < 2) {
                ++starved;
                break;
            }
            const unsigned int a = gRead % kRing, b = (gRead + 1) % kRing;
            const float t = (float)gSourcePos;
            left[i] = (gRing[a * 2] + (gRing[b * 2] - gRing[a * 2]) * t) * scale;
            right[i] = (gRing[a * 2 + 1] + (gRing[b * 2 + 1] - gRing[a * 2 + 1]) * t) * scale;
            gSourcePos += gStep;
            while (gSourcePos >= 1.0) {
                gSourcePos -= 1.0;
                ++gRead;
            }
        }
        __sync_synchronize();
        xsdk_audio_queue_frame();
        if (++frames % (188 * 60) == 0) {
            XsdkAudioStats st;
            xsdk_audio_stats(&st);
            recomp_log("audio: %u frames played, %u silent, fewest queued %u, longest wait %u us, starved %u",
                       (unsigned)st.played, (unsigned)st.silent, (unsigned)st.fewest_queued, (unsigned)st.longest_wait_us, starved);
        }
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------ watchdog
// the title's free physical memory (the frame log lines): what the console has left after the XEX, the guest
// heap committed so far, the GPU's images and buffers
uint32_t FreeMemory() {
    MM_QUERY_STATISTICS_RESULT st;
    memset(&st, 0, sizeof(st));
    st.size = sizeof(st);
    return NT_SUCCESS(MmQueryStatistics(&st)) ? st.title.available_pages * 4096u : 0;
}

// the C heap newlib took from the kernel (it never gives back below its top) and the big blocks beside it
uint32_t HostHeapBytes() { return (uint32_t)mallinfo().arena + (uint32_t)spire_big_bytes(); }

void LogLine(const char* line) { recomp_log("%s", line); }

void Quit() {
    vf_drain();                         // the files the game wrote reach the drive
    recomp_log_drain();
    _exit(0);
}

void* Watchdog(void*) {
    xsdk_thread_move(5);
    uint32_t last = gl_frame_count();
    int still = 0, quitHeld = 0, quitWaited = 0;
    for (;;) {
        timespec ts = {0, 100000000};
        nanosleep(&ts, 0);
        // Back + Start on any pad for two seconds
        uint16_t held = 0;
        for (int i = 0; i < 4; ++i) {
            HostPad p;
            memset(&p, 0, sizeof(p));
            if (ReadPad(i, &p)) held |= p.buttons;
        }
        quitHeld = !xsdk_system_ui_open() && (held & 0x0020) && (held & 0x0010) ? quitHeld + 1 : 0;
        if (quitHeld >= 20 && !g_quitRequested.load()) {
            recomp_log("Back + Start held, quitting");
            g_quitRequested.store(true);
        }
        // a game that doesn't reach its frame's end (loading, stuck) goes anyway
        if (g_quitRequested.load() && ++quitWaited > 30) {
            recomp_log("no frame ended within 3 s of the quit: quitting from the watchdog");
            recomp_log_drain();
            _exit(0);
        }
        uint32_t f = gl_frame_count();
        if (f != last) {
            last = f;
            still = 0;
            continue;
        }
        still += 100;
        if (still % 5000 == 0) {
            // loading slowly or stuck: the file counters move while it loads, the stacks show where it is
            uint64_t opens, bytes, disk;
            vf_stats(&opens, &bytes, &disk);
            recomp_log("watchdog: no frame for %d ms at frame %u; heap %u MB, free %u MB, %llu opens, read %llu MB "
                       "(%llu MB from the drive)", still, f, guest_heap_used() >> 20, FreeMemory() >> 20,
                       (unsigned long long)opens, (unsigned long long)(bytes >> 20), (unsigned long long)(disk >> 20));
            recomp_sample_threads(LogLine);
        }
    }
    return 0;
}

void ReadArgs(std::vector<std::string>& args) {
    FILE* f = fopen("isaac360_args.txt", "rb");
    if (!f) return;
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    for (char* t = strtok(buf, " \t\r\n"); t; t = strtok(0, " \t\r\n")) args.push_back(t);
}

int gExitCode = 0;

void* GameMain(void*) {
    ThreadStarted("main");
    RecompConfig cfg;
    cfg.gameDir = "game";
    cfg.saveDir = "save";
    cfg.dllDir = "dlls";
    cfg.memSize = 320u << 20;                        // address space; only the heap the game uses is committed
    cfg.logPath = "isaac360.log";
    cfg.reserveMemory = ReserveMemory;
    cfg.commitMemory = CommitMemory;
    cfg.threadStarted = ThreadStarted;
    std::vector<std::string> args;
    ReadArgs(args);
    setenv("ISAAC360_OUT_DIR", ".", 1);
    for (size_t i = 0; i < args.size(); ++i) {
        size_t eq = args[i].find('=');
        if (args[i] == "--mem" && i + 1 < args.size()) cfg.memSize = (uint32_t)atoi(args[++i].c_str()) << 20;
        else if (eq != std::string::npos && eq > 0) setenv(args[i].substr(0, eq).c_str(), args[i].c_str() + eq + 1, 1);
    }
    mkdir("save", 0777);
    // The game lays out for a 960x540 picture, as on a PC at 1920x1080, which it draws there with each pixel doubled:
    // at 1280x720 it takes its small-screen HUD (larger outlined icons) and scales an 852x480 picture by 1.5 into
    // uneven pixels. A 960x540 window gets the 1080p layout without the 1080p frame (8 MB, and targets past eDRAM
    // that vulkan4xenon draws in bands). On a 1080p output the display's front buffer is 1920x1080 shown 1:1 and
    // the present pass (shaders.py PRESENT_FS) gives each pixel an exact 2x2, the PC's 1080p picture; elsewhere it
    // grows the picture to 1280x720, each output pixel the area-weighted average of the pixels under it, which the
    // display scales to the output. Cutscenes draw at the window's size too. isaac360_args.txt can say
    // ISAAC360_WINDOW=1920x1080 or 1280x720, and ISAAC360_DISPLAY=720p for 1280x720 on every output (boot 14's picture).
    setenv("ISAAC360_WINDOW", "960x540", 0);
    const char* display = getenv("ISAAC360_DISPLAY");
    v4x_set_display_output(!(display && !strcmp(display, "720p")));

    GlVkHooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.getInstanceProcAddr = vkGetInstanceProcAddr;
    hooks.surfaceExtension = VK_KHR_DISPLAY_EXTENSION_NAME;
    hooks.createSurface = CreateSurface;
    gl_vk_configure(hooks);
    g_hostPadSource = ReadPad;
    g_hostFreeMemory = FreeMemory;
    g_hostHeapBytes = HostHeapBytes;
    g_hostQuit = Quit;

    pthread_t watchdog;
    if (pthread_create(&watchdog, 0, Watchdog, 0) == 0) pthread_detach(watchdog);
    xsdk_system_ui_open();                           // starts listening for the Guide

    shims_register_all();
    gExitCode = recomp_run(cfg);
    recomp_log("exit %d", gExitCode);
    recomp_log_drain();
    return 0;
}

}  // namespace

// OpenAL's mixer output (shims_al.cpp)
void audio_out_open(int rate) {
    if (gAudioRunning) return;
    gStep = (double)rate / 48000.0;
    if (xsdk_audio_open(12) != 0) {
        recomp_log("audio: no audio output");
        return;
    }
    gAudioRunning = true;
    pthread_t t;
    if (pthread_create(&t, 0, Feed, 0) != 0) {
        gAudioRunning = false;
        recomp_log("audio: no feeder thread");
        return;
    }
    pthread_detach(t);
    recomp_log("audio: xsdk feeder, %d Hz mix resampled to 48 kHz, 12 frames ahead", rate);
}

// blocks while the ring is full, so the mixer runs at the output's pace
void audio_out_submit(const int16_t* stereo, int frames) {
    if (!gAudioRunning || frames <= 0) return;
    while (gAudioRunning && Filled() + (unsigned int)frames > (unsigned int)kRing) {
        timespec ts = {0, 2000000};
        nanosleep(&ts, 0);
    }
    for (int i = 0; i < frames; ++i) {
        const unsigned int at = (gWrite + (unsigned int)i) % kRing;
        gRing[at * 2] = stereo[i * 2];
        gRing[at * 2 + 1] = stereo[i * 2 + 1];
    }
    __sync_synchronize();
    gWrite += (unsigned int)frames;
}

int main() {
    // the game runs on a thread with a large stack: translated functions keep the guest's registers in locals
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024u * 1024u);
    pthread_t game;
    if (pthread_create(&game, &attr, GameMain, 0) != 0) GameMain(0);
    else pthread_join(game, 0);
    fflush(stdout);
    _exit(gExitCode);
}
