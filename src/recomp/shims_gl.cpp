// OpenGL for the recompiled game (docs/RECOMP_PLAN.md, phase 3). The game loads every entry point by name
// (glad-style: GL 1.1 through GetProcAddress on opengl32.dll, the rest through wglGetProcAddress). Each name gets
// a thunk: implemented ones run the GL state machine here (drawn through the shared Vulkan renderer), the rest
// stop with their name the first time the game calls them, so the set grows with what the game really uses.
#include "runtime.h"
#include "gl_vk.h"
#include "guest_lock.h"
#include "vfile.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

bool gl_create_context();

namespace {
std::map<std::string, ShimFn> g_gl;
std::map<std::string, uint32_t> g_glThunks;
uint32_t g_frames = 0;

void gl_trap(X86* c) { recomp_fatal(c, "GL function %s is not implemented", current_thunk_name()); }

// strings handed to the game live in guest memory
uint32_t guest_const_str(const char* s) {
    static std::map<std::string, uint32_t> cache;
    std::map<std::string, uint32_t>::iterator it = cache.find(s);
    if (it != cache.end()) return it->second;
    uint32_t p = guest_malloc((uint32_t)strlen(s) + 1);
    memcpy(G(p), s, strlen(s) + 1);
    cache[s] = p;
    return p;
}

enum { GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02, GL_EXTENSIONS = 0x1F03, GL_SHADING_LANGUAGE_VERSION = 0x8B8C };

void glGetString_(X86* c) {
    uint32_t name = ARG(c, 0);
    const char* s = name == GL_VENDOR ? "isaac360" : name == GL_RENDERER ? "vulkan4xenon" : name == GL_VERSION ? "2.1"
                  : name == GL_SHADING_LANGUAGE_VERSION ? "1.20" : name == GL_EXTENSIONS ? "GL_ARB_framebuffer_object GL_EXT_framebuffer_object" : "";
    RET_STD(c, 1, guest_const_str(s));
}
void glGetError_(X86* c) { RET_STD(c, 0, 0); }

// WGL extensions the game looks up through wglGetProcAddress
void wglCreateContextAttribsARB_(X86* c) { RET_STD(c, 3, gl_create_context() ? 0x00030030 : 0); }
void wglSwapIntervalEXT_(X86* c) { RET_STD(c, 1, 1); }
void wglGetSwapIntervalEXT_(X86* c) { RET_STD(c, 0, 1); }
void wglGetExtensionsString_(X86* c) {
    RET_STD(c, 0, guest_const_str("WGL_ARB_create_context WGL_ARB_pixel_format WGL_EXT_swap_control"));
}
void wglGetExtensionsStringARB_(X86* c) {
    RET_STD(c, 1, guest_const_str("WGL_ARB_create_context WGL_ARB_pixel_format WGL_EXT_swap_control"));
}
// wglGetPixelFormatAttribivARB(hdc, format, layer, n, attributes*, values*): one RGBA8/D24S8 double-buffered format
uint32_t pixel_attribute(uint32_t a) {
    switch (a) {
    case 0x2000: return 1;                 // WGL_NUMBER_PIXEL_FORMATS_ARB
    case 0x2001: case 0x2010: case 0x2011: return 1;   // DRAW_TO_WINDOW, SUPPORT_OPENGL, DOUBLE_BUFFER
    case 0x2003: return 0x2027;            // WGL_ACCELERATION_ARB = WGL_FULL_ACCELERATION_ARB
    case 0x2013: return 0x202B;            // WGL_PIXEL_TYPE_ARB = WGL_TYPE_RGBA_ARB
    case 0x2014: return 32;                // WGL_COLOR_BITS_ARB
    case 0x2015: case 0x2017: case 0x2019: case 0x201B: return 8;   // RED/GREEN/BLUE/ALPHA_BITS
    case 0x2016: return 16;                // RED_SHIFT
    case 0x2018: return 8;                 // GREEN_SHIFT
    case 0x201C: return 24;                // ALPHA_SHIFT
    case 0x2022: return 24;                // WGL_DEPTH_BITS_ARB
    case 0x2023: return 8;                 // WGL_STENCIL_BITS_ARB
    default: return 0;                     // stereo, accum, aux, samples, sRGB...
    }
}
void wglGetPixelFormatAttribivARB_(X86* c) {
    uint32_t n = ARG(c, 3), attrs = ARG(c, 4), out = ARG(c, 5);
    for (uint32_t i = 0; i < n; ++i) WR32(out + 4 * i, pixel_attribute(RD32(attrs + 4 * i)));
    RET_STD(c, 6, 1);
}
void wglGetPixelFormatAttribfvARB_(X86* c) {
    uint32_t n = ARG(c, 3), attrs = ARG(c, 4), out = ARG(c, 5);
    for (uint32_t i = 0; i < n; ++i) WRF(out + 4 * i, (float)pixel_attribute(RD32(attrs + 4 * i)));
    RET_STD(c, 6, 1);
}
// wglChoosePixelFormatARB(hdc, iattribs, fattribs, max, formats*, count*)
void wglChoosePixelFormatARB_(X86* c) {
    if (ARG(c, 3) && ARG(c, 4)) WR32(ARG(c, 4), 1);
    if (ARG(c, 5)) WR32(ARG(c, 5), 1);
    RET_STD(c, 6, 1);
}
}


