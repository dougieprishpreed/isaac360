// Windows only: the game's OpenGL calls passed straight to the host driver (generated thunks, gl_passthrough.inc),
// to validate the recompiled game's rendering before the console's GL-on-Vulkan layer (docs/RECOMP_PLAN.md).
// The context lives on a window of ours that is placed off-screen and shown without activation, so test runs
// never take the keyboard focus. ISAAC360_SHOTS="frame,frame,..." saves those frames to build/recomp/shot_N.bmp.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <GL/gl.h>
typedef int8_t khronos_int8_t;
typedef uint8_t khronos_uint8_t;
typedef int16_t khronos_int16_t;
typedef uint16_t khronos_uint16_t;
typedef int32_t khronos_int32_t;
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
typedef float khronos_float_t;
typedef intptr_t khronos_intptr_t;
typedef intptr_t khronos_ssize_t;
#include "gl_types.inc"

bool gl_pt_create_context_impl();

namespace {
HWND g_wnd = 0;
HDC g_dc = 0;
HGLRC g_rc = 0;
HMODULE g_gl32 = 0;
std::map<std::string, void*> g_procs;
uint32_t g_boundArray = 0, g_boundElement = 0, g_boundUnpack = 0, g_boundPack = 0;
std::set<int> g_shots;
int g_frame = 0;

void* gl_host(const char* name) {
    if (!g_rc) gl_pt_create_context_impl();
    std::map<std::string, void*>::iterator it = g_procs.find(name);
    if (it != g_procs.end()) return it->second;
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) p = (void*)GetProcAddress(g_gl32, name);
    if (!p) recomp_log("GL host function %s is missing", name);
    g_procs[name] = p;
    return p;
}

void* gl_ptr(uint32_t a) { return a ? (void*)(g_mem + a) : 0; }
void* gl_ptr_or_offset(uint32_t a, uint32_t binding) {
    uint32_t bound = binding == 0x8894 ? g_boundArray : binding == 0x8895 ? g_boundElement : binding == 0x88EF ? g_boundUnpack : g_boundPack;
    return bound ? (void*)(uintptr_t)a : gl_ptr(a);
}
std::map<uint32_t, GLsync> g_syncs;
void* gl_sync_handle(uint32_t h) { return (void*)g_syncs[h]; }
// strings the driver returns are copied into guest memory once per distinct string
uint32_t gl_guest_ptr(const void* p, const char* fn) {
    if (!p) return 0;
    static std::map<std::string, uint32_t> cache;
    std::string s((const char*)p);
    std::map<std::string, uint32_t>::iterator it = cache.find(s);
    if (it != cache.end()) return it->second;
    uint32_t g = guest_malloc((uint32_t)s.size() + 1);
    memcpy(g_mem + g, s.c_str(), s.size() + 1);
    cache[s] = g;
    (void)fn;
    return g;
}

#define GL_PT(name, body) void pt_##name(X86* c) body
#include "gl_passthrough.inc"
#undef GL_PT

// ISAAC360_DUMP_SHADERS: which dumped sources each program links (build/recomp/shaders/programs.txt)
std::map<uint32_t, int> g_shaderDump;
std::map<uint32_t, std::vector<int> > g_programShaders;
void attach_shader(X86* c) {
    g_programShaders[ARG(c, 0)].push_back(g_shaderDump.count(ARG(c, 1)) ? g_shaderDump[ARG(c, 1)] : -1);
    pt_glAttachShader(c);
}
void link_program(X86* c) {
    if (getenv("ISAAC360_DUMP_SHADERS")) {
        FILE* f = fopen("build/recomp/shaders/programs.txt", "a");
        if (f) {
            std::vector<int>& s = g_programShaders[ARG(c, 0)];
            for (size_t i = 0; i < s.size(); ++i) fprintf(f, "%s%03d", i ? " " : "", s[i]);
            fprintf(f, "\n");
            fclose(f);
        }
    }
    pt_glLinkProgram(c);
}

