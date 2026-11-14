// Windows host of the recompiled game (docs/RECOMP_PLAN.md): runs the translated isaac-ng.exe from the user's
// game folder. Test runs stay hidden and muted (the platform shims honour ISAAC360_HIDDEN / ISAAC360_MUTE).
//   isaac360_recomp.exe [--game game] [--mem 512] [--log build/recomp/run.log] [--hidden] [--vk]
// --vk (or ISAAC360_GL=vk) draws through the console's GL-on-Vulkan layer (gl_vk.cpp, headless) instead of
// passing GL to the host driver.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "runtime.h"
#include "gl_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

void shims_register_all();

// audio output: test runs are muted (the mixer still runs in real time); TODO waveOut for interactive runs.
// ISAAC360_AUDIO_DUMP=path.wav keeps what the mixer puts out (16-bit stereo) for checks.
static FILE* g_audioDump = 0;
static uint32_t g_audioFrames = 0;
static int g_audioRate = 44100;
static void audio_dump_header() {
    uint32_t bytes = g_audioFrames * 4;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + bytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;
    memcpy(h + 16, &v, 4);
    uint16_t s = 1;
    memcpy(h + 20, &s, 2);
    s = 2;
    memcpy(h + 22, &s, 2);
    v = (uint32_t)g_audioRate;
    memcpy(h + 24, &v, 4);
    v = (uint32_t)g_audioRate * 4;
    memcpy(h + 28, &v, 4);
    s = 4;
    memcpy(h + 32, &s, 2);
    s = 16;
    memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(g_audioDump, 0, SEEK_SET);
    fwrite(h, 1, 44, g_audioDump);
    fseek(g_audioDump, 0, SEEK_END);
}
void audio_out_open(int rate) {
    g_audioRate = rate;
    const char* dump = getenv("ISAAC360_AUDIO_DUMP");
    if (dump && !g_audioDump && (g_audioDump = fopen(dump, "wb+")) != 0) audio_dump_header();
}
// the PC's mixer paces itself (shims_al.cpp mixer_thread), so its dumps follow the game's timeline
bool audio_out_pull(void (*)(int16_t*, int)) { return false; }
void audio_out_submit(const int16_t* stereo, int frames) {
    if (!g_audioDump || frames <= 0) return;
    fwrite(stereo, 4, (size_t)frames, g_audioDump);
    g_audioFrames += (uint32_t)frames;
    if (g_audioFrames % (g_audioRate / 2) < (uint32_t)frames) audio_dump_header();   // twice a second
}

static void log_line(const char* line) { recomp_log("%s", line); }

// a host fault nothing handled (a guest pointer gone wrong in translated code): where each guest thread was
static LONG WINAPI host_fault(EXCEPTION_POINTERS* e) {
    recomp_log("host fault %08lx at %p (exe +%llx; address %p)", e->ExceptionRecord->ExceptionCode,
               e->ExceptionRecord->ExceptionAddress,
               (unsigned long long)((char*)e->ExceptionRecord->ExceptionAddress - (char*)GetModuleHandleA(0)),
               e->ExceptionRecord->NumberParameters > 1 ? (void*)e->ExceptionRecord->ExceptionInformation[1] : 0);
    recomp_sample_threads(log_line);
    recomp_log_drain();
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(host_fault);
    RecompConfig cfg;
    cfg.gameDir = "game";
    cfg.saveDir = "build/recomp/save";
    cfg.dllDir = "build/recomp/dlls";
    cfg.memSize = 512u << 20;
    cfg.logPath = "build/recomp/run.log";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) cfg.gameDir = argv[++i];
        else if (!strcmp(argv[i], "--mem") && i + 1 < argc) cfg.memSize = (uint32_t)atoi(argv[++i]) << 20;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) cfg.logPath = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) cfg.saveDir = argv[++i];
        else if (!strcmp(argv[i], "--hidden")) _putenv("ISAAC360_HIDDEN=1");
        else if (!strcmp(argv[i], "--vk")) _putenv("ISAAC360_GL=vk");
        else if (!strcmp(argv[i], "--data-overlay") && i + 1 < argc) cfg.dataOverlay = argv[++i];
    }
    const char* gl = getenv("ISAAC360_GL");
    if (gl && !strcmp(gl, "vk")) {
        HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
        GlVkHooks hooks;
        memset(&hooks, 0, sizeof(hooks));
        hooks.getInstanceProcAddr = vulkan ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkan, "vkGetInstanceProcAddr") : 0;
        hooks.validation = getenv("ISAAC360_VK_VALIDATION") != 0;
        gl_vk_configure(hooks);
    }
    shims_register_all();
    int code = recomp_run(cfg);
    recomp_log_drain();
    return code;
}