#if defined(_WIN32)
ShimFn gl_pt_shim(const std::string& name);
bool gl_pt_create_context();
void gl_pt_make_current();
void gl_pt_present();
#endif

// ISAAC360_TRACE_GL=1: count the GL calls the game makes (written to build/recomp/gl_calls.txt at each 600th frame)
// and the distinct values of the arguments that are not pointers (build/recomp/gl_args.txt), "-" skipping one
std::map<std::string, ShimFn> g_glReal;
std::map<std::string, uint64_t> g_glCounts;
std::map<std::string, std::set<std::string> > g_glArgs;
const char* gl_arg_mask(const std::string& n) {
    static std::map<std::string, const char*> masks;
    if (masks.empty()) {
        masks["glEnable"] = "x"; masks["glDisable"] = "x"; masks["glGetIntegerv"] = "x"; masks["glGetFloatv"] = "x";
        masks["glTexImage2D"] = "xxxdddxx-"; masks["glTexSubImage2D"] = "xxddddxx-"; masks["glTexParameteri"] = "xxx";
        masks["glReadPixels"] = "ddddxx-"; masks["glDrawElements"] = "xdx-"; masks["glDrawArrays"] = "xdd";
        masks["glVertexAttribPointer"] = "ddxdd-"; masks["glBlendFuncSeparate"] = "xxxx"; masks["glBlendFunc"] = "xx";
        masks["glCullFace"] = "x"; masks["glClear"] = "x"; masks["glBindFramebuffer"] = "xd"; masks["glFrontFace"] = "x";
        masks["glFramebufferTexture2D"] = "xxxdd"; masks["glRenderbufferStorage"] = "xxdd"; masks["glViewport"] = "dddd";
        masks["glGetRenderbufferParameteriv"] = "xx-"; masks["glAlphaFunc"] = "xf"; masks["glClampColorARB"] = "xx";
        masks["glDepthFunc"] = "x"; masks["glGetStringi"] = "xd"; masks["glGetString"] = "x"; masks["glActiveTexture"] = "x";
        masks["glFramebufferRenderbuffer"] = "xxxd"; masks["glCheckFramebufferStatus"] = "x"; masks["glClearColor"] = "ffff";
        masks["glGetProgramiv"] = "-x-"; masks["glGetShaderiv"] = "-x-"; masks["glCreateShader"] = "x"; masks["glClearDepth"] = "-";
        masks["glUniformMatrix4fv"] = "ddd-"; masks["glUniform1i"] = "dd"; masks["glBindRenderbuffer"] = "xd";
        masks["glBindTexture"] = "x-"; masks["glScissor"] = "dddd"; masks["glColorMask"] = "xxxx"; masks["glPixelStorei"] = "xd";
    }
    std::map<std::string, const char*>::iterator it = masks.find(n);
    return it == masks.end() ? "" : it->second;
}
void gl_counting(X86* c) {
    std::string n = current_thunk_name();
    ++g_glCounts[n];
    const char* m = gl_arg_mask(n);
    if (*m) {
        std::string s;
        char b[32];
        for (int i = 0; m[i]; ++i) {
            uint32_t v = ARG(c, i);
            float fv;
            memcpy(&fv, &v, 4);
            if (m[i] == 'x') sprintf(b, " 0x%x", v);
            else if (m[i] == 'd') sprintf(b, " %d", (int)v);
            else if (m[i] == 'f') sprintf(b, " %g", fv);
            else strcpy(b, " -");
            s += b;
        }
        std::set<std::string>& seen = g_glArgs[n];
        if (seen.size() < 64) seen.insert(s);
    }
    g_glReal[n](c);
}
void gl_write_counts() {
    FILE* f = fopen("build/recomp/gl_calls.txt", "w");
    if (!f) return;
    for (std::map<std::string, uint64_t>::iterator it = g_glCounts.begin(); it != g_glCounts.end(); ++it)
        fprintf(f, "%10llu %s\n", (unsigned long long)it->second, it->first.c_str());
    fclose(f);
    f = fopen("build/recomp/gl_args.txt", "w");
    if (!f) return;
    for (std::map<std::string, std::set<std::string> >::iterator it = g_glArgs.begin(); it != g_glArgs.end(); ++it)
        for (std::set<std::string>::iterator s = it->second.begin(); s != it->second.end(); ++s)
            fprintf(f, "%s%s\n", it->first.c_str(), s->c_str());
    fclose(f);
}

uint32_t gl_get_proc(const std::string& name) {
    std::map<std::string, uint32_t>::iterator t = g_glThunks.find(name);
    if (t != g_glThunks.end()) return t->second;
    std::map<std::string, ShimFn>::iterator it = g_gl.find(name);
    ShimFn fn = it != g_gl.end() ? it->second : gl_trap;
    if (gl_vk_enabled()) {
        if (ShimFn vk = gl_vk_shim(name)) fn = vk;      // the console's renderer (ISAAC360_GL=vk on the PC)
    }
#if defined(_WIN32)
    else if (ShimFn pt = gl_pt_shim(name)) fn = pt;     // the host driver (validation path)
#endif
    static bool count = getenv("ISAAC360_TRACE_GL") != 0;
    if (count && fn != gl_trap) {
        g_glReal[name] = fn;
        fn = gl_counting;
    }
    uint32_t a = thunk_for(fn, name.c_str());
    g_glThunks[name] = a;
    return a;
}

