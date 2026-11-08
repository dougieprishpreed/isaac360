// OpenGL for the recompiled game (docs/RECOMP_PLAN.md, phase 3). The game loads every entry point by name
// (glad-style: GL 1.1 through GetProcAddress on opengl32.dll, the rest through wglGetProcAddress). Each name gets
// a thunk: implemented ones run the GL state machine here (drawn through the shared Vulkan renderer), the rest
// stop with their name the first time the game calls them, so the set grows with what the game really uses.
#include "runtime.h"
#include "gl_vk.h"
#include "vfile.h"
#include <chrono>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
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
    const char* s = name == GL_VENDOR ? "boi360" : name == GL_RENDERER ? "vulkan4xenon" : name == GL_VERSION ? "2.1"
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

// BOI360_TRACE_GL=1: count the GL calls the game makes (written to build/recomp/gl_calls.txt at each 600th frame)
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
        if (ShimFn vk = gl_vk_shim(name)) fn = vk;      // the console's renderer (BOI360_GL=vk on the PC)
    }
#if defined(_WIN32)
    else if (ShimFn pt = gl_pt_shim(name)) fn = pt;     // the host driver (validation path)
#endif
    static bool count = getenv("BOI360_TRACE_GL") != 0;
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

// BOI360_SHOTS="frame,frame,..." on the Vulkan path: the default framebuffer to build/recomp/shot_N.bmp
void gl_vk_shot(uint32_t frame) {
    static std::set<uint32_t> shots;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        for (const char* p = getenv("BOI360_SHOTS"); p && *p;) {
            shots.insert((uint32_t)atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    if (!shots.count(frame)) return;
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!gl_vk_read_frame(px, w, h)) return;
    char path[256];
    sprintf(path, "build/recomp/shot_%05u.bmp", frame);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    uint32_t size = 54 + (uint32_t)px.size();
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);                         // bottom-up rows, as GL keeps them
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    fwrite(&px[0], 1, px.size(), f);
    fclose(f);
    recomp_log("screenshot %s", path);
}

void heap_profile_dump();                           // shims_crt.cpp

void gl_present() {
    static uint32_t profileFrame = getenv("BOI360_HEAP_PROFILE") ? (uint32_t)atoi(getenv("BOI360_HEAP_PROFILE")) : 0;
    if (profileFrame && g_frames == profileFrame) heap_profile_dump();
    // BOI360_TEX_LIST="frame,frame,...": the live textures at those frames (build/recomp/textures_N.txt)
    static std::set<uint32_t> texFrames;
    static bool texParsed = false;
    if (!texParsed) {
        texParsed = true;
        for (const char* p = getenv("BOI360_TEX_LIST"); p && *p;) {
            texFrames.insert((uint32_t)atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    if (texFrames.count(g_frames) && gl_vk_enabled()) gl_vk_write_texture_list();
    if (gl_vk_enabled()) {
        gl_vk_shot(g_frames);
        gl_vk_present();
    }
#if defined(_WIN32)
    else gl_pt_present();
#endif
    ++g_frames;
    if (g_frames <= 3 || g_frames % 600 == 0) {
        // seconds since the first frame: loading shows as the gap between frames 1 and 2
        static std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        char free[32] = "";
        if (g_hostFreeMemory) sprintf(free, ", free %u MB", g_hostFreeMemory() >> 20);
        recomp_log("frame %u at %.1f s, heap %u MB, images %u MB (+%u MB packed)%s", g_frames, t,
                   guest_heap_used() >> 20, (unsigned)(gl_vk_image_bytes() >> 20), (unsigned)(gl_vk_packed_bytes() >> 20),
                   free);
        static bool traceReads = getenv("BOI360_TRACE_READS") != 0;
        if (traceReads && g_frames <= 3) vf_dump_readers(log_line);
        if (!g_glCounts.empty()) gl_write_counts();
        if (gl_vk_enabled()) gl_vk_write_stats();
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