// overrides: buffer bindings are tracked for the offset rule; glShaderSource takes an array of guest pointers
void bind_buffer(X86* c) {
    uint32_t target = ARG(c, 0), b = ARG(c, 1);
    if (target == 0x8892) g_boundArray = b;            // GL_ARRAY_BUFFER
    else if (target == 0x8893) g_boundElement = b;     // GL_ELEMENT_ARRAY_BUFFER
    else if (target == 0x88EC) g_boundUnpack = b;      // GL_PIXEL_UNPACK_BUFFER
    else if (target == 0x88EB) g_boundPack = b;        // GL_PIXEL_PACK_BUFFER
    pt_glBindBuffer(c);
}
void shader_source(X86* c) {
    uint32_t shader = ARG(c, 0), count = ARG(c, 1), strs = ARG(c, 2), lens = ARG(c, 3);
    std::vector<std::string> src(count);
    std::vector<const char*> ptrs(count);
    std::vector<GLint> ln(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t s = RD32(strs + 4 * i);
        int32_t l = lens ? (int32_t)RD32(lens + 4 * i) : -1;
        src[i] = l < 0 ? guest_str(s) : std::string((const char*)(g_mem + s), (size_t)l);
        ptrs[i] = src[i].c_str();
        ln[i] = (GLint)src[i].size();
    }
    if (getenv("ISAAC360_DUMP_SHADERS")) {
        // each source the game compiles, for the console's offline shader conversion (tools/recomp/shaders.py)
        static int n = 0;
        CreateDirectoryA("build/recomp/shaders", 0);
        char path[128];
        g_shaderDump[shader] = n;
        sprintf(path, "build/recomp/shaders/%03d.glsl", n++);
        FILE* f = fopen(path, "wb");
        if (f) {
            for (uint32_t i = 0; i < count; ++i) fwrite(src[i].data(), 1, src[i].size(), f);
            fclose(f);
        }
    }
    ((void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*))gl_host("glShaderSource"))(shader, (GLsizei)count, &ptrs[0], &ln[0]);
    RET_STD(c, 4, 0);
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }
}

void* gl_pt_lookup(const std::string& name);

bool gl_pt_create_context() { return gl_pt_create_context_impl(); }

bool gl_pt_create_context_impl() {
    if (g_rc) return true;
    g_gl32 = LoadLibraryA("opengl32.dll");
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "isaac360recomp";
    RegisterClassA(&wc);
    bool hidden = getenv("ISAAC360_HIDDEN") != 0;
    g_wnd = CreateWindowExA(hidden ? WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE : 0, "isaac360recomp", "The Binding of Isaac: Rebirth",
                            hidden ? WS_POPUP : WS_OVERLAPPEDWINDOW, hidden ? -32000 : CW_USEDEFAULT, hidden ? -32000 : CW_USEDEFAULT,
                            1280, 720, 0, 0, wc.hInstance, 0);
    if (!g_wnd) return false;
    // shown, but off-screen and never activated when hidden: GL keeps its default framebuffer, focus stays put
    ShowWindow(g_wnd, hidden ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
    g_dc = GetDC(g_wnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    SetPixelFormat(g_dc, pf, &pfd);
    g_rc = wglCreateContext(g_dc);
    if (!g_rc || !wglMakeCurrent(g_dc, g_rc)) return false;
    const char* shots = getenv("ISAAC360_SHOTS");
    for (const char* p = shots; p && *p;) {
        g_shots.insert(atoi(p));
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    recomp_log("GL context: %s, %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    return true;
}

void gl_pt_make_current() { if (g_rc) wglMakeCurrent(g_dc, g_rc); }

void gl_pt_present() {
    if (!g_rc) return;
    if (g_shots.count(g_frame)) {
        std::vector<uint8_t> px(1280 * 720 * 4);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, 1280, 720, GL_BGRA_EXT, GL_UNSIGNED_BYTE, &px[0]);
        char path[256];
        sprintf(path, "build/recomp/shot_%05d.bmp", g_frame);
        FILE* f = fopen(path, "wb");
        if (f) {
            BITMAPFILEHEADER bf;
            BITMAPINFOHEADER bi;
            memset(&bf, 0, sizeof(bf));
            memset(&bi, 0, sizeof(bi));
            bf.bfType = 0x4d42;
            bf.bfOffBits = sizeof(bf) + sizeof(bi);
            bf.bfSize = bf.bfOffBits + (DWORD)px.size();
            bi.biSize = sizeof(bi);
            bi.biWidth = 1280;
            bi.biHeight = 720;                              // bottom-up, as glReadPixels returns rows
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            fwrite(&bf, sizeof(bf), 1, f);
            fwrite(&bi, sizeof(bi), 1, f);
            fwrite(&px[0], 1, px.size(), f);
            fclose(f);
            recomp_log("screenshot %s", path);
        }
    }
    ++g_frame;
    SwapBuffers(g_dc);
    MSG msg;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

ShimFn gl_pt_shim(const std::string& name) {
    static std::map<std::string, ShimFn> table;
    if (table.empty()) {
#define GL_PT(name, body) table[#name] = pt_##name;
#include "gl_passthrough.inc"
#undef GL_PT
        table["glBindBuffer"] = bind_buffer;
        table["glBindBufferARB"] = bind_buffer;
        table["glShaderSource"] = shader_source;
        table["glAttachShader"] = attach_shader;
        table["glLinkProgram"] = link_program;
        table["glShaderSourceARB"] = shader_source;
    }
    std::map<std::string, ShimFn>::iterator it = table.find(name);
    return it == table.end() ? 0 : it->second;
}
#endif