uint32_t gl_frame_count() { return g_frames; }
uint32_t (*g_hostFreeMemory)() = 0;                // the console's free memory in bytes (host_xsdk.cpp), for the log
uint32_t (*g_hostHeapBytes)() = 0;                 // the console's C heap and big blocks (host_xsdk.cpp)
// A quit the platform asked for (the console's Back + Start) happens here, at a frame's end with the GPU idle: the
// game thread never presents into a display that is closing (host_xsdk.cpp)
std::atomic<bool> g_quitRequested(false);
void (*g_hostQuit)() = 0;
static void log_line(const char* s) { recomp_log("%s", s); }

bool gl_create_context() {
    if (gl_vk_enabled()) return gl_vk_create_context();
#if defined(_WIN32)
    return gl_pt_create_context();
#else
    return true;
#endif
}
void gl_make_current() {
#if defined(_WIN32)
    if (!gl_vk_enabled()) gl_pt_make_current();
#endif
}

static void write_bmp(const std::string& path, const std::vector<uint8_t>& px, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    uint32_t size = 54 + (uint32_t)px.size();
    hdr[0] = 'B';
    hdr[1] = 'M';
    for (int i = 0; i < 4; ++i) {                    // little-endian fields on every host
        hdr[2 + i] = (uint8_t)(size >> (8 * i));
        hdr[18 + i] = (uint8_t)((uint32_t)w >> (8 * i));
        hdr[22 + i] = (uint8_t)((uint32_t)h >> (8 * i));   // bottom-up rows, as GL keeps them
    }
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    fwrite(&px[0], 1, px.size(), f);
    fclose(f);
    recomp_log("screenshot %s", path.c_str());
}

// ISAAC360_SHOTS="frame,frame,..." on the Vulkan path: the default framebuffer to <out>/shot_N.bmp, and with
// ISAAC360_SHOT_TARGETS each texture the game renders to as shot_N_tex<name>.bmp (recomp_out_dir)
void gl_vk_shot(uint32_t frame) {
    static std::set<uint32_t> shots;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        for (const char* p = getenv("ISAAC360_SHOTS"); p && *p;) {
            shots.insert((uint32_t)atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    if (!shots.count(frame)) return;
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    char name[64];
    if (gl_vk_read_frame(px, w, h)) {
        sprintf(name, "/shot_%05u.bmp", frame);
        write_bmp(recomp_out_dir() + name, px, w, h);
    }
    static bool targets = getenv("ISAAC360_SHOT_TARGETS") != 0;
    if (!targets) return;
    std::vector<uint32_t> names = gl_vk_targets();
    for (size_t i = 0; i < names.size(); ++i)
        if (gl_vk_read_texture(names[i], px, w, h)) {
            sprintf(name, "/shot_%05u_tex%u.bmp", frame, names[i]);
            write_bmp(recomp_out_dir() + name, px, w, h);
        }
}

void heap_profile_dump();                           // shims_crt.cpp

// Frame times for the log, each 600 frames: the game's part (from one present's end to the next one's start) and the
// present's (recording the frame's end, submitting, waiting for a free frame and the display), with what the GL layer
// waited for in them (the display, a frame's GPU work, read-backs)
struct FrameTimes {
    double game, present, frame, gameMax, presentMax, frameMax, slept;   // slept: the game's own Sleep calls
    uint32_t frames, over17, over34, slowLogged;
    GlVkWork work;                                  // at the block's start
};
static FrameTimes g_times;
typedef std::chrono::steady_clock Clock;

static void log_times() {
    FrameTimes& t = g_times;
    if (!t.frames) return;
    GlVkWork w;
    memset(&w, 0, sizeof(w));
    if (gl_vk_enabled()) gl_vk_work(&w);
    // the GPU's own work a frame (gl_vk.cpp's time stamps), the number to compare between builds whatever was played
    char gpu[96] = "";
    uint32_t timed = w.gpuFrames - t.work.gpuFrames;
    double gpuMax = gl_vk_enabled() ? gl_vk_gpu_max_reset() : 0;
    if (timed)
        snprintf(gpu, sizeof(gpu), "; the GPU's work %.1f ms a frame (max %.1f, %u frames timed)",
                 (w.gpuMs - t.work.gpuMs) / timed, gpuMax, timed);
    recomp_log("time: frame %.1f ms (max %.1f), game %.1f (max %.1f; %.1f of it the game's sleeps), present %.1f (max "
               "%.1f); %u of %u frames over "
               "17 ms, %u over 34; waited for the display %.1f, the GPU %.1f, read-backs %.1f ms a frame%s",
               t.frame / t.frames, t.frameMax, t.game / t.frames, t.gameMax, t.slept / t.frames, t.present / t.frames, t.presentMax, t.over17,
               t.frames, t.over34, (w.waitAcquire - t.work.waitAcquire) / t.frames, (w.waitFence - t.work.waitFence) / t.frames,
               (w.waitIdle - t.work.waitIdle) / t.frames, gpu);
    static bool profiling = getenv("ISAAC360_PROFILE") != 0;
    if (profiling && gl_vk_enabled()) gl_vk_gpu_trace_next();
    memset(&t, 0, sizeof(t));
    t.work = w;
}

// Each room the game enters (its log line, shims_crt.cpp) gets a line when the game leaves it: its first 30 frames
// (the door's or load's cost) apart from the rest, whose times, slow frames and GPU work are a room's steady cost to
// compare between builds. With ISAAC360_PROFILE the room's 60th steady frame gets a gpu trace (its draws one by one);
// ISAAC360_PROFILE=room logs the samples of rooms held 240 frames or more there.
struct RoomTimes {
    std::string name, counts;                       // counts: its 60th frame after the first 30's
    uint32_t frames, entryFrames, over17, over34;
    double entry, entryMax, game, present, frameMax, slept;
    GlVkWork work;                                  // after the first 30 frames
    GuestHeapStats heap;
    uint64_t enters;                                // the main thread's critical sections, after the first 30 frames
    uint64_t imageHits, imageMisses;                // hle_image.cpp's counts as the room began
    uint32_t traced;                                // the frame given a gpu trace (0: none)
    uint32_t anm2, spawn;                           // the game's ANM2 loading and spawn totals (ms) as it began,
                                                    // then their growth over the first 30 frames
};
static RoomTimes g_room;
static std::mutex g_roomLock;
static std::string g_roomNext;
static std::atomic<bool> g_roomPending(false);
static const uint32_t kRoomEntryFrames = 30;

void gl_room_entered(const char* name) {
    std::string s = name;
    if (!s.compare(0, 5, "Room ")) s.erase(0, 5);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    std::lock_guard<std::mutex> lock(g_roomLock);
    g_roomNext = s;
    g_roomPending = true;
}

static void log_room() {
    RoomTimes& r = g_room;
    if (r.name.empty()) return;
    char steady[1024] = "";
    if (r.frames) {
        GlVkWork w;
        memset(&w, 0, sizeof(w));
        if (gl_vk_enabled()) gl_vk_work(&w);
        uint32_t timed = w.gpuFrames - r.work.gpuFrames;
        GuestHeapStats h;
        guest_heap_stats(&h);
        uint64_t big = h.big - r.heap.big;
        char trace[48] = "";
        if (r.traced) snprintf(trace, sizeof(trace), ", gpu trace of frame %u", r.traced);
        snprintf(steady, sizeof(steady), "; then %u frames: game %.1f ms (%.1f of it the game's sleeps), present %.1f "
                 "(frame max %.1f), %u over 17 ms, %u over 34; the GPU's work %.1f ms a frame (%u timed%s); %s; %.1f "
                 "read-backs (%.2f waited), %.1f ms waiting for the GPU mid-frame, %.0f critical sections, %.0f "
                 "allocations a frame (%.1f over 2 KB, %.0f free runs looked at each)", r.frames, r.game / r.frames, r.slept / r.frames, r.present / r.frames, r.frameMax,
                 r.over17, r.over34, timed ? (w.gpuMs - r.work.gpuMs) / timed : 0.0, timed, trace, r.counts.c_str(),
                 (double)(w.readPixels - r.work.readPixels) / r.frames,
                 (double)(w.readPixelsWaited - r.work.readPixelsWaited) / r.frames,
                 (w.waitIdle - r.work.waitIdle) / r.frames,
                 (double)(shims_main_lock_enters() - r.enters) / r.frames, (double)(h.allocs - r.heap.allocs) / r.frames,
                 (double)big / r.frames, big ? (double)(h.scanned - r.heap.scanned) / big : 0.0);
    }
    uint64_t hits, misses, hitBytes;
    size_t cached;
    hle_image_stats(&hits, &misses, &hitBytes, &cached);
    recomp_log("room %s: first %u frames %.0f ms (slowest %.1f; the game's ANM2 loading %u ms, entity spawns %u ms), "
               "%llu images decoded and %llu from the cache (%u MB held)%s", r.name.c_str(), r.entryFrames, r.entry,
               r.entryMax, r.entryFrames == kRoomEntryFrames ? r.anm2 : RD32(0xbcb4ac) - r.anm2,
               r.entryFrames == kRoomEntryFrames ? r.spawn : RD32(0xbcc4d4) - r.spawn,
               (unsigned long long)(misses - r.imageMisses), (unsigned long long)(hits - r.imageHits),
               (unsigned)(cached >> 20), steady);
    if (recomp_profile_by_room() && r.frames >= 240) recomp_profile_dump(log_line);
}

// Audit scripts (scratch Lua run through every card, pill and active item) log "audit: <what>" before each thing
// they do: the frames until the next mark get a line with the game's work (its sleeps left out) and the GPU's, the
// average and the worst frame, to find effects that cost far more than a room's usual frame.
// With ISAAC360_FILL_STATS the line also gives the pixels the frames' draws covered (gl_vk.cpp add_fill), in
// millions: sprites with their effects, sprites drawn plain, everything else; average and the worst frame's sprites.
struct AuditWindow {
    std::string what;
    uint32_t frames, gpuFrames;
    double work, workMax, gpu, gpuMax;
    uint32_t workMaxFrame, gpuMaxFrame;
    uint32_t over3, over6;                          // frames of game work over 2.8 and 5.6 ms
    double fillSprite, fillPlain, fillOther, fillMax;   // pixels; fillMax: the most sprite pixels in a frame
    uint32_t fillMaxFrame;
    uint32_t pipes;                                 // pipelines made, and their ms
    double pipeMs;
    GlVkWork last;
};
static AuditWindow g_audit;
static bool g_auditOn = false;

static void log_audit() {
    if (!g_auditOn || !g_audit.frames) return;
    AuditWindow& a = g_audit;
    recomp_log("audit %s: %u frames; game work %.2f ms a frame, worst %.2f (frame %u), %u over 2.8 ms, %u over 5.6; the "
               "GPU's %.2f, worst %.2f (frame %u); fill %.2f + %.2f plain + %.2f other Mpx a frame, sprites (both) at "
               "most %.2f (frame %u); %u pipelines made in %.1f ms", a.what.c_str(), a.frames, a.work / a.frames,
               a.workMax, a.workMaxFrame, a.over3, a.over6, a.gpuFrames ? a.gpu / a.gpuFrames : 0.0, a.gpuMax,
               a.gpuMaxFrame, a.fillSprite / a.frames / 1e6, a.fillPlain / a.frames / 1e6, a.fillOther / a.frames / 1e6,
               a.fillMax / 1e6, a.fillMaxFrame, a.pipes, a.pipeMs);
}

void gl_audit_mark(const char* what) {
    log_audit();
    g_audit = AuditWindow();
    g_audit.what = what;
    while (!g_audit.what.empty() && (g_audit.what.back() == '\n' || g_audit.what.back() == '\r')) g_audit.what.pop_back();
    memset(&g_audit.last, 0, sizeof(g_audit.last));
    if (gl_vk_enabled()) gl_vk_work(&g_audit.last);
    g_auditOn = true;
}

static void audit_frame(double work) {
    if (!g_auditOn) return;
    AuditWindow& a = g_audit;
    ++a.frames;
    a.work += work;
    if (work > a.workMax) {
        a.workMax = work;
        a.workMaxFrame = g_frames;
    }
    if (work > 2.8) ++a.over3;
    if (work > 5.6) ++a.over6;
    if (!gl_vk_enabled()) return;
    GlVkWork w;
    gl_vk_work(&w);
    a.pipes += w.pipesMade - a.last.pipesMade;
    a.pipeMs += w.pipeMs - a.last.pipeMs;
    double sprite = w.fillSprite - a.last.fillSprite, plainFill = w.fillPlain - a.last.fillPlain;
    a.fillSprite += sprite;
    a.fillPlain += plainFill;
    a.fillOther += w.fillOther - a.last.fillOther;
    if (sprite + plainFill > a.fillMax) {
        a.fillMax = sprite + plainFill;
        a.fillMaxFrame = g_frames;
    }
    uint32_t n = w.gpuFrames - a.last.gpuFrames;
    if (n) {
        double ms = (w.gpuMs - a.last.gpuMs) / n;
        a.gpu += w.gpuMs - a.last.gpuMs;
        a.gpuFrames += n;
        if (ms > a.gpuMax) {
            a.gpuMax = ms;
            a.gpuMaxFrame = g_frames;
        }
    }
    a.last = w;
}

// the game is quitting (its exit()): the room it was in gets its line too
void gl_game_exiting() {
    log_room();
    g_room.name.clear();
    log_audit();
    g_auditOn = false;
}

static void room_frame(double ms, double game, double present, double slept) {
    RoomTimes& r = g_room;
    if (g_roomPending.load()) {
        log_room();
        std::string next;
        {
            std::lock_guard<std::mutex> lock(g_roomLock);
            next = g_roomNext;
            g_roomPending = false;
        }
        r = RoomTimes();
        r.name = next;
        r.anm2 = RD32(0xbcb4ac);
        r.spawn = RD32(0xbcc4d4);
        uint64_t hitBytes;
        size_t cached;
        hle_image_stats(&r.imageHits, &r.imageMisses, &hitBytes, &cached);
    }
    if (r.name.empty()) return;
    if (r.entryFrames < kRoomEntryFrames) {
        ++r.entryFrames;
        r.entry += ms;
        if (ms > r.entryMax) r.entryMax = ms;
        if (r.entryFrames == kRoomEntryFrames) {
            r.anm2 = RD32(0xbcb4ac) - r.anm2;
            r.spawn = RD32(0xbcc4d4) - r.spawn;
            if (gl_vk_enabled()) gl_vk_work(&r.work);
            guest_heap_stats(&r.heap);
            r.enters = shims_main_lock_enters();
            if (recomp_profile_by_room()) recomp_profile_clear();
        }
        return;
    }
    if (++r.frames == 60 && gl_vk_enabled()) {
        char counts[160];
        gl_vk_frame_counts(counts, sizeof(counts));
        r.counts = counts;
        static bool profiling = getenv("ISAAC360_PROFILE") != 0;
        if (profiling) {
            gl_vk_gpu_trace_next();
            r.traced = g_frames + 1;
        }
    }
    r.game += game;
    r.slept += slept;
    r.present += present;
    if (ms > r.frameMax) r.frameMax = ms;
    if (ms > 17.5) ++r.over17;
    if (ms > 34.0) ++r.over34;
}

// a frame over 50 ms (ISAAC360_SLOW_MS; up to 12 each 600 frames): what it did, to tell loading from waiting
static void log_slow_frame(double game, double present) {
    static uint64_t lastBytes = 0, lastDiskBytes = 0;
    static GlVkWork last;
    static double lastLocks = 0, lastDisk = 0;
    static bool profiling = getenv("ISAAC360_PROFILE") != 0;
    double diskMs = vf_disk_ms();
    uint64_t opens, bytes, disk;
    vf_stats(&opens, &bytes, &disk);
    GlVkWork w;
    memset(&w, 0, sizeof(w));
    if (gl_vk_enabled()) gl_vk_work(&w);
    double locks = profiling ? shims_lock_wait_ms() : 0;
    // the game's own totals (ms, by its timeGetTime): ANM2 loading (added at 0x408b9a) and entity spawns (0x6bb683),
    // printed at its shutdown as "Total ANM2 loading time" and "Total entity spawn time"
    static uint32_t lastAnm2 = 0, lastSpawn = 0;
    uint32_t anm2 = RD32(0xbcb4ac), spawn = RD32(0xbcc4d4);
    static double slowMs = getenv("ISAAC360_SLOW_MS") ? atof(getenv("ISAAC360_SLOW_MS")) : 50.0;
    if (profiling) recomp_profile_frame(game + present > slowMs && g_frames > 3);
    if (game + present > slowMs && g_frames > 3 && g_times.slowLogged < 12) {
        ++g_times.slowLogged;
        recomp_log("slow frame %u: game %.1f ms, present %.1f; read %llu KB (%llu KB from the drive in %.1f ms), %u images "
                   "made (%llu as an earlier load left them), %llu KB uploaded; textures %.1f ms for %llu KB, packed images "
                   "%.1f; waited for the GPU %.1f, read-backs %.1f, locks %.1f ms; the game's ANM2 loading %u ms, entity "
                   "spawns %u ms; %u pipelines made in %.1f ms", g_frames, game, present,
                   (unsigned long long)((bytes - lastBytes) >> 10),
                   (unsigned long long)((disk - lastDiskBytes) >> 10), diskMs - lastDisk, w.imagesMade - last.imagesMade,
                   (unsigned long long)(w.reused - last.reused), (unsigned long long)((w.uploaded - last.uploaded) >> 10),
                   w.texMs - last.texMs, (unsigned long long)((w.texBytes - last.texBytes) >> 10),
                   w.materializeMs - last.materializeMs, w.waitFence - last.waitFence, w.waitIdle - last.waitIdle,
                   locks - lastLocks, anm2 - lastAnm2, spawn - lastSpawn, w.pipesMade - last.pipesMade,
                   w.pipeMs - last.pipeMs);
    }
    lastAnm2 = anm2;
    lastSpawn = spawn;
    lastBytes = bytes;
    lastDiskBytes = disk;
    lastDisk = diskMs;
    last = w;
    lastLocks = locks;
}

// ---- the run's loading screen kept moving
// From Continue or a new run the game shows a drawing (F_89af70 loads its two pictures, gfx/ui/loadimages/
// loadimages-NNN.png and its _2, into the manager at +0x4A364 and +0x4A36C: texture objects that hold their GL names
// at +0x78) and alternates them every 8 frames by the manager's frame counter (+0x4A230, which a frame draws with as
// the present finds it; F_89b260: the second picture while the count mod 16 is 8 or more), for at least 3 s (F_6b81f0:
// 0xBB8 ms by timeGetTime from +0x4A374) and until the run is loaded. The load runs in one frame inside it: 0.15-0.4 s
// on the PC, 2.3 s on the console (boot 21), the drawing still all that time. While the pictures are loaded the GL
// layer keeps each frame's draws (gl_vk_record_frames); once a frame has run 50 ms, the main thread's chores (between
// its lock operations and in its clock reads: guest_lock.h) draw the last frame again with the picture the counter
// would have reached by then, each time it changes, and present it (gl_vk_replay). The game's memory is left alone:
// after the load its counter goes on from where it was. ISAAC360_LOAD_FRAMES=0 leaves the drawing still;
// ISAAC360_LOAD_STALL_MS=N draws a frame of the screen that runs over 100 ms out by N ms, waits of N/20 ms in
// 20 of those chores (the console's load on the PC); ISAAC360_LOAD_SHOTS=1 writes each frame drawn again as load_F_N.bmp.
namespace {
const uint32_t kManager = 0x00BC9B0Cu;
enum { kLoadPictures = 0x4A364, kFrameCounter = 0x4A230, kTextureName = 0x78 };
struct LoadScreen {
    bool armed, failed;
    uint32_t picture[2];                            // GL names (0: none)
    uint32_t counter;                               // the manager's frame counter the last frame drew with
    int shown;                                      // the picture on the screen
    Clock::time_point presented, lastChance;        // the last present's end, the last chore's clock read
    uint32_t calls, chances, injected, stalls;
    double longestGap;
};
LoadScreen g_load;

uint32_t load_picture(uint32_t manager, int i) {
    uint32_t obj = RD32(manager + kLoadPictures + 8 * i);
    uint32_t name = obj ? RD32(obj + kTextureName) : 0;
    return gl_vk_texture_exists(name) ? name : 0;
}

void load_frames_chore() {
    LoadScreen& s = g_load;
    if (++s.calls & 31) return;                     // the clock at every 32nd chance
    Clock::time_point now = Clock::now();
    double gap = std::chrono::duration<double, std::milli>(now - s.lastChance).count();
    if (gap > s.longestGap) s.longestGap = gap;
    s.lastChance = now;
    ++s.chances;
    double ran = std::chrono::duration<double, std::milli>(now - s.presented).count();
    static const int stallMs = getenv("ISAAC360_LOAD_STALL_MS") ? atoi(getenv("ISAAC360_LOAD_STALL_MS")) : 0;
    if (stallMs > 0 && ran > 100.0 && s.stalls < 20) {
        ++s.stalls;
        std::this_thread::sleep_for(std::chrono::milliseconds(stallMs / 20));
    }
    if (ran < 50.0 || s.failed) return;
    uint32_t k = (uint32_t)(ran * 0.06);            // display frames since the last present
    int want = s.picture[1] && ((s.counter + k) & 15) >= 8 ? 1 : 0;
    if (want == s.shown) return;
    uint32_t to[2] = {s.picture[want], s.picture[want]};
    guest_lock_main_park();                         // a present may wait for the display: others may take locks
    bool ok = gl_vk_replay(s.picture, to, 2);
    guest_lock_main_unpark();
    if (!ok) {
        s.failed = true;                            // not again in this frame (gl_vk logs why)
        return;
    }
    s.shown = want;
    ++s.injected;
    static const bool shots = getenv("ISAAC360_LOAD_SHOTS") != 0;
    if (shots) {
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        char name[64];
        snprintf(name, sizeof(name), "/load_%05u_%02u.bmp", g_frames, s.injected);
        if (gl_vk_read_frame(px, w, h)) write_bmp(recomp_out_dir() + name, px, w, h);
    }
}

// after each present: the screen up (its pictures loaded) arms the chore for the next frame
void load_frames_present() {
    static const bool replay = !(getenv("ISAAC360_LOAD_FRAMES") && !atoi(getenv("ISAAC360_LOAD_FRAMES")));
    static const bool stall = getenv("ISAAC360_LOAD_STALL_MS") != 0;
    LoadScreen& s = g_load;
    Clock::time_point now = Clock::now();
    double ran = std::chrono::duration<double, std::milli>(now - s.presented).count();
    if (s.armed && (s.injected || ran > 100.0))
        recomp_log("load frames: frame %u ran %.0f ms, the loading screen presented %u times in it (the main thread's "
                   "chores read the clock %u times, at most %.1f ms apart)", g_frames, ran, s.injected, s.chances,
                   s.longestGap);
    uint32_t m = replay || stall ? RD32(kManager) : 0;
    uint32_t a = m ? load_picture(m, 0) : 0;
    gl_vk_record_frames(a != 0 && replay);
    if (!a) {
        if (s.armed) guest_main_frames_chore(0);
        s.armed = false;
        return;
    }
    s.picture[0] = a;
    s.picture[1] = load_picture(m, 1);
    s.counter = RD32(m + kFrameCounter);
    s.shown = gl_vk_last_frame_samples(s.picture[1]) ? 1 : gl_vk_last_frame_samples(a) ? 0 : -1;
    s.failed = !replay || s.shown < 0;              // a frame without the drawing (or not recorded yet) stays as it is
    s.presented = s.lastChance = now;
    s.calls = s.chances = s.injected = s.stalls = 0;
    s.longestGap = 0;
    if (!s.armed) guest_main_frames_chore(load_frames_chore);
    s.armed = true;
}
}  // namespace

void gl_present() {
    static Clock::time_point lastEnd = Clock::now();
    Clock::time_point start = Clock::now();
    static uint32_t profileFrame = getenv("ISAAC360_HEAP_PROFILE") ? (uint32_t)atoi(getenv("ISAAC360_HEAP_PROFILE")) : 0;
    if (profileFrame && g_frames == profileFrame) heap_profile_dump();
    // ISAAC360_TEX_LIST="frame,frame,...": the live textures at those frames (build/recomp/textures_N.txt)
    static std::set<uint32_t> texFrames;
    static bool texParsed = false;
    if (!texParsed) {
        texParsed = true;
        for (const char* p = getenv("ISAAC360_TEX_LIST"); p && *p;) {
            texFrames.insert((uint32_t)atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    if (texFrames.count(g_frames) && gl_vk_enabled()) gl_vk_write_texture_list();
    if (gl_vk_enabled()) {
        gl_vk_shot(g_frames);
        gl_vk_present();
        load_frames_present();
    }
#if defined(_WIN32)
    else gl_pt_present();
#endif
    Clock::time_point end = Clock::now();
    {
        double game = std::chrono::duration<double, std::milli>(start - lastEnd).count();
        double present = std::chrono::duration<double, std::milli>(end - start).count();
        static double sleptBefore = 0;
        double sleptNow = shims_main_sleep_ms(), slept = sleptNow - sleptBefore;
        sleptBefore = sleptNow;
        FrameTimes& t = g_times;
        t.game += game;
        t.slept += slept;
        t.present += present;
        t.frame += game + present;
        if (game > t.gameMax) t.gameMax = game;
        if (present > t.presentMax) t.presentMax = present;
        if (game + present > t.frameMax) t.frameMax = game + present;
        if (game + present > 17.5) ++t.over17;
        if (game + present > 34.0) ++t.over34;
        ++t.frames;
        // ISAAC360_STALL_MS=N: a frame the PC finds slow (a load, a death, 25 ms of the game's part) waits N ms more,
        // so the console's long load frames and what they do to streams and timers show on the PC
        static const int stallMs = getenv("ISAAC360_STALL_MS") ? atoi(getenv("ISAAC360_STALL_MS")) : 0;
        if (stallMs > 0 && game - slept > 25.0 && g_frames > 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stallMs));
            end = Clock::now();
        }
        lastEnd = end;
        log_slow_frame(game, present);
        room_frame(game + present, game, present, slept);
        audit_frame(game - slept);
    }
    ++g_frames;
    if (g_quitRequested.load()) {
        if (gl_vk_enabled()) gl_vk_wait_idle();
        log_room();
        recomp_log("quitting at the end of frame %u", g_frames);
        if (g_hostQuit) g_hostQuit();
    }
    // archive blocks past the budget or unread for a minute go; all but this second's when the console runs short
    if (g_frames % 60 == 0) {
        uint32_t free = g_hostFreeMemory ? g_hostFreeMemory() : ~0u;
        vf_trim(free < (48u << 20) ? 1 : 60);
        if (free < (32u << 20)) hle_image_trim_low();     // as gl_vk gives idle images back
    }
    if (g_frames <= 3 || g_frames % 600 == 0) {
        // seconds since the first frame: loading shows as the gap between frames 1 and 2
        static std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        char free[32] = "";
        if (g_hostFreeMemory) sprintf(free, ", free %u MB", g_hostFreeMemory() >> 20);
        // where memory goes: the guest heap's high mark (committed on the console), the host's C heap, the archive
        // readers' cached blocks, the GL layer's buffers
        char mem[128];
        snprintf(mem, sizeof(mem), "; heap top %u MB, host heap %u MB, read cache %u MB, GL buffers %u MB",
                 (guest_heap_top() - kGuestHeapLow) >> 20, g_hostHeapBytes ? g_hostHeapBytes() >> 20 : 0,
                 vf_cached_bytes() >> 20, (unsigned)(gl_vk_buffer_bytes() >> 20));
        char counts[160] = "";
        if (gl_vk_enabled()) {
            counts[0] = ';';
            counts[1] = ' ';
            gl_vk_frame_counts(counts + 2, sizeof(counts) - 2);
        }
        recomp_log("frame %u at %.1f s, heap %u MB, images %u MB (+%u MB packed)%s%s%s", g_frames, t,
                   guest_heap_used() >> 20, (unsigned)(gl_vk_image_bytes() >> 20), (unsigned)(gl_vk_packed_bytes() >> 20),
                   free, counts, mem);
        static bool traceReads = getenv("ISAAC360_TRACE_READS") != 0;
        if (traceReads) vf_dump_readers(log_line);
        if (!g_glCounts.empty()) gl_write_counts();
        if (gl_vk_enabled()) gl_vk_write_stats();
        if (g_frames % 600 == 0) {
            recomp_count_dump(600, log_line);
            log_times();
            if (!recomp_profile_by_room()) recomp_profile_dump(log_line);
            static bool profiling = getenv("ISAAC360_PROFILE") != 0;
            if (profiling) shims_lock_stats_dump(log_line);
        }
    }
}

void shims_gl_register() {
    g_gl["wglCreateContextAttribsARB"] = wglCreateContextAttribsARB_;
    g_gl["wglSwapIntervalEXT"] = wglSwapIntervalEXT_;
    g_gl["wglGetSwapIntervalEXT"] = wglGetSwapIntervalEXT_;
    g_gl["wglGetExtensionsStringEXT"] = wglGetExtensionsString_;
    g_gl["wglGetExtensionsStringARB"] = wglGetExtensionsStringARB_;
    g_gl["wglChoosePixelFormatARB"] = wglChoosePixelFormatARB_;
    g_gl["wglGetPixelFormatAttribivARB"] = wglGetPixelFormatAttribivARB_;
    g_gl["wglGetPixelFormatAttribfvARB"] = wglGetPixelFormatAttribfvARB_;
    g_gl["glGetString"] = glGetString_;
    g_gl["glGetError"] = glGetError_;
}
