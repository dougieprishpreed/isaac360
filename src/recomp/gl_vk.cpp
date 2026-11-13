// The game's OpenGL on Vulkan (gl_vk.h, docs/RECOMP_PLAN.md "GL on vulkan4xenon"). The game's GL use is small
// and fixed (measured with ISAAC360_TRACE_GL): client-side float arrays drawn with glDrawElements(GL_TRIANGLES,
// u16), RGB/RGBA8 textures, one framebuffer object whose colour texture changes and whose depth is a 1024x1024
// renderbuffer, 1x1 glReadPixels, blending, culling, a depth test and the alpha test switched on once.
//
// * Everything is drawn in GL's own row order: row 0 of every image is GL's bottom row, the viewport and
//   gl_FragCoord are GL's numbers, render-to-texture lands as GL samples it. Only the present pass flips.
//   Faces: with y not flipped, GL's counter-clockwise is Vulkan's clockwise.
// * Shaders were converted offline (tools/recomp/shaders.py); a program is found by its sources' hashes.
// * vulkan4xenon's rules (crab360, Spire360): a pass whose first act is a full clear begins DONT_CARE and
//   clears with vkCmdClearAttachments; matrices stay block members (push constants, 128 bytes); staging
//   buffers are only copied from and prefer cached memory.
// * Vertex data and indices are copied out of guest memory for each draw, in host byte order.
#include "gl_vk.h"
#include "lz.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <map>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "gl_shaders.inc"

void gl_window_size(int& w, int& h);                // shims_user32.cpp: the game's window
uint32_t gl_frame_count();                          // shims_gl.cpp
extern uint32_t (*g_hostFreeMemory)();               // shims_gl.cpp (the console's free memory)

namespace {

#define GLVK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceLayerProperties)
#define GLVK_INSTANCE(X)                                                                                          \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                           \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice)          \
    X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceFormatProperties) X(vkDestroySurfaceKHR)                          \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                          \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)
#define GLVK_DEVICE(X)                                                                                            \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkCreateBuffer) X(vkDestroyBuffer)                       \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage)                     \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)              \
    X(vkCreateSampler) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout)                \
    X(vkCreateGraphicsPipelines) X(vkCreateRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer)            \
    X(vkCreateDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkResetDescriptorPool)                            \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkAllocateCommandBuffers)     \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkWaitForFences)    \
    X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)   \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer)            \
    X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw) X(vkCmdDrawIndexed)                \
    X(vkCmdClearAttachments) X(vkCmdClearColorImage) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer)         \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)         \
    X(vkQueuePresentKHR) X(vkCreateQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp)                      \
    X(vkGetQueryPoolResults)

#define GLVK_DECLARE(name) PFN_##name name = 0;
GLVK_GLOBAL(GLVK_DECLARE)
GLVK_INSTANCE(GLVK_DECLARE)
GLVK_DEVICE(GLVK_DECLARE)

enum {
    FRAMES = 2,
    VERTEX_RING = 512 << 10,            // a frame of play uses ~22 KB of vertices and 1 KB of indices (the frame log's
    INDEX_RING = 128 << 10,             // "rings at most"); a full ring waits for the GPU and starts again
    UPLOAD_RING = 4 << 20,              // larger uploads get a buffer of their own
    PIXEL_READS = 256,                  // single pixels glReadPixels may queue a frame
    MAX_SETS = 2048,
    SAMPLER_SLOTS = 4,                  // set 0 bindings 0..3 (the game's programs use at most two)
    PUSH_BYTES = 128,
    MAX_ATTRIBS = 16,
    MAX_UNITS = 16,
    MAX_SWAP = 4,
    COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM,
    OUTPUT_FORMAT = VK_FORMAT_B8G8R8A8_UNORM,
};

// GL enums the layer reads
enum {
    GL_TRIANGLES = 0x0004, GL_TRIANGLE_STRIP = 0x0005, GL_TRIANGLE_FAN = 0x0006,
    GL_UNSIGNED_BYTE = 0x1401, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406,
    GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_BGRA = 0x80E1, GL_ALPHA = 0x1906, GL_LUMINANCE = 0x1909, GL_LUMINANCE_ALPHA = 0x190A,
    GL_CULL_FACE = 0x0B44, GL_DEPTH_TEST = 0x0B71, GL_ALPHA_TEST = 0x0BC0, GL_BLEND = 0x0BE2, GL_SCISSOR_TEST = 0x0C11,
    GL_STENCIL_TEST = 0x0B90,
    GL_FRONT = 0x0404, GL_BACK = 0x0405, GL_FRONT_AND_BACK = 0x0408, GL_CW = 0x0900, GL_CCW = 0x0901,
    GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
    GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812F, GL_MIRRORED_REPEAT = 0x8370,
    GL_TEXTURE0 = 0x84C0,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00,
    GL_STENCIL_ATTACHMENT = 0x8D20, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_RENDERBUFFER_WIDTH = 0x8D42, GL_RENDERBUFFER_HEIGHT = 0x8D43, GL_RENDERBUFFER_INTERNAL_FORMAT = 0x8D44,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84, GL_SHADER_TYPE = 0x8B4F,
    GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30,
    GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02, GL_EXTENSIONS = 0x1F03, GL_SHADING_LANGUAGE_VERSION = 0x8B8C,
    GL_NUM_EXTENSIONS = 0x821D, GL_CONTEXT_FLAGS = 0x821E, GL_RESET_NOTIFICATION_STRATEGY = 0x8256,
    GL_NO_RESET_NOTIFICATION = 0x8261, GL_FRAMEBUFFER_BINDING = 0x8CA6, GL_CONTEXT_PROFILE_MASK = 0x9126,
    GL_MAX_TEXTURE_SIZE = 0x0D33, GL_VIEWPORT = 0x0BA2, GL_MAJOR_VERSION = 0x821B, GL_MINOR_VERSION = 0x821C,
    GL_MAX_VERTEX_ATTRIBS = 0x8869, GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D,
    GL_TEXTURE_BINDING_2D = 0x8069, GL_CURRENT_PROGRAM = 0x8B8D, GL_ACTIVE_TEXTURE = 0x84E0, GL_UNPACK_ALIGNMENT = 0x0CF5,
    GL_PACK_ALIGNMENT = 0x0D05, GL_MAX_RENDERBUFFER_SIZE = 0x84E8, GL_MAX_VIEWPORT_DIMS = 0x0D3A,
    GL_GREATER = 0x0204, GL_LESS = 0x0201,
};

// what the game is told: the pass-through run's compatibility profile, and the extensions its paths look for
const char* const kExtensions[] = {
    "GL_ARB_color_buffer_float", "GL_ARB_compatibility", "GL_ARB_depth_texture", "GL_ARB_fragment_shader",
    "GL_ARB_framebuffer_object", "GL_ARB_multitexture", "GL_ARB_shader_objects", "GL_ARB_shading_language_100",
    "GL_ARB_texture_non_power_of_two", "GL_ARB_vertex_buffer_object", "GL_ARB_vertex_shader",
    "GL_EXT_blend_func_separate", "GL_EXT_framebuffer_object", "GL_EXT_packed_depth_stencil", "GL_EXT_bgra",
};
const int kExtensionCount = (int)(sizeof(kExtensions) / sizeof(kExtensions[0]));

struct Buffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    uint8_t* map;
    VkDeviceSize size, bytes;           // asked, and the memory it took
};

struct Image {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    int w, h;
    VkDeviceSize bytes;
    int drawnW, drawnH;                 // a colour target: the area its draws' viewports reach (0: not seen yet)
};

struct Garbage {
    Image image;
    Buffer buffer;
    VkFramebuffer fb;
};

struct Texture {
    Image img;
    uint32_t minFilter, magFilter, wrapS, wrapT;
    bool target, data;                  // rendered to / given pixels (texture list)
    uint32_t uploadFrame, format;
    uint32_t drawFrame, draws;          // the last frame a draw sampled it, how many draws did (texture list)
    uint32_t packed;                    // its pixels' size compressed (lz.h)
    // A texture the game gave pixels keeps them compressed (lz.h) and gets its GPU image only when a draw samples
    // it: the game loads far more than a screen draws (Basement I: 104 of 149 MB never drawn, 3.4 MB packed).
    // Images not drawn for a while go again when the GPU's total passes the budget (evict_textures). The packing
    // runs on the packer thread: until it is done the pixels are kept as they came (raw), and after it, beside the
    // packed ones until the first draw, within RAW_KEPT (rawKept).
    std::vector<uint8_t> lz;
    std::shared_ptr<std::vector<uint8_t> > raw;
    uint32_t gen;                       // which upload the packer's result belongs to
    int lw, lh;
    bool rawKept;
    uint32_t keptFrame;
    // pixels all one colour (the game makes blank images for its room targets at every door: 4 MB each copied,
    // packed and uploaded): only the colour is kept, and the image is made by a clear on the GPU
    bool solid;
    uint8_t solidRgba[4];
    // the image file its pixels are (hle_image.cpp), while nothing changed them: when the game deletes it, its image
    // waits in the reload pool for the next load of that file (next_pixels)
    std::string key;
};

struct Renderbuffer {
    // The game's one depth renderbuffer is sized to the screen's next powers of two (2048x2048 at 1080p, 16 MB), but
    // every target it backs is smaller and gets a depth image of its own size (sized): its image is made only when a
    // target of its very size draws
    Image img;
    int w = 0, h = 0;                   // as glRenderbufferStorage gave them
    uint32_t format;
    // GL lets a colour texture smaller than the depth buffer share it (its top left); vulkan4xenon, like the
    // console's eDRAM, wants attachments of one size, so each other colour size gets a depth image of its own
    std::map<std::pair<int, int>, Image> sized;
};

struct Fbo {
    uint32_t color, depth;              // texture / renderbuffer names
};

struct ShaderObj {
    uint32_t type;
    uint64_t hash;
    bool known;
};

struct Attr {
    std::string name;
    int loc, comps, offset;             // offset in the program's packed vertex (bytes)
};

struct Uniform {
    std::string name;
    int offset, bytes;                  // push constants; bytes < 0: a sampler, offset = binding
};

struct Program {
    uint32_t shaders[2];
    int nshaders;
    const GlVkProgramDesc* desc;
    std::vector<Attr> attrs;
    std::vector<Uniform> uniforms;      // index = GL uniform location
    int stride, pushBytes;
    int samplerUnit[SAMPLER_SLOTS];
    uint8_t push[PUSH_BYTES];
    VkShaderModule vs, fs;
    bool linked;
    // a plain twin (shaders.py PLAIN_VARIANTS): its fragment stage, and the attributes (indices in attrs) whose
    // values all 0 let a draw use it: ColorizeIn's alpha, ColorOffsetIn, PixelationAmount (<= 0), ClipPlane (xy 0,
    // z <= 0); -1: none
    VkShaderModule fsPlain;
    int plainAt[4];
};

struct VertexArray {
    bool enabled;
    int size, stride;
    uint32_t type, ptr;
    bool normalized;
};

// a pixel glReadPixels asked for, copied in the frame's commands and taken once its fence has passed
struct PixelRead {
    VkImage image;
    int x, y;
};

struct Frame {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    Buffer vertices, indices, upload;
    VkDeviceSize vOff, iOff, uOff;
    VkDescriptorPool pool;
    std::vector<Garbage> garbage;
    Buffer reads;                       // PIXEL_READS pixels, RGBA
    std::vector<PixelRead> pixelReads;
};

struct PassKey {
    VkImageView color, depth;
    bool operator<(const PassKey& o) const { return color != o.color ? color < o.color : depth < o.depth; }
    bool operator==(const PassKey& o) const { return color == o.color && depth == o.depth; }
};

struct PipeKey {
    int program;
    bool blend, cull, depthTest, depthWrite, hasDepth, plain;
    uint32_t src, dst, srcA, dstA, cullFace, depthFunc, frontFace, topology;
    bool operator<(const PipeKey& o) const { return memcmp(this, &o, sizeof(*this)) < 0; }
};

// ---- the device -------------------------------------------------------------------------------------------
GlVkHooks g_hooks;
bool g_enabled = false, g_ready = false;
VkInstance g_instance = VK_NULL_HANDLE;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkPhysicalDevice g_gpu = VK_NULL_HANDLE;
VkPhysicalDeviceMemoryProperties g_memProps;
uint32_t g_family = 0;
VkDevice g_dev = VK_NULL_HANDLE;
VkQueue g_queue = VK_NULL_HANDLE;
VkCommandPool g_cmdPool = VK_NULL_HANDLE;
VkFormat g_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
Frame g_frames[FRAMES];
int g_frame = 0;
VkDescriptorSetLayout g_setLayout = VK_NULL_HANDLE, g_presentSetLayout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE, g_presentLayout = VK_NULL_HANDLE;
std::map<uint32_t, VkSampler> g_samplers;
std::map<int, VkRenderPass> g_passes;              // colourLoad | depthLoad << 1 | hasDepth << 2 | output << 3
std::map<PipeKey, VkPipeline> g_pipelines;
std::map<PassKey, VkFramebuffer> g_framebuffers;
Image g_dummy;                                     // bound to sampler slots a program does not use
Image g_black;                                     // bloom's rows when they could not be made (adds nothing)
Image g_defColor, g_defDepth;                      // the default framebuffer
Buffer g_readback;
// output
VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
VkExtent2D g_swapExtent = {0, 0};
uint32_t g_swapCount = 0;
VkImage g_swapImages[MAX_SWAP];
VkImageView g_swapViews[MAX_SWAP];
VkFramebuffer g_swapFbs[MAX_SWAP];
VkSemaphore g_swapDone[MAX_SWAP];
VkPipeline g_presentPipe = VK_NULL_HANDLE, g_presentCopyPipe = VK_NULL_HANDLE;
// bloom's rows pass (bloom_rows; shaders.py bloom_fs): its pipeline, image and framebuffer, and the draw that reads it
VkPipeline g_bloomRowsPipe = VK_NULL_HANDLE;
Image g_bloomRows;
VkFramebuffer g_bloomRowsFb = VK_NULL_HANDLE;
const GlVkProgramDesc* g_bloomDesc = 0;
VkImageView g_bloomView = VK_NULL_HANDLE;           // set for the bloom draw being recorded: its Texture1
VkSampler g_bloomSampler = VK_NULL_HANDLE;
bool g_presentWhole = false;                        // the display holds the frame a whole number of times
Buffer g_presentQuad;
VkDescriptorPool g_presentPool = VK_NULL_HANDLE;
VkDescriptorSet g_presentSet = VK_NULL_HANDLE;

// recording state
bool g_recording = false, g_inPass = false;
PassKey g_passKey = {VK_NULL_HANDLE, VK_NULL_HANDLE};
int g_passW = 0, g_passH = 0;                 // the pass's area (from the target's corner)
// The pass's first clear, made again over the part a pass grows into (pass_area_for_draw)
struct PassClear {
    bool color, depth;
    float rgba[4], z;
};
PassClear g_passClear = {false, false, {0, 0, 0, 0}, 0};
uint32_t g_passDraws = 0;                           // draws in the pass being recorded
VkPipeline g_boundPipe = VK_NULL_HANDLE;
VkDescriptorSet g_lastSet = VK_NULL_HANDLE;
VkImageView g_lastViews[SAMPLER_SLOTS];
VkSampler g_lastSamplers[SAMPLER_SLOTS];

// ---- GL state ---------------------------------------------------------------------------------------------
std::map<uint32_t, Texture> g_textures;
std::map<uint32_t, Renderbuffer> g_renderbuffers;
std::map<uint32_t, Fbo> g_fbos;
std::map<uint32_t, ShaderObj> g_shaderObjs;
std::map<uint32_t, Program> g_programs;
uint32_t g_nextName = 1;
uint32_t g_units[MAX_UNITS];
uint32_t g_activeUnit = 0;
uint32_t g_program = 0, g_fbo = 0, g_rb = 0;
VertexArray g_va[MAX_ATTRIBS];
int g_viewport[4] = {0, 0, 0, 0};
float g_clearColor[4] = {0, 0, 0, 0};
float g_clearDepth = 1.0f;
bool g_blend = false, g_cull = false, g_depthTest = false, g_alphaTest = false, g_depthMask = true;
uint32_t g_src = 1, g_dst = 0, g_srcA = 1, g_dstA = 0, g_cullFace = GL_BACK, g_frontFace = GL_CCW, g_depthFunc = GL_LESS;
uint32_t g_alphaFunc = 0x0207;
float g_alphaRef = 0.0f;
std::map<std::string, int> g_onceLogged;
uint64_t g_imageBytes = 0;                         // live images (frame log)
uint64_t g_bufferBytes = 0;                        // live buffers (frame log)
VkDeviceSize g_ringHigh[3] = {0, 0, 0};            // the most of the vertex, index and upload rings a frame used
// what frames did, summed (shims_gl.cpp's slow frame lines and time lines): images made, bytes uploaded, waits
uint32_t g_imagesMade = 0;
uint64_t g_uploaded = 0;
double g_waitAcquire = 0, g_waitFence = 0, g_waitIdle = 0;   // ms: for the display, a frame's GPU work, read-backs
double g_texMs = 0, g_materializeMs = 0;            // ms in glTexImage2D (convert, pack) and in making packed images
uint64_t g_texBytes = 0;                            // RGBA bytes glTexImage2D was given
typedef std::chrono::steady_clock WaitClock;
double since(WaitClock::time_point t0) { return std::chrono::duration<double, std::milli>(WaitClock::now() - t0).count(); }
void ring_high(VkDeviceSize v, VkDeviceSize i, VkDeviceSize u) {
    if (v > g_ringHigh[0]) g_ringHigh[0] = v;
    if (i > g_ringHigh[1]) g_ringHigh[1] = i;
    if (u > g_ringHigh[2]) g_ringHigh[2] = u;
}
// ISAAC360_GL_STATS: per program, the z its vertices span and the states it draws with (build/recomp/gl_stats.txt)
struct ProgramStats {
    float zmin, zmax;
    uint64_t draws;
    std::set<std::string> modes;
    ProgramStats() : zmin(1e30f), zmax(-1e30f), draws(0) {}
};
bool g_stats = false;
std::map<std::string, ProgramStats> g_programStats;
// ISAAC360_SHADER_SAMPLES: per program, the push constants and first vertices of a few draws, the inputs
// tools/recomp/check_shaders.py runs the console's shaders on (build/recomp/shader_samples.txt)
struct ShaderSample {
    std::vector<float> push, verts;     // verts: up to 6 packed vertices of the draw's first triangles
};
bool g_samples = false;
uint32_t g_samplesFrom = 0;           // ISAAC360_SHADER_SAMPLES=N: from frame N (an effect a script starts later)
std::map<int, std::vector<ShaderSample> > g_shaderSamples;
enum { SAMPLE_DRAWS = 24, SAMPLE_VERTS = 6 };
std::set<float> g_clearDepths;
// What each frame did, for the frame lines (gl_vk_frame_counts)
struct FrameCounts {
    uint32_t passes, draws, depthClears;
};
FrameCounts g_counts, g_lastCounts;
// ISAAC360_TRACE_FRAME=N,M,...: each pass, clear and draw of those frames in the log (program, target, states,
// textures, the first vertex and the push constants)
std::set<uint32_t> g_traceFrames;
bool tracing() { return !g_traceFrames.empty() && g_traceFrames.count(gl_frame_count()); }

// GPU time: a time stamp at each frame's first command and its last (a frame is one submit at its end, so the two
// bound the GPU's work on it); a frame that also submitted mid-way (read-backs, full rings) is left out, its span
// holding the CPU's waits. With ISAAC360_PROFILE every 600th frame's draws are stamped one by one too, each after
// the GPU finished what came before, and the frame's GPU time by program and target size is logged once its fence
// has passed (gl_vk_gpu_trace_next). Query slots: two a frame, then the trace's.
enum { GPU_TRACE_MAX = 400 };
VkQueryPool g_queries = VK_NULL_HANDLE;
double g_tickNs = 0;
bool g_frameTimed[FRAMES], g_frameFlushed[FRAMES];
// Effect lines (ISAAC360_PROFILE or ISAAC360_EFFECT_LOG): a program drawn after 30 frames without it starts an effect, and
// 30 frames without it end it, logged with its frames and the GPU's work a frame meanwhile, so a pill's, a boss
// attack's or a room's screen effect shows its cost in the console's log (the gpu trace lines catch one frame in 600)
const int kEffectGap = 30;
uint64_t g_effectMask = 0;                      // the programs drawn this frame (bit = GLVK_PROGRAMS index)
uint64_t g_frameMask[FRAMES];                   // each frame slot's, for its GPU time
struct EffectRun {
    bool on;
    uint32_t first, last, frames, timed;
    double gpuMs;
};
EffectRun g_effects[64];
double g_gpuMs = 0, g_gpuMax = 0;
uint32_t g_gpuFrames = 0;
bool g_gpuTraceNext = false;
int g_gpuTraceSlot = -1;                            // the frame slot being traced, or -1
uint32_t g_gpuTraceFrame = 0;
std::vector<std::string> g_gpuTraceLabels;          // what each stamp after the first follows

void gpu_stamp(const std::string& label) {
    if (g_gpuTraceSlot != g_frame || g_gpuTraceLabels.size() + 1 >= GPU_TRACE_MAX) return;
    vkCmdWriteTimestamp(g_frames[g_frame].cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_queries,
                        (uint32_t)(2 * FRAMES + 1 + g_gpuTraceLabels.size()));
    g_gpuTraceLabels.push_back(label);
}

void log_once(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_onceLogged[buf]++) return;
    recomp_log("gl_vk: %s", buf);
}

// ---- Vulkan helpers ---------------------------------------------------------------------------------------
int find_memory(uint32_t bits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags want) {
    int fallback = -1;
    for (uint32_t i = 0; i < g_memProps.memoryTypeCount; ++i) {
        if (!(bits & (1u << i))) continue;
        VkMemoryPropertyFlags f = g_memProps.memoryTypes[i].propertyFlags;
        if ((f & need) != need) continue;
        if ((f & want) == want) return (int)i;
        if (fallback < 0) fallback = (int)i;
    }
    return fallback;
}

bool reclaim_images();
uint64_t g_readPixels = 0;                          // glReadPixels calls so far (the room lines)
uint64_t g_readPixelsWaited = 0;                    // those that waited for the GPU

bool make_buffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags want) {
    memset(&b, 0, sizeof(b));
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(g_dev, &bi, 0, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, b.buf, &req);
    const VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    int type = find_memory(req.memoryTypeBits, need, want);
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    bool got = type >= 0 && vkAllocateMemory(g_dev, &ai, 0, &b.mem) == VK_SUCCESS;
    if (!got && type >= 0 && reclaim_images()) got = vkAllocateMemory(g_dev, &ai, 0, &b.mem) == VK_SUCCESS;
    if (!got) {
        recomp_log("gl_vk: no memory for a %u-byte buffer", (unsigned)size);
        vkDestroyBuffer(g_dev, b.buf, 0);
        b.buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(g_dev, b.buf, b.mem, 0);
    g_bufferBytes += req.size;
    b.bytes = req.size;
    void* p = 0;
    vkMapMemory(g_dev, b.mem, 0, VK_WHOLE_SIZE, 0, &p);
    b.map = (uint8_t*)p;
    b.size = size;
    return true;
}

void free_buffer(Buffer& b) {
    if (b.mem) g_bufferBytes -= b.bytes;
    if (b.buf) vkDestroyBuffer(g_dev, b.buf, 0);
    if (b.mem) vkFreeMemory(g_dev, b.mem, 0);
    memset(&b, 0, sizeof(b));
}

bool make_image(Image& im, int w, int h, VkFormat format, bool depth) {
    memset(&im, 0, sizeof(im));
    VkImageCreateInfo ii;
    memset(&ii, 0, sizeof(ii));
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                     : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_dev, &ii, 0, &im.image) != VK_SUCCESS) {
        recomp_log("gl_vk: vkCreateImage %dx%d failed", w, h);
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_dev, im.image, &req);
    int type = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (type < 0) type = find_memory(req.memoryTypeBits, 0, 0);
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    bool got = type >= 0 && vkAllocateMemory(g_dev, &ai, 0, &im.mem) == VK_SUCCESS;
    if (!got && type >= 0 && reclaim_images()) got = vkAllocateMemory(g_dev, &ai, 0, &im.mem) == VK_SUCCESS;
    if (!got) {
        recomp_log("gl_vk: no memory for a %dx%d image", w, h);
        vkDestroyImage(g_dev, im.image, 0);
        im.image = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(g_dev, im.image, im.mem, 0);
    ++g_imagesMade;
    g_imageBytes += req.size;
    im.bytes = req.size;
    VkImageViewCreateInfo vi;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    vkCreateImageView(g_dev, &vi, 0, &im.view);
    im.w = w;
    im.h = h;
    return true;
}

void destroy_image_now(Image& im) {
    if (im.image) g_imageBytes -= im.bytes;
    if (im.view) vkDestroyImageView(g_dev, im.view, 0);
    if (im.image) vkDestroyImage(g_dev, im.image, 0);
    if (im.mem) vkFreeMemory(g_dev, im.mem, 0);
    memset(&im, 0, sizeof(im));
}

// an image may still be in this frame's commands or an earlier frame's: freed when this frame retires
void retire_image(Image& im) {
    if (!im.image) return;
    for (std::map<PassKey, VkFramebuffer>::iterator it = g_framebuffers.begin(); it != g_framebuffers.end();) {
        if (it->first.color == im.view || it->first.depth == im.view) {
            Garbage g;
            memset(&g, 0, sizeof(g));
            g.fb = it->second;
            g_frames[g_frame].garbage.push_back(g);
            g_framebuffers.erase(it++);
        } else {
            ++it;
        }
    }
    Garbage g;
    memset(&g, 0, sizeof(g));
    g.image = im;
    g_frames[g_frame].garbage.push_back(g);
    memset(&im, 0, sizeof(im));
}

void free_garbage(Frame& f) {
    for (size_t i = 0; i < f.garbage.size(); ++i) {
        Garbage& g = f.garbage[i];
        if (g.fb) vkDestroyFramebuffer(g_dev, g.fb, 0);
        destroy_image_now(g.image);
        free_buffer(g.buffer);
    }
    f.garbage.clear();
}

void barrier(VkImage image, bool depth, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (depth && g_depthFormat == VK_FORMAT_D32_SFLOAT) b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_frames[g_frame].cmd, srcStage, dstStage, 0, 0, 0, 0, 0, 1, &b);
}

const VkPipelineStageFlags kAllGraphics = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
const VkAccessFlags kAllAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

void begin_recording() {
    if (g_recording) return;
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_frames[g_frame].cmd, &bi);
    g_recording = true;
    g_boundPipe = VK_NULL_HANDLE;
    g_lastSet = VK_NULL_HANDLE;
}

void end_pass() {
    if (!g_inPass) return;
    vkCmdEndRenderPass(g_frames[g_frame].cmd);
    g_inPass = false;
}

// submits what is recorded and waits for it (read-backs, full rings); recording continues in the same frame
void flush_wait() {
    if (!g_recording) return;
    end_pass();
    Frame& f = g_frames[g_frame];
    vkEndCommandBuffer(f.cmd);
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    vkQueueSubmit(g_queue, 1, &si, VK_NULL_HANDLE);
    g_frameFlushed[g_frame] = true;
    WaitClock::time_point t0 = WaitClock::now();
    vkQueueWaitIdle(g_queue);
    g_waitIdle += since(t0);
    vkResetCommandBuffer(f.cmd, 0);
    g_recording = false;
    ring_high(f.vOff, f.iOff, f.uOff);
    f.vOff = f.iOff = f.uOff = 0;
    begin_recording();
}

VkRenderPass render_pass(bool colorLoad, bool depthLoad, bool hasDepth, bool output) {
    int key = (colorLoad ? 1 : 0) | (depthLoad ? 2 : 0) | (hasDepth ? 4 : 0) | (output ? 8 : 0);
    std::map<int, VkRenderPass>::iterator it = g_passes.find(key);
    if (it != g_passes.end()) return it->second;
    VkAttachmentDescription a[2];
    memset(a, 0, sizeof(a));
    a[0].format = (VkFormat)(output ? OUTPUT_FORMAT : COLOR_FORMAT);
    a[0].samples = VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp = colorLoad ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = colorLoad ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout = output ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    a[1].format = g_depthFormat;
    a[1].samples = VK_SAMPLE_COUNT_1_BIT;
    a[1].loadOp = depthLoad ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[1].stencilLoadOp = a[1].loadOp;
    a[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[1].initialLayout = depthLoad ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub;
    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = hasDepth ? &dref : 0;
    VkSubpassDependency deps[2];
    memset(deps, 0, sizeof(deps));
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = kAllGraphics;
    deps[0].srcAccessMask = kAllAccess | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = kAllGraphics;
    deps[1].dstAccessMask = kAllAccess | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkRenderPassCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = hasDepth ? 2 : 1;
    ci.pAttachments = a;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(g_dev, &ci, 0, &pass) != VK_SUCCESS) recomp_log("gl_vk: vkCreateRenderPass failed");
    g_passes[key] = pass;
    return pass;
}

VkShaderModule make_module(const uint32_t* code, uint32_t words) {
    VkShaderModuleCreateInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    mi.codeSize = words * 4;
    mi.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(g_dev, &mi, 0, &m);
    return m;
}

VkSampler sampler_for(uint32_t minF, uint32_t magF, uint32_t wrapS, uint32_t wrapT) {
    // mipmapped minifying filters sample the base level (the game makes no mipmaps)
    bool minLinear = minF == GL_LINEAR || minF == 0x2701 || minF == 0x2703;
    bool magLinear = magF == GL_LINEAR;
    uint32_t key = (minLinear ? 1u : 0u) | (magLinear ? 2u : 0u);
    VkSamplerAddressMode mode[2];
    uint32_t wraps[2] = {wrapS, wrapT};
    for (int i = 0; i < 2; ++i) {
        mode[i] = wraps[i] == GL_REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                : wraps[i] == GL_MIRRORED_REPEAT ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        key |= (uint32_t)mode[i] << (4 + 4 * i);
    }
    std::map<uint32_t, VkSampler>::iterator it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    VkSamplerCreateInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.minFilter = minLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = mode[0];
    si.addressModeV = mode[1];
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(g_dev, &si, 0, &s);
    g_samplers[key] = s;
    return s;
}

// one-shot work outside the frame (start-up)
void run_now(void (*fn)(VkCommandBuffer, void*), void* arg) {
    VkCommandBufferAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(g_dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    fn(cmd, arg);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(g_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_queue);
}

// uploads RGBA rows (GL order) into an image in SHADER_READ_ONLY layout, inside the frame's commands. An upload past
// half the ring gets a buffer of its own; when there is no memory for one (console boot 18: the epilogue's 1024x1024
// sheets, 4 MB each, with the heap fragmented), it goes through the ring in bands of rows, waiting for the GPU between
// them, rather than being dropped (the cutscene showed its first frame only).
void upload_rgba(Image& im, int x, int y, int w, int h, const uint8_t* rgba, bool fresh) {
    begin_recording();
    end_pass();
    Frame& f = g_frames[g_frame];
    VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
    g_uploaded += bytes;
    Buffer* src = &f.upload;
    VkDeviceSize off = (f.uOff + 15) & ~(VkDeviceSize)15;
    Buffer big;
    memset(&big, 0, sizeof(big));
    bool banded = false;
    if (off + bytes > f.upload.size) {
        if (bytes > f.upload.size / 2) {
            static const bool bands = getenv("ISAAC360_UPLOAD_BANDS") != 0;    // the fallback, to check it on the PC
            if (!bands && make_buffer(big, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                src = &big;
                off = 0;
            } else {
                banded = true;
                log_once("gl_vk: uploads without memory for a buffer of their own go through the ring in bands");
            }
        } else {
            flush_wait();
            off = 0;
        }
    }
    barrier(im.image, false, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAllAccess, VK_ACCESS_TRANSFER_WRITE_BIT, kAllGraphics,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.depth = 1;
    const VkDeviceSize row = (VkDeviceSize)w * 4;
    int band = banded ? (int)((f.upload.size / 2) / row) : h;
    if (band < 1) band = 1;
    for (int r = 0; r < h; r += band) {
        int n = h - r < band ? h - r : band;
        if (banded) {
            off = (f.uOff + 15) & ~(VkDeviceSize)15;
            if (off + n * row > f.upload.size) {
                flush_wait();
                off = 0;
            }
        }
        memcpy(src->map + off, rgba + r * row, (size_t)(n * row));
        if (src == &f.upload) f.uOff = off + n * row;
        region.bufferOffset = off;
        region.imageOffset.y = y + r;
        region.imageExtent.height = (uint32_t)n;
        vkCmdCopyBufferToImage(g_frames[g_frame].cmd, src->buf, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    barrier(im.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, kAllAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, kAllGraphics);
    if (big.buf) {
        Garbage g;
        memset(&g, 0, sizeof(g));
        g.buffer = big;
        f.garbage.push_back(g);
    }
}

void init_depth_layout(Image& im) {
    begin_recording();
    end_pass();
    barrier(im.image, true, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
}

// the default framebuffer follows the game's window size
void ensure_default_framebuffer() {
    int w = 0, h = 0;
    gl_window_size(w, h);
    if (w <= 0 || h <= 0) {
        w = 1280;
        h = 720;
    }
    if (g_defColor.image && g_defColor.w == w && g_defColor.h == h) return;
    retire_image(g_defColor);
    retire_image(g_defDepth);
    make_image(g_defColor, w, h, (VkFormat)COLOR_FORMAT, false);
    make_image(g_defDepth, w, h, g_depthFormat, true);
    std::vector<uint8_t> black((size_t)w * h * 4, 0);
    upload_rgba(g_defColor, 0, 0, w, h, &black[0], true);
    init_depth_layout(g_defDepth);
    if (g_presentSet) {
        // a frame the display holds a whole number of times (its size, or the console's 960x540 on a 1920x1080
        // display) takes nearest pixels; any other scale is filtered (shaders.py PRESENT_FS)
        uint32_t uw = (uint32_t)w, uh = (uint32_t)h;
        bool whole = !g_swapchain || (g_swapExtent.width % uw == 0 && g_swapExtent.height % uh == 0 &&
                                      g_swapExtent.width / uw == g_swapExtent.height / uh);
        uint32_t filter = whole ? GL_NEAREST : GL_LINEAR;
        g_presentWhole = whole && g_presentCopyPipe;
        VkDescriptorImageInfo img = {sampler_for(filter, filter, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE), g_defColor.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr;
        memset(&wr, 0, sizeof(wr));
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = g_presentSet;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &img;
        // the set may be in the frame being recorded: write it once that frame is done
        flush_wait();
        vkUpdateDescriptorSets(g_dev, 1, &wr, 0, 0);
    }
    recomp_log("gl_vk: default framebuffer %dx%d", w, h);
}

// ---- the frame target -------------------------------------------------------------------------------------
struct Target {
    Image* color;
    Image* depth;
    int w, h;
};

bool current_target(Target& t) {
    if (g_fbo == 0) {
        ensure_default_framebuffer();
        t.color = &g_defColor;
        t.depth = &g_defDepth;
    } else {
        Fbo& f = g_fbos[g_fbo];
        std::map<uint32_t, Texture>::iterator ct = g_textures.find(f.color);
        if (ct == g_textures.end() || !ct->second.img.image) {
            log_once("framebuffer %u has no colour texture", g_fbo);
            return false;
        }
        t.color = &ct->second.img;
        std::map<uint32_t, Renderbuffer>::iterator rt = g_renderbuffers.find(f.depth);
        Renderbuffer* rb = rt != g_renderbuffers.end() && rt->second.w > 0 ? &rt->second : 0;
        t.depth = 0;
        if (rb && rb->w == t.color->w && rb->h == t.color->h) {
            if (!rb->img.image && make_image(rb->img, rb->w, rb->h, g_depthFormat, true)) init_depth_layout(rb->img);
            t.depth = rb->img.image ? &rb->img : 0;
        } else if (rb) {
            Image& d = rb->sized[std::make_pair(t.color->w, t.color->h)];
            if (!d.image) {
                log_once("a %dx%d colour target with a %dx%d depth buffer: a depth image of the colour's size",
                         t.color->w, t.color->h, rb->w, rb->h);
                if (make_image(d, t.color->w, t.color->h, g_depthFormat, true)) init_depth_layout(d);
            }
            t.depth = d.image ? &d : 0;
        }
    }
    t.w = t.color->w;
    t.h = t.color->h;
    if (t.depth) {
        if (t.depth->w < t.w) t.w = t.depth->w;
        if (t.depth->h < t.h) t.h = t.depth->h;
    }
    return true;
}

// A pass covers what the target's draws reach rather than the whole target: the game draws its 960x540 picture in
// 1024x1024 targets, and on the console a pass's clear and its copy out of eDRAM cost by area (boot 18: 1.33 ms for
// each full-screen pass into 1024x1024). The area is the union of the viewports drawn into the target, on
// vulkan4xenon's 8-pixel grid (an area off it loads the target); a target's first pass, and every pass under
// ISAAC360_FULL_PASSES=1, covers it all. A draw past the area ends the pass and goes on in a wider one
// (pass_area_for_draw), whose new part gets the pass's clear.
const int kAreaGrid = 8;
void pass_area_for_draw();

void pass_area(const Target& t, int& w, int& h) {
    static const bool full = getenv("ISAAC360_FULL_PASSES") != 0;
    w = t.w;
    h = t.h;
    if (full || !t.color->drawnW) return;
    if (t.color->drawnW < w) w = t.color->drawnW;
    if (t.color->drawnH < h) h = t.color->drawnH;
}

// begins (or keeps) the pass on the bound framebuffer; a pass begun for a full clear skips the load
bool ensure_pass(bool clearsColor, bool clearsDepth) {
    Target t;
    if (!current_target(t)) return false;
    begin_recording();
    PassKey key = {t.color->view, t.depth ? t.depth->view : VK_NULL_HANDLE};
    if (g_inPass && g_passKey == key) return true;
    end_pass();
    bool hasDepth = t.depth != 0;
    VkRenderPass pass = render_pass(!clearsColor, !(clearsDepth && hasDepth), hasDepth, false);
    VkFramebuffer fb;
    std::map<PassKey, VkFramebuffer>::iterator it = g_framebuffers.find(key);
    if (it != g_framebuffers.end()) {
        fb = it->second;
    } else {
        VkImageView views[2] = {t.color->view, key.depth};
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = pass;
        fi.attachmentCount = hasDepth ? 2 : 1;
        fi.pAttachments = views;
        fi.width = (uint32_t)t.w;
        fi.height = (uint32_t)t.h;
        fi.layers = 1;
        fb = VK_NULL_HANDLE;
        vkCreateFramebuffer(g_dev, &fi, 0, &fb);
        g_framebuffers[key] = fb;
    }
    VkRenderPassBeginInfo rp;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = pass;
    rp.framebuffer = fb;
    int aw, ah;
    pass_area(t, aw, ah);
    rp.renderArea.extent.width = (uint32_t)aw;
    rp.renderArea.extent.height = (uint32_t)ah;
    vkCmdBeginRenderPass(g_frames[g_frame].cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    g_inPass = true;
    g_passKey = key;
    g_passClear.color = g_passClear.depth = false;
    g_passDraws = 0;
    ++g_counts.passes;
    if (tracing())
        recomp_log("trace: pass fbo %u, colour %dx%d (%s), depth %s (%s), area %dx%d", g_fbo, t.color->w, t.color->h,
                   clearsColor ? "cleared" : "loaded", t.depth ? "yes" : "none", clearsDepth ? "cleared" : "loaded", aw, ah);
    g_passW = aw;
    g_passH = ah;
    g_boundPipe = VK_NULL_HANDLE;
    return true;
}

VkBlendFactor blend_factor(uint32_t f) {
    switch (f) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 0x300: return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306: return VK_BLEND_FACTOR_DST_COLOR;
    case 0x307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    log_once("blend factor 0x%x", f);
    return VK_BLEND_FACTOR_ONE;
}

VkCompareOp compare_op(uint32_t f) { return (VkCompareOp)(f - 0x0200); }   // GL_NEVER.. and VK_COMPARE_OP_NEVER.. agree

uint32_t g_pipesMade = 0;
double g_pipeMs = 0;                                // making pipelines (slow frame lines)

VkPipeline pipeline(const PipeKey& k, Program& p) {
    // the last draw's key first: runs of sprites share one (a map search with a 60-byte key compare at each step)
    static PipeKey lastKey;
    static VkPipeline lastPipe = VK_NULL_HANDLE;
    if (lastPipe && !memcmp(&k, &lastKey, sizeof(k))) return lastPipe;
    std::map<PipeKey, VkPipeline>::iterator it = g_pipelines.find(k);
    if (it != g_pipelines.end()) {
        lastKey = k;
        lastPipe = it->second;
        return it->second;
    }
    ++g_pipesMade;
    struct Timed {
        WaitClock::time_point t0;
        ~Timed() { g_pipeMs += since(t0); }
    } timed = {WaitClock::now()};
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = i == 0 ? p.vs : k.plain && p.fsPlain ? p.fsPlain : p.fs;
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription binding = {0, (uint32_t)p.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attrs(p.attrs.size());
    static const VkFormat formats[5] = {VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                        VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
    for (size_t i = 0; i < p.attrs.size(); ++i) {
        attrs[i].location = (uint32_t)p.attrs[i].loc;
        attrs[i].binding = 0;
        attrs[i].format = formats[p.attrs[i].comps];
        attrs[i].offset = (uint32_t)p.attrs[i].offset;
    }
    VkPipelineVertexInputStateCreateInfo vin;
    memset(&vin, 0, sizeof(vin));
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vin.pVertexAttributeDescriptions = attrs.empty() ? 0 : &attrs[0];
    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = (VkPrimitiveTopology)k.topology;
    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = !k.cull ? VK_CULL_MODE_NONE : k.cullFace == GL_FRONT ? VK_CULL_MODE_FRONT_BIT
                : k.cullFace == GL_BACK ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_AND_BACK;
    // GL's window y and these images' rows run the same way, so GL's counter-clockwise is Vulkan's clockwise
    rs.frontFace = k.frontFace == GL_CCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss;
    memset(&dss, 0, sizeof(dss));
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = k.depthTest ? VK_TRUE : VK_FALSE;
    dss.depthWriteEnable = k.depthWrite ? VK_TRUE : VK_FALSE;
    dss.depthCompareOp = compare_op(k.depthFunc);
    VkPipelineColorBlendAttachmentState blend;
    memset(&blend, 0, sizeof(blend));
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = k.blend ? VK_TRUE : VK_FALSE;
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.srcColorBlendFactor = blend_factor(k.src);
    blend.dstColorBlendFactor = blend_factor(k.dst);
    blend.srcAlphaBlendFactor = blend_factor(k.srcA);
    blend.dstAlphaBlendFactor = blend_factor(k.dstA);
    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp;
    memset(&gp, 0, sizeof(gp));
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = k.hasDepth ? &dss : 0;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = g_layout;
    gp.renderPass = render_pass(true, true, k.hasDepth, false);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gp, 0, &pipe) != VK_SUCCESS)
        recomp_log("gl_vk: pipeline for %s failed", p.desc ? p.desc->label : "?");
    g_pipelines[k] = pipe;
    return pipe;
}

// ---- device set-up ----------------------------------------------------------------------------------------
VkPipeline quad_pipeline(const uint32_t* fsCode, size_t fsWords, VkRenderPass pass);

bool make_device() {
    PFN_vkGetInstanceProcAddr gipa = g_hooks.getInstanceProcAddr;
#define GLVK_LOAD_GLOBAL(name) name = (PFN_##name)gipa(VK_NULL_HANDLE, #name);
    GLVK_GLOBAL(GLVK_LOAD_GLOBAL)
    if (!vkCreateInstance) return false;
    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "isaac360";
    app.pEngineName = "isaac360";
    app.apiVersion = VK_API_VERSION_1_0;
    const char* exts[2];
    uint32_t extCount = 0;
    if (g_hooks.surfaceExtension) {
        exts[extCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
        exts[extCount++] = g_hooks.surfaceExtension;
    }
    const char* layer = "VK_LAYER_KHRONOS_validation";
    bool validation = false;
    if (g_hooks.validation && vkEnumerateInstanceLayerProperties) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, 0);
        std::vector<VkLayerProperties> props(n);
        if (n) vkEnumerateInstanceLayerProperties(&n, &props[0]);
        for (uint32_t i = 0; i < n; ++i)
            if (strcmp(props[i].layerName, layer) == 0) validation = true;
    }
    VkInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extCount;
    ci.ppEnabledExtensionNames = extCount ? exts : 0;
    ci.enabledLayerCount = validation ? 1 : 0;
    ci.ppEnabledLayerNames = validation ? &layer : 0;
    if (vkCreateInstance(&ci, 0, &g_instance) != VK_SUCCESS) {
        recomp_log("gl_vk: vkCreateInstance failed");
        return false;
    }
#define GLVK_LOAD_INSTANCE(name) name = (PFN_##name)gipa(g_instance, #name);
    GLVK_INSTANCE(GLVK_LOAD_INSTANCE)
    if (validation) recomp_log("gl_vk: validation on");
    if (g_hooks.createSurface && g_hooks.createSurface(g_instance, gipa, &g_surface) != VK_SUCCESS) {
        recomp_log("gl_vk: no surface");
        return false;
    }
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_instance, &n, 0);
    std::vector<VkPhysicalDevice> gpus(n);
    if (n) vkEnumeratePhysicalDevices(g_instance, &n, &gpus[0]);
    int best = -1, bestScore = -1;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, 0);
        std::vector<VkQueueFamilyProperties> qs(qn);
        if (qn) vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, &qs[0]);
        for (uint32_t q = 0; q < qn; ++q) {
            if (!(qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (g_surface) {
                VkBool32 ok = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, g_surface, &ok);
                if (!ok) continue;
            }
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(gpus[i], &p);
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > bestScore) {
                best = (int)i;
                bestScore = score;
                g_family = q;
            }
            break;
        }
    }
    if (best < 0) {
        recomp_log("gl_vk: no GPU");
        return false;
    }
    g_gpu = gpus[best];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_gpu, &props);
    vkGetPhysicalDeviceMemoryProperties(g_gpu, &g_memProps);
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_gpu, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) g_depthFormat = VK_FORMAT_D32_SFLOAT;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi;
    memset(&qi, 0, sizeof(qi));
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = g_family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char* swapExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di;
    memset(&di, 0, sizeof(di));
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = g_surface ? 1 : 0;
    di.ppEnabledExtensionNames = g_surface ? &swapExt : 0;
    if (vkCreateDevice(g_gpu, &di, 0, &g_dev) != VK_SUCCESS) {
        recomp_log("gl_vk: vkCreateDevice failed");
        return false;
    }
#define GLVK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(g_dev, #name);
    GLVK_DEVICE(GLVK_LOAD_DEVICE)
    vkGetDeviceQueue(g_dev, g_family, 0, &g_queue);
    recomp_log("gl_vk: %s, depth %s", props.deviceName, g_depthFormat == VK_FORMAT_D32_SFLOAT ? "D32" : "D24S8");
    g_tickNs = props.limits.timestampPeriod;
    if (g_tickNs > 0 && vkCreateQueryPool && vkCmdWriteTimestamp) {
        VkQueryPoolCreateInfo qi;
        memset(&qi, 0, sizeof(qi));
        qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 2 * FRAMES + GPU_TRACE_MAX;
        if (vkCreateQueryPool(g_dev, &qi, 0, &g_queries) != VK_SUCCESS) g_queries = VK_NULL_HANDLE;
    }
    return true;
}

bool make_frames() {
    VkCommandPoolCreateInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = g_family;
    if (vkCreateCommandPool(g_dev, &pi, 0, &g_cmdPool) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding b[SAMPLER_SLOTS];
    memset(b, 0, sizeof(b));
    for (int i = 0; i < SAMPLER_SLOTS; ++i) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li;
    memset(&li, 0, sizeof(li));
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = SAMPLER_SLOTS;
    li.pBindings = b;
    if (vkCreateDescriptorSetLayout(g_dev, &li, 0, &g_setLayout) != VK_SUCCESS) return false;
    li.bindingCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    if (vkCreateDescriptorSetLayout(g_dev, &li, 0, &g_presentSetLayout) != VK_SUCCESS) return false;
    VkPushConstantRange pc = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, PUSH_BYTES};
    VkPipelineLayoutCreateInfo pli;
    memset(&pli, 0, sizeof(pli));
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(g_dev, &pli, 0, &g_layout) != VK_SUCCESS) return false;
    pli.pSetLayouts = &g_presentSetLayout;
    VkPushConstantRange presentPc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};   // shaders.py PRESENT_FS
    pli.pPushConstantRanges = &presentPc;
    if (vkCreatePipelineLayout(g_dev, &pli, 0, &g_presentLayout) != VK_SUCCESS) return false;
    g_bloomRowsPipe = quad_pipeline(GLVK_BLOOM_ROWS_FS, sizeof(GLVK_BLOOM_ROWS_FS) / 4,
                                    render_pass(false, false, false, false));
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SETS * SAMPLER_SLOTS};
    VkDescriptorPoolCreateInfo dpi;
    memset(&dpi, 0, sizeof(dpi));
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = MAX_SETS;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    for (int i = 0; i < FRAMES; ++i) {
        Frame& f = g_frames[i];
        VkCommandBufferAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = g_cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(g_dev, &ai, &f.cmd) != VK_SUCCESS) return false;
        VkFenceCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(g_dev, &fi, 0, &f.fence) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(g_dev, &si, 0, &f.acquired) != VK_SUCCESS) return false;
        if (!make_buffer(f.vertices, VERTEX_RING, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0)) return false;
        if (!make_buffer(f.indices, INDEX_RING, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0)) return false;
        if (!make_buffer(f.upload, UPLOAD_RING, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) return false;
        make_buffer(f.reads, PIXEL_READS * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (vkCreateDescriptorPool(g_dev, &dpi, 0, &f.pool) != VK_SUCCESS) return false;
        f.vOff = f.iOff = f.uOff = 0;
    }
    // glReadPixels reads single pixels; a screenshot grows it (read_color)
    if (!make_buffer(g_readback, 1 << 20, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
        return false;
    // a 1x1 opaque white texture for sampler slots a program leaves empty
    make_image(g_dummy, 1, 1, (VkFormat)COLOR_FORMAT, false);
    make_image(g_black, 1, 1, (VkFormat)COLOR_FORMAT, false);
    begin_recording();
    const uint8_t white[4] = {255, 255, 255, 255}, black[4] = {0, 0, 0, 0};
    upload_rgba(g_dummy, 0, 0, 1, 1, white, true);
    upload_rgba(g_black, 0, 0, 1, 1, black, true);
    flush_wait();
    return true;
}

// A quad pipeline on the present's layout (one sampler, 32 bytes of fragment push constants) with g_presentQuad's
// vertices (position, uv): the present and bloom's rows. Made at the device's set-up as well as the output's, so
// a run without a display (the PC's hidden tests) has bloom's rows too.
VkPipeline quad_pipeline(const uint32_t* fsCode, size_t fsWords, VkRenderPass pass) {
    if (!g_presentQuad.buf) {
        // position, uv: the default framebuffer's row 0 is GL's bottom row
        const float quad[24] = {-1, -1, 0, 1, 1, -1, 1, 1, 1, 1, 1, 0, -1, -1, 0, 1, 1, 1, 1, 0, -1, 1, 0, 0};
        make_buffer(g_presentQuad, sizeof(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);
        memcpy(g_presentQuad.map, quad, sizeof(quad));
    }
    VkShaderModule vs = make_module(GLVK_PRESENT_VS, sizeof(GLVK_PRESENT_VS) / 4);
    VkShaderModule fs = make_module(fsCode, (uint32_t)fsWords);
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = i == 0 ? vs : fs;
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription binding = {0, 16, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}};
    VkPipelineVertexInputStateCreateInfo vin;
    memset(&vin, 0, sizeof(vin));
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend;
    memset(&blend, 0, sizeof(blend));
    blend.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp;
    memset(&gp, 0, sizeof(gp));
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = g_presentLayout;
    gp.renderPass = pass;
    VkPipeline pipe = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gp, 0, &pipe);
    vkDestroyShaderModule(g_dev, vs, 0);
    vkDestroyShaderModule(g_dev, fs, 0);
    return pipe;
}

bool make_output() {
    if (!g_surface) return true;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_gpu, g_surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = 1280;
        extent.height = 720;
    }
    g_swapExtent = extent;
    // three images: with two, a frame's work reached the GPU only once the one before it had shown (vulkan4xenon's
    // acquire waits for that), so the game's part and the GPU's ran one after the other and a room at 8 + 12.7 ms
    // missed a refresh in one frame of five (boot 18). A third lets the GPU work on one frame while the next is made,
    // a frame's more delay only while the GPU is the limit. ISAAC360_SWAP_IMAGES=2 keeps the shorter pad-to-screen path.
    static const uint32_t wanted = getenv("ISAAC360_SWAP_IMAGES") ? (uint32_t)atoi(getenv("ISAAC360_SWAP_IMAGES")) : 3;
    uint32_t count = wanted < caps.minImageCount ? caps.minImageCount : wanted;
    if (count < 2) count = 2;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > MAX_SWAP) count = MAX_SWAP;
    VkSwapchainCreateInfoKHR ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = g_surface;
    ci.minImageCount = count;
    ci.imageFormat = (VkFormat)OUTPUT_FORMAT;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -(int)caps.supportedCompositeAlpha);
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(g_dev, &ci, 0, &g_swapchain) != VK_SUCCESS) {
        recomp_log("gl_vk: vkCreateSwapchainKHR failed");
        return false;
    }
    g_swapCount = MAX_SWAP;
    vkGetSwapchainImagesKHR(g_dev, g_swapchain, &g_swapCount, g_swapImages);
    VkRenderPass pass = render_pass(false, false, false, true);
    for (uint32_t i = 0; i < g_swapCount; ++i) {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = g_swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)OUTPUT_FORMAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        vkCreateImageView(g_dev, &vi, 0, &g_swapViews[i]);
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = pass;
        fi.attachmentCount = 1;
        fi.pAttachments = &g_swapViews[i];
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layers = 1;
        vkCreateFramebuffer(g_dev, &fi, 0, &g_swapFbs[i]);
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(g_dev, &si, 0, &g_swapDone[i]);
    }
    // the present pipeline: a quad over the output, sampling the default framebuffer upside down; and one nearest
    // sample where the display holds the frame a whole number of times (shaders.py PRESENT_COPY_FS)
    g_presentPipe = quad_pipeline(GLVK_PRESENT_FS, sizeof(GLVK_PRESENT_FS) / 4, pass);
    g_presentCopyPipe = quad_pipeline(GLVK_PRESENT_COPY_FS, sizeof(GLVK_PRESENT_COPY_FS) / 4, pass);
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpi;
    memset(&dpi, 0, sizeof(dpi));
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(g_dev, &dpi, 0, &g_presentPool);
    VkDescriptorSetAllocateInfo sai;
    memset(&sai, 0, sizeof(sai));
    sai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sai.descriptorPool = g_presentPool;
    sai.descriptorSetCount = 1;
    sai.pSetLayouts = &g_presentSetLayout;
    vkAllocateDescriptorSets(g_dev, &sai, &g_presentSet);
    return true;
}

// ---- programs ---------------------------------------------------------------------------------------------
uint64_t fnv64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < s.size(); ++i) h = (h ^ (uint8_t)s[i]) * 0x100000001b3ull;
    return h;
}

// "a:1:2,b:3:4" -> rows of fields
std::vector<std::vector<std::string> > split_meta(const char* s) {
    std::vector<std::vector<std::string> > rows;
    std::string cur;
    std::vector<std::string> row;
    for (const char* p = s;; ++p) {
        if (*p == ':' || *p == ',' || !*p) {
            row.push_back(cur);
            cur.clear();
            if (*p != ':') {
                if (!(row.size() == 1 && row[0].empty())) rows.push_back(row);
                row.clear();
            }
            if (!*p) break;
        } else {
            cur += *p;
        }
    }
    return rows;
}

void clear_loc_cache();

bool link(Program& p) {
    clear_loc_cache();                              // the locations the game asked for may change
    uint64_t vs = 0, fs = 0;
    bool haveV = false, haveF = false;
    for (int i = 0; i < p.nshaders; ++i) {
        ShaderObj& s = g_shaderObjs[p.shaders[i]];
        if (s.type == GL_VERTEX_SHADER) { vs = s.hash; haveV = true; }
        else { fs = s.hash; haveF = true; }
    }
    p.desc = 0;
    for (size_t i = 0; i < sizeof(GLVK_PROGRAMS) / sizeof(GLVK_PROGRAMS[0]); ++i)
        if (haveV && haveF && GLVK_PROGRAMS[i].vsHash == vs && GLVK_PROGRAMS[i].fsHash == fs) p.desc = &GLVK_PROGRAMS[i];
    if (!p.desc) {
        recomp_log("gl_vk: no converted program for vs %016llx fs %016llx (run tools/recomp/shaders.py)",
                   (unsigned long long)vs, (unsigned long long)fs);
        p.linked = false;
        return false;
    }
    p.attrs.clear();
    p.uniforms.clear();
    int off = 0;
    std::vector<std::vector<std::string> > rows = split_meta(p.desc->attribs);
    for (size_t i = 0; i < rows.size(); ++i) {
        Attr a;
        a.name = rows[i][0];
        a.loc = atoi(rows[i][1].c_str());
        a.comps = atoi(rows[i][2].c_str());
        a.offset = off;
        off += 4 * a.comps;
        p.attrs.push_back(a);
    }
    p.stride = off ? off : 4;
    p.pushBytes = 0;
    rows = split_meta(p.desc->uniforms);
    for (size_t i = 0; i < rows.size(); ++i) {
        Uniform u;
        u.name = rows[i][0];
        u.offset = atoi(rows[i][1].c_str());
        u.bytes = atoi(rows[i][2].c_str());
        if (u.offset + u.bytes > p.pushBytes) p.pushBytes = u.offset + u.bytes;
        p.uniforms.push_back(u);
    }
    rows = split_meta(p.desc->samplers);
    for (size_t i = 0; i < rows.size(); ++i) {
        Uniform u;
        u.name = rows[i][0];
        u.offset = atoi(rows[i][1].c_str());
        u.bytes = -1;
        p.uniforms.push_back(u);
    }
    memset(p.push, 0, sizeof(p.push));
    for (int i = 0; i < SAMPLER_SLOTS; ++i) p.samplerUnit[i] = 0;
    p.vs = make_module(p.desc->vs, p.desc->vsWords);
    p.fs = make_module(p.desc->fs, p.desc->fsWords);
    p.fsPlain = VK_NULL_HANDLE;
    for (int i = 0; i < 4; ++i) p.plainAt[i] = -1;
    static const bool plainOn = !getenv("ISAAC360_PLAIN_SPRITES") || atoi(getenv("ISAAC360_PLAIN_SPRITES")) != 0;
    std::string twin = std::string(p.desc->label) + "#plain";
    for (size_t i = 0; plainOn && i < sizeof(GLVK_PROGRAMS) / sizeof(GLVK_PROGRAMS[0]); ++i)
        if (twin == GLVK_PROGRAMS[i].label) {
            static const char* const names[4] = {"ColorizeIn", "ColorOffsetIn", "PixelationAmount", "ClipPlane"};
            bool all = true;
            for (int n = 0; n < 4; ++n) {
                for (size_t a = 0; a < p.attrs.size(); ++a)
                    if (p.attrs[a].name == names[n]) p.plainAt[n] = (int)a;
                all = all && p.plainAt[n] >= 0;
            }
            if (all) p.fsPlain = make_module(GLVK_PROGRAMS[i].fs, GLVK_PROGRAMS[i].fsWords);
            else log_once("gl_vk: %s's plain twin lacks its attributes", p.desc->label);
        }
    p.linked = true;
    return true;
}

// ---- packed textures -------------------------------------------------------------------------------------
bool g_lazyTextures = true;
uint64_t g_packedBytes = 0;                        // the compressed pixels held (frame log)
// images above this total make the least recently drawn packed textures give their images back (ISAAC360_TEX_BUDGET MB)
uint64_t g_textureBudget = 96ull << 20;

// The packer thread compresses what glTexImage2D gave (a room's 5-14 MB took the game's thread ~10 ms on the PC,
// several times that on the console); collect_packed takes its results at the next upload or a frame's end.
// Until then the pixels stay as they came, so past PACK_HELD the uploads pack on their own thread: a new run gives
// ~150 MB of textures in one frame, and holding them all ran the console out of memory (boot 12).
struct PackJob {
    uint32_t name, gen;
    std::shared_ptr<std::vector<uint8_t> > raw;
    std::vector<uint8_t> lz;
    std::string key;            // an image file's first load: the image cache gets a copy (hle_image_store)
};
enum { PACK_HELD = 16 << 20 };
std::mutex g_packLock;
std::condition_variable g_packWake, g_packDoneWake;     // a job queued; a job done
std::deque<PackJob> g_packQueue, g_packDone;
bool g_packerStarted = false;
uint32_t g_packGen = 0;
size_t g_packHeld = 0;                              // the raw pixels of jobs not collected yet
// A packed texture not drawn yet keeps its raw pixels too, so its first draw makes its image without unpacking: a
// door's new textures are drawn a frame or two after their upload, and unpacking them took 40-110 ms of the door's
// frame on the console (boot 14). Up to RAW_KEPT, the oldest let go first, each for at most two seconds, and all of
// them when an image finds no memory.
enum { RAW_KEPT = 24 << 20, RAW_KEPT_FRAMES = 120 };
size_t g_rawKept = 0;
std::deque<std::pair<uint32_t, uint32_t> > g_keptRaw;   // (name, gen), oldest first

void release_raw(Texture& t) {
    if (!t.rawKept) return;
    t.rawKept = false;
    g_rawKept -= t.raw->size();
    g_packedBytes -= t.raw->size();
    t.raw.reset();
}

// lets kept raw pixels go: those of textures gone, drawn, re-given or past RAW_KEPT_FRAMES, and the oldest while
// more than keep bytes are kept
void release_kept_raw(size_t keep) {
    while (!g_keptRaw.empty()) {
        std::map<uint32_t, Texture>::iterator it = g_textures.find(g_keptRaw.front().first);
        if (it != g_textures.end() && it->second.rawKept && it->second.gen == g_keptRaw.front().second) {
            Texture& t = it->second;
            if (g_rawKept <= keep && t.keptFrame + RAW_KEPT_FRAMES > gl_frame_count()) break;
            release_raw(t);
        }
        g_keptRaw.pop_front();
    }
}

void packer_thread(void*) {
    if (recomp_config().threadStarted) recomp_config().threadStarted("packer");
    std::unique_lock<std::mutex> l(g_packLock);
    for (;;) {
        while (g_packQueue.empty()) g_packWake.wait(l);
        PackJob job;
        std::swap(job, g_packQueue.front());
        g_packQueue.pop_front();
        l.unlock();
        lz_compress(&(*job.raw)[0], job.raw->size(), job.lz);
        l.lock();
        g_packDone.push_back(PackJob());
        std::swap(g_packDone.back(), job);
        g_packDoneWake.notify_all();
    }
}

void collect_packed();

// Past PACK_HELD the game's thread waits for the packer to catch up rather than packing itself: the packer runs on
// another hardware thread beside the game's loading, where packing on the game's thread added every byte of a new
// run's ~90 MB to that one frame (boot 20: 178 ms of glTexImage2D in the new run's 2.4 s frame).
void pack_later(uint32_t name, Texture& t, std::vector<uint8_t>& rgba, const std::string& key = std::string()) {
    collect_packed();
    t.gen = ++g_packGen;
    if (g_packHeld + rgba.size() > PACK_HELD && g_packerStarted) {
        log_once("gl_vk: the packer holds %u MB; uploads wait for it", (unsigned)(g_packHeld >> 20));
        while (g_packHeld && g_packHeld + rgba.size() > PACK_HELD) {
            {
                std::unique_lock<std::mutex> l(g_packLock);
                if (g_packDone.empty()) g_packDoneWake.wait_for(l, std::chrono::milliseconds(50));
            }
            collect_packed();
        }
    }
    t.raw.reset(new std::vector<uint8_t>());
    t.raw->swap(rgba);
    g_packedBytes += t.raw->size();
    g_packHeld += t.raw->size();
    std::lock_guard<std::mutex> l(g_packLock);
    if (!g_packerStarted) {
        g_packerStarted = true;
        host_thread_start(packer_thread, 0, 256u << 10);
    }
    g_packQueue.push_back(PackJob());
    g_packQueue.back().name = name;
    g_packQueue.back().gen = t.gen;
    g_packQueue.back().raw = t.raw;
    g_packQueue.back().key = key;
    g_packWake.notify_one();
}

// the packer's finished textures keep their packed pixels and let the raw ones go (a newer upload's are newer)
void collect_packed() {
    std::deque<PackJob> done;
    {
        std::lock_guard<std::mutex> l(g_packLock);
        done.swap(g_packDone);
    }
    for (size_t i = 0; i < done.size(); ++i) {
        g_packHeld -= done[i].raw->size();
        if (!done[i].key.empty())                       // a first load: the image cache's copy
            hle_image_store(done[i].key, std::make_shared<const std::vector<uint8_t> >(done[i].lz));
        std::map<uint32_t, Texture>::iterator it = g_textures.find(done[i].name);
        if (it == g_textures.end() || !it->second.raw || it->second.gen != done[i].gen) continue;
        Texture& t = it->second;
        t.lz.swap(done[i].lz);
        t.packed = (uint32_t)t.lz.size();
        g_packedBytes += t.lz.size();
        size_t n = t.raw->size();
        if (!t.img.image && n <= RAW_KEPT) {
            release_kept_raw(RAW_KEPT - n);
            t.rawKept = true;
            t.keptFrame = gl_frame_count();
            g_rawKept += n;
            g_keptRaw.push_back(std::make_pair(it->first, t.gen));
        } else {
            g_packedBytes -= n;
            t.raw.reset();
        }
    }
    release_kept_raw(RAW_KEPT);
}

bool has_pixels(const Texture& t) { return t.raw || !t.lz.empty() || t.solid; }

void drop_packed(Texture& t) {
    t.solid = false;
    t.key.clear();
    release_raw(t);
    g_packedBytes -= t.lz.size();
    std::vector<uint8_t>().swap(t.lz);
    if (t.raw) g_packedBytes -= t.raw->size();
    t.raw.reset();
}

// ---- images the game loads again ---------------------------------------------------------------------------
// The game frees a room's images when it leaves and loads the same files again at a later door: 5-10 MB a door
// copied from the guest, packed and uploaded each time (17-27 and 16-24 ms of a console door, boot 20). The image
// cache (hle_image.cpp) names the file whose pixels the next glTexImage2D gets and hands over its packed copy
// (gl_vk_next_pixels): the texture keeps that rather than packing its own, and a deleted texture of a file keeps
// its image in the reload pool, so the file's next load takes the image as it is, with no copy, packing or upload.
// ISAAC360_RELOAD_POOL=MB sizes the pool (default 24, 0 turns it off); the oldest go first, all when memory is short.
struct NextPixels {
    uint32_t buf, bytes;
    std::string key;
    std::shared_ptr<const std::vector<uint8_t> > packed;
    bool filled;                                    // the guest buffer holds the pixels too
    uint32_t frame;
};
NextPixels g_next;
struct Pooled {
    Image img;
    std::vector<uint8_t> lz;
    int lw, lh;
    uint32_t frame;                                 // when it went in
};
std::map<std::string, Pooled> g_reloadPool;
uint64_t g_poolBytes = 0;                           // the pool's images
uint64_t g_poolReused = 0;                          // images taken from it (the slow frame lines)

uint64_t pool_budget() {
    static const long mb = getenv("ISAAC360_RELOAD_POOL") ? atol(getenv("ISAAC360_RELOAD_POOL")) : 24;
    return mb > 0 ? (uint64_t)mb << 20 : 0;
}

void pool_erase(std::map<std::string, Pooled>::iterator it) {
    g_poolBytes -= it->second.img.bytes;
    g_packedBytes -= it->second.lz.size();
    retire_image(it->second.img);
    g_lastSet = VK_NULL_HANDLE;                     // a new image may reuse a freed view's handle
    g_reloadPool.erase(it);
}

// the oldest go until the pool's images are within keep bytes
void pool_trim(uint64_t keep) {
    while (g_poolBytes > keep && !g_reloadPool.empty()) {
        std::map<std::string, Pooled>::iterator oldest = g_reloadPool.begin();
        for (std::map<std::string, Pooled>::iterator it = g_reloadPool.begin(); it != g_reloadPool.end(); ++it)
            if (it->second.frame < oldest->second.frame) oldest = it;
        pool_erase(oldest);
    }
}

// a texture the game deletes: its image (and packed pixels) wait for the next load of its file
bool pool_put(Texture& t) {
    if (t.key.empty() || !t.img.image || t.target || t.lz.empty() || !pool_budget() || t.img.bytes > pool_budget() / 2)
        return false;
    std::map<std::string, Pooled>::iterator it = g_reloadPool.find(t.key);
    if (it != g_reloadPool.end()) pool_erase(it);
    Pooled& p = g_reloadPool[t.key];
    p.img = t.img;
    memset(&t.img, 0, sizeof(t.img));
    p.lz.swap(t.lz);                                // still counted in g_packedBytes
    p.lw = t.lw;
    p.lh = t.lh;
    p.frame = gl_frame_count();
    g_poolBytes += p.img.bytes;
    pool_trim(pool_budget());
    return true;
}

// glTexImage2D given the pixels the image cache named: the file's pooled image, or the cache's packed copy (and
// the pixels as they came, for the first draw). False: not those pixels (the guest buffer then holds them).
bool take_named(uint32_t name, Texture& t, uint32_t pixels, uint32_t format, uint32_t type, int w, int h) {
    if (!g_next.buf || pixels != g_next.buf) return false;
    NextPixels next = g_next;
    g_next.buf = 0;
    g_next.packed.reset();
    if (next.frame != gl_frame_count() || format != GL_RGBA || type != GL_UNSIGNED_BYTE || t.target ||
        (uint64_t)w * h * 4 != next.bytes || (!next.packed && !next.filled) || !g_lazyTextures) {
        log_once("gl_vk: a named image went the usual way (%dx%d, format 0x%x, %u bytes named)", w, h, format,
                 next.bytes);
        if (!next.filled && next.packed && pixels < g_mem_size && next.bytes <= g_mem_size - pixels &&
            !lz_decompress(&(*next.packed)[0], next.packed->size(), g_mem + pixels, next.bytes))
            log_once("lz: a named image did not unpack");
        return false;
    }
    t.lw = w;
    t.lh = h;
    t.key = next.key;
    if (t.img.image) {
        if (g_inPass && t.img.view == g_passKey.color) end_pass();
        retire_image(t.img);
        g_lastSet = VK_NULL_HANDLE;
    }
    std::map<std::string, Pooled>::iterator it = g_reloadPool.find(next.key);
    if (it != g_reloadPool.end() && it->second.lw == w && it->second.lh == h) {
        // ISAAC360_POOL_CHECK=1: the image's packed pixels against this load's (logged when they differ)
        static const bool check = getenv("ISAAC360_POOL_CHECK") != 0;
        if (check) {
            // against this load's packed copy, or (a first load) its pixels in the guest buffer
            static uint64_t checked = 0, differ = 0;
            bool same;
            if (next.packed) {
                same = it->second.lz == *next.packed;
            } else {
                std::vector<uint8_t> px(next.bytes);
                same = pixels < g_mem_size && next.bytes <= g_mem_size - pixels &&
                       lz_decompress(&it->second.lz[0], it->second.lz.size(), &px[0], px.size()) &&
                       !memcmp(&px[0], g_mem + pixels, px.size());
            }
            ++checked;
            if (!same && ++differ <= 20)
                recomp_log("pool check: %s differs from its kept image's pixels", next.key.c_str());
            if (checked % 100 == 0)
                recomp_log("pool check: %llu reuses compared, %llu differ", (unsigned long long)checked,
                           (unsigned long long)differ);
        }
        t.img = it->second.img;                     // the image as the file's last load left it
        memset(&it->second.img, 0, sizeof(it->second.img));
        t.lz.swap(it->second.lz);
        t.packed = (uint32_t)t.lz.size();
        g_poolBytes -= t.img.bytes;
        g_reloadPool.erase(it);
        ++g_poolReused;
        if (!next.packed) hle_image_store(next.key, std::make_shared<const std::vector<uint8_t> >(t.lz));
        return true;
    }
    if (!next.packed) {
        // a first load: packed on the packer's thread like any upload, which hands the image cache its copy
        if (pixels >= g_mem_size || next.bytes > g_mem_size - pixels) return false;
        std::vector<uint8_t> rgba(g_mem + pixels, g_mem + pixels + next.bytes);
        pack_later(name, t, rgba, next.key);
        return true;
    }
    t.lz.assign(next.packed->begin(), next.packed->end());
    t.packed = (uint32_t)t.lz.size();
    g_packedBytes += t.lz.size();
    t.gen = ++g_packGen;
    if (next.filled && next.bytes <= RAW_KEPT && pixels < g_mem_size && next.bytes <= g_mem_size - pixels) {
        // the first draw makes the image from these, as from the packer's kept ones (collect_packed)
        t.raw.reset(new std::vector<uint8_t>(g_mem + pixels, g_mem + pixels + next.bytes));
        g_packedBytes += next.bytes;
        release_kept_raw(RAW_KEPT - next.bytes);
        t.rawKept = true;
        t.keptFrame = gl_frame_count();
        g_rawKept += next.bytes;
        g_keptRaw.push_back(std::make_pair(name, t.gen));
    }
    return true;
}

// an image filled with one colour on the GPU, in SHADER_READ_ONLY layout; outside a render pass
void clear_image(Image& im, const uint8_t* rgba, bool fresh) {
    begin_recording();
    end_pass();
    barrier(im.image, false, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAllAccess, VK_ACCESS_TRANSFER_WRITE_BIT, kAllGraphics,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue v;
    for (int i = 0; i < 4; ++i) v.float32[i] = rgba[i] / 255.0f;
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(g_frames[g_frame].cmd, im.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &range);
    barrier(im.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, kAllAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, kAllGraphics);
}

// the guest's RGBA bytes all one colour (the first difference ends the look, so pictures cost a few reads)
bool solid_rgba(uint32_t pixels, int w, int h, uint8_t* rgba) {
    size_t n = (size_t)w * h;
    if (!pixels || pixels >= g_mem_size || n * 4 > g_mem_size - pixels) return false;
    const uint8_t* p = g_mem + pixels;
    uint32_t first;
    memcpy(&first, p, 4);
    for (size_t i = 1; i < n; ++i) {
        uint32_t v;
        memcpy(&v, p + 4 * i, 4);
        if (v != first) return false;
    }
    memcpy(rgba, p, 4);
    return true;
}

// the GPU image of a packed texture, made from its pixels; outside a render pass (the upload ends it)
bool materialize(Texture& t) {
    if (t.img.image || !has_pixels(t)) return t.img.image != VK_NULL_HANDLE;
    WaitClock::time_point t0 = WaitClock::now();
    struct Timed {
        WaitClock::time_point t0;
        ~Timed() { g_materializeMs += since(t0); }
    } timed = {t0};
    if (!make_image(t.img, t.lw, t.lh, (VkFormat)COLOR_FORMAT, false)) return false;
    if (t.solid) {
        clear_image(t.img, t.solidRgba, true);
        return true;
    }
    if (t.raw) {                                        // not packed yet, or kept for this draw: as they came
        upload_rgba(t.img, 0, 0, t.lw, t.lh, &(*t.raw)[0], true);
        release_raw(t);
        return true;
    }
    std::vector<uint8_t> rgba((size_t)t.lw * t.lh * 4);
    if (!lz_decompress(&t.lz[0], t.lz.size(), &rgba[0], rgba.size())) {
        log_once("lz: a packed texture did not unpack");
        std::fill(rgba.begin(), rgba.end(), 0);
    }
    upload_rgba(t.img, 0, 0, t.lw, t.lh, &rgba[0], true);
    return true;
}

// No memory for an image: the packed textures no draw used this frame or the last give their images back (their
// pixels stay packed; a later draw makes the image again), the GPU finishes what is recorded, and their memory goes
// at once rather than when the frame retires. True if any went. A target without an image would draw nothing and
// read as the 1x1 white placeholder (the white playfield of console boot 10).
bool reclaim_images() {
    release_kept_raw(0);
    uint64_t freed = g_poolBytes;
    pool_trim(0);
    for (std::map<uint32_t, Texture>::iterator it = g_textures.begin(); it != g_textures.end(); ++it) {
        Texture& t = it->second;
        if (!t.img.image || !has_pixels(t) || t.target || t.drawFrame + 1 >= gl_frame_count()) continue;
        if (g_inPass && t.img.view == g_passKey.color) end_pass();
        freed += t.img.bytes;
        retire_image(t.img);
    }
    if (!freed) return false;
    g_lastSet = VK_NULL_HANDLE;                           // a new image may reuse a freed view's handle
    if (g_recording) flush_wait();
    else vkQueueWaitIdle(g_queue);
    for (int i = 0; i < FRAMES; ++i) free_garbage(g_frames[i]);
    log_once("out of memory for images: idle textures give theirs back (%u MB)", (unsigned)(freed >> 20));
    return true;
}

// at the end of a frame: the least recently drawn packed textures not drawn this frame give their images back
// until the total is under the budget (their pixels stay packed; a later draw makes the image again). On the
// console, also while free memory is short: those not drawn for a second, up to 16 MB a time (checked every 30
// frames; retired images free two frames later).
void evict_textures() {
    if (g_hostFreeMemory && gl_frame_count() % 30 == 0 && g_hostFreeMemory() < (32u << 20)) {
        release_kept_raw(0);
        if (g_poolBytes) log_once("free memory short: the reload pool's images go");
        pool_trim(0);
        std::vector<std::pair<uint32_t, uint32_t> > idle;     // (last drawn frame, name)
        for (std::map<uint32_t, Texture>::iterator it = g_textures.begin(); it != g_textures.end(); ++it)
            if (it->second.img.image && has_pixels(it->second) && !it->second.target &&
                it->second.drawFrame + 60 < gl_frame_count())
                idle.push_back(std::make_pair(it->second.drawFrame, it->first));
        std::sort(idle.begin(), idle.end());
        uint64_t freed = 0;
        for (size_t i = 0; i < idle.size() && freed < (16u << 20); ++i) {
            Texture& t = g_textures[idle[i].second];
            if (g_inPass && t.img.view == g_passKey.color) end_pass();
            freed += t.img.bytes;
            retire_image(t.img);
            g_lastSet = VK_NULL_HANDLE;
        }
        if (freed) log_once("free memory short: idle textures give their images back");
    }
    if (g_imageBytes > g_textureBudget) pool_trim(0);   // images for reloads go before any a texture has
    if (g_imageBytes <= g_textureBudget) return;
    std::vector<std::pair<uint32_t, uint32_t> > idle;     // (last drawn frame, name)
    for (std::map<uint32_t, Texture>::iterator it = g_textures.begin(); it != g_textures.end(); ++it)
        if (it->second.img.image && has_pixels(it->second) && !it->second.target &&
            it->second.drawFrame + 1 < gl_frame_count())
            idle.push_back(std::make_pair(it->second.drawFrame, it->first));
    std::sort(idle.begin(), idle.end());
    uint64_t bytes = g_imageBytes;                         // a retired image is counted until its frame retires
    for (size_t i = 0; i < idle.size() && bytes > g_textureBudget * 9 / 10; ++i) {
        Texture& t = g_textures[idle[i].second];
        if (g_inPass && t.img.view == g_passKey.color) end_pass();
        bytes -= t.img.bytes;
        retire_image(t.img);
        g_lastSet = VK_NULL_HANDLE;                       // a new image may reuse a freed view's handle
    }
}

// ---- drawing ----------------------------------------------------------------------------------------------
float attr_component(const VertexArray& a, uint32_t base, int c) {
    switch (a.type) {
    case GL_FLOAT: return RDF(base + 4 * c);
    case GL_UNSIGNED_BYTE: { float v = (float)RD8(base + c); return a.normalized ? v / 255.0f : v; }
    case GL_UNSIGNED_SHORT: { float v = (float)RD16(base + 2 * c); return a.normalized ? v / 65535.0f : v; }
    case GL_SHORT: { float v = (float)(int16_t)RD16(base + 2 * c); return a.normalized ? v / 32767.0f : v; }
    }
    log_once("vertex attribute type 0x%x", a.type);
    return 0.0f;
}

int type_bytes(uint32_t t) { return t == GL_FLOAT || t == GL_UNSIGNED_INT ? 4 : t == GL_UNSIGNED_SHORT || t == GL_SHORT ? 2 : 1; }

// Bloom's rows (shaders.py bloom_fs): before the game's bloom draw (one quad, Texture0 the scene), each texel's six
// squares along its row go into g_bloomRows, an image of the scene's size, over the part of it the scene's passes
// drew (all of it where the scene's texture repeats); the draw then reads them as its Texture1. The draw comes first
// in a pass that holds only its clears, so that pass ends and the target's begins again with the same clears rather
// than loading. Without the image the draw reads black: the scene without its bloom.
void clear_again(const PassClear& cleared, const VkClearRect* rects, uint32_t n);

void bloom_rows(Program& p, uint32_t first) {
    g_bloomView = g_black.view;
    g_bloomSampler = sampler_for(GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    float rx = 0;
    for (size_t a = 0; a < p.attrs.size(); ++a)
        if (p.attrs[a].name == "Ratio") {
            const VertexArray& va = g_va[p.attrs[a].loc];
            if (!va.enabled) break;
            int stride = va.stride ? va.stride : va.size * type_bytes(va.type);
            rx = attr_component(va, va.ptr + first * (uint32_t)stride, 0);
        }
    Texture* src = 0;
    for (size_t u = 0; u < p.uniforms.size(); ++u)
        if (p.uniforms[u].bytes < 0 && p.uniforms[u].offset == 0) {
            uint32_t unit = (uint32_t)p.samplerUnit[0];
            std::map<uint32_t, Texture>::iterator it = g_textures.find(unit < MAX_UNITS ? g_units[unit] : 0);
            if (it != g_textures.end()) src = &it->second;
        }
    if (!src || !src->img.image || !g_bloomRowsPipe || !g_presentQuad.buf) {
        log_once("gl_vk: bloom's rows not made (scene %d, pipeline %d, quad %d): the bloom draws without them",
                 src && src->img.image ? 1 : 0, g_bloomRowsPipe ? 1 : 0, g_presentQuad.buf ? 1 : 0);
        return;
    }
    Image& im = src->img;
    log_once("gl_vk: bloom's rows from a %dx%d scene (drawn %dx%d), %g apart", im.w, im.h, im.drawnW, im.drawnH, rx);
    if (!g_bloomRows.image || g_bloomRows.w != im.w || g_bloomRows.h != im.h) {
        if (g_bloomRows.image) {
            Frame& f = g_frames[g_frame];
            Garbage g;
            memset(&g, 0, sizeof(g));
            g.image = g_bloomRows;
            g.fb = g_bloomRowsFb;
            f.garbage.push_back(g);
            memset(&g_bloomRows, 0, sizeof(g_bloomRows));
            g_bloomRowsFb = VK_NULL_HANDLE;
        }
        if (!make_image(g_bloomRows, im.w, im.h, (VkFormat)COLOR_FORMAT, false)) {
            log_once("gl_vk: no memory for bloom's rows: the bloom draws without them");
            return;
        }
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = render_pass(false, false, false, false);
        fi.attachmentCount = 1;
        fi.pAttachments = &g_bloomRows.view;
        fi.width = (uint32_t)im.w;
        fi.height = (uint32_t)im.h;
        fi.layers = 1;
        vkCreateFramebuffer(g_dev, &fi, 0, &g_bloomRowsFb);
    }
    // the area: what the scene's passes drew, or all of it where a repeat could reach the rest
    int aw = im.w, ah = im.h;
    if (src->wrapS != GL_REPEAT && src->wrapT != GL_REPEAT && im.drawnW && im.drawnH) {
        aw = im.drawnW;
        ah = im.drawnH;
    }
    PassClear cleared = g_passClear;
    bool reopen = g_inPass && !g_passDraws;
    begin_recording();
    end_pass();
    Frame& f = g_frames[g_frame];
    VkDescriptorSetAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = f.pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g_presentSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(g_dev, &ai, &set) != VK_SUCCESS) return;
    VkDescriptorImageInfo img = {sampler_for(src->minFilter, src->magFilter, src->wrapS, src->wrapT), im.view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr;
    memset(&wr, 0, sizeof(wr));
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &img;
    vkUpdateDescriptorSets(g_dev, 1, &wr, 0, 0);
    VkRenderPassBeginInfo rp;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass(false, false, false, false);
    rp.framebuffer = g_bloomRowsFb;
    rp.renderArea.extent.width = (uint32_t)aw;
    rp.renderArea.extent.height = (uint32_t)ah;
    vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_bloomRowsPipe);
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_presentLayout, 0, 1, &set, 0, 0);
    const float pc[8] = {1.0f / im.w, 1.0f / im.h, 0, 0, rx, 0, 0, 0};
    vkCmdPushConstants(f.cmd, g_presentLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
    VkViewport vp = {0, 0, (float)aw, (float)ah, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {(uint32_t)aw, (uint32_t)ah}};
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(f.cmd, 0, 1, &g_presentQuad.buf, &off);
    vkCmdDraw(f.cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(f.cmd);
    g_boundPipe = VK_NULL_HANDLE;
    g_lastSet = VK_NULL_HANDLE;
    if (g_gpuTraceSlot == g_frame) {
        char where[64];
        snprintf(where, sizeof(where), "bloom's rows into %dx%d", aw, ah);
        gpu_stamp(where);
    }
    g_bloomView = g_bloomRows.view;
    g_bloomSampler = sampler_for(GL_NEAREST, GL_NEAREST, src->wrapS, src->wrapT);
    if (reopen && ensure_pass(cleared.color, cleared.depth)) {
        g_passClear = cleared;
        clear_again(cleared, 0, 0);
    }
}

// ISAAC360_FILL_STATS=1 (audits): the pixels each frame's draws cover, summed (overdraw), for sprites (the
// coloroffset programs, apart those drawn plain) and the rest. The PC's GPU is far too fast to show what overdraw
// costs the console (the Death card's smoke took the console's GPU to 20.5 ms and the PC's not past its usual
// frames). A sprite triangle's area comes from its Position through the program's Transform as glUniformMatrix4fv
// gave it (column-major), clamped to the viewport; a draw of a program without one counts its viewport.
bool g_fillStats = false;
double g_fillSprite = 0, g_fillPlain = 0, g_fillOther = 0;     // pixels, summed over all frames
std::vector<uint32_t>& draw_indices() {
    static std::vector<uint32_t> v;
    return v;
}
void add_fill(const Program& p, uint32_t lo, int count, bool plain) {
    double vw = g_viewport[2], vh = g_viewport[3];
    bool sprite = p.desc && strstr(p.desc->label, "coloroffset") != 0;
    const float* m = 0;
    for (size_t u = 0; u < p.uniforms.size(); ++u)
        if (p.uniforms[u].bytes == 64 && p.uniforms[u].name == "Transform") m = (const float*)(p.push + p.uniforms[u].offset);
    if (!m || p.attrs.empty() || p.attrs[0].name != "Position") {
        (sprite ? g_fillSprite : g_fillOther) += vw * vh;
        return;
    }
    const std::vector<uint32_t>& idx = draw_indices();
    const VertexArray& va = g_va[p.attrs[0].loc];
    if (!va.enabled || va.type != GL_FLOAT) return;
    int stride = va.stride ? va.stride : va.size * 4;
    double sum = 0;
    for (int t = 0; t + 2 < count; t += 3) {
        double x[3], y[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t base = va.ptr + idx[(size_t)(t + k)] * (uint32_t)stride;
            float px = RDF(base), py = va.size > 1 ? RDF(base + 4) : 0.0f, pz = va.size > 2 ? RDF(base + 8) : 0.0f;
            double cx = m[0] * px + m[4] * py + m[8] * pz + m[12];
            double cy = m[1] * px + m[5] * py + m[9] * pz + m[13];
            double cw = m[3] * px + m[7] * py + m[11] * pz + m[15];
            if (cw == 0) cw = 1;
            double nx = cx / cw, ny = cy / cw;
            nx = nx < -1 ? -1 : nx > 1 ? 1 : nx;
            ny = ny < -1 ? -1 : ny > 1 ? 1 : ny;
            x[k] = (nx * 0.5 + 0.5) * vw;
            y[k] = (ny * 0.5 + 0.5) * vh;
        }
        double a = ((x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0])) * 0.5;
        sum += a < 0 ? -a : a;
    }
    (void)lo;
    if (!sprite) g_fillOther += sum;
    else if (plain) g_fillPlain += sum;
    else g_fillSprite += sum;
}

void draw(uint32_t mode, int count, uint32_t indexType, uint32_t indices, int first, bool indexed) {
    if (count <= 0 || !g_program) return;
    // the program in use, found once each time it changes (map nodes stay where they are)
    static uint32_t lastName = 0;
    static Program* lastProgram = 0;
    if (g_program != lastName || !lastProgram) {
        lastProgram = &g_programs[g_program];
        lastName = g_program;
    }
    Program& p = *lastProgram;
    if (!p.linked) return;
    if (!g_alphaTest || g_alphaFunc != GL_GREATER || g_alphaRef != 0.0f)
        log_once("alpha test %s func 0x%x ref %g differs from the converted shaders' GREATER 0", g_alphaTest ? "on" : "off",
                 g_alphaFunc, g_alphaRef);
    VkPrimitiveTopology topo = mode == GL_TRIANGLES ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                             : mode == GL_TRIANGLE_STRIP ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                             : mode == GL_TRIANGLE_FAN ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN : VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    if (topo == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM) {
        log_once("draw mode 0x%x", mode);
        return;
    }
    // the vertex range the indices use (one list kept for every draw: the game's thread alone draws)
    std::vector<uint32_t>& idx = draw_indices();
    if (idx.size() < (size_t)count) idx.resize((size_t)count);
    uint32_t lo = 0xffffffffu, hi = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t v = !indexed ? (uint32_t)(first + i)
                   : indexType == GL_UNSIGNED_SHORT ? RD16(indices + 2 * i)
                   : indexType == GL_UNSIGNED_INT ? RD32(indices + 4 * i) : RD8(indices + i);
        idx[(size_t)i] = v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    uint32_t nverts = hi - lo + 1;
    Frame* f = &g_frames[g_frame];
    VkDeviceSize vbytes = (VkDeviceSize)nverts * p.stride, ibytes = (VkDeviceSize)count * 4;
    if (vbytes > VERTEX_RING || ibytes > INDEX_RING) {
        log_once("a draw of %u vertices does not fit the rings", nverts);
        return;
    }
    // packed textures this draw samples get their images now, outside the pass (an upload ends one)
    if (g_lazyTextures)
        for (size_t u = 0; u < p.uniforms.size(); ++u) {
            if (p.uniforms[u].bytes >= 0) continue;
            uint32_t unit = (uint32_t)p.samplerUnit[p.uniforms[u].offset];
            std::map<uint32_t, Texture>::iterator it = g_textures.find(unit < MAX_UNITS ? g_units[unit] : 0);
            if (it != g_textures.end() && !it->second.img.image && has_pixels(it->second)) materialize(it->second);
        }
    f = &g_frames[g_frame];
    if (f->vOff + vbytes > VERTEX_RING || f->iOff + ibytes > INDEX_RING) flush_wait();
    if (!g_bloomDesc)
        for (size_t i = 0; i < sizeof(GLVK_PROGRAMS) / sizeof(GLVK_PROGRAMS[0]); ++i)
            if (!strcmp(GLVK_PROGRAMS[i].label, "resources/shaders/bloom")) g_bloomDesc = &GLVK_PROGRAMS[i];
    bool bloom = p.desc && p.desc == g_bloomDesc;
    if (bloom) bloom_rows(p, lo);
    pass_area_for_draw();
    if (!ensure_pass(false, false)) return;
    f = &g_frames[g_frame];
    // vertices, packed in the program's attribute order; a program with a plain twin learns on the way whether
    // every vertex has its effects off (Program::plainAt). Float attributes (all of the game's) are copied as bits:
    // a byte-swapped load into a float register goes through memory on the console's PowerPC, a ~40-cycle stall for
    // each of a sprite vertex's 22 floats.
    uint32_t* out = (uint32_t*)(f->vertices.map + f->vOff);
    bool plain = p.fsPlain != VK_NULL_HANDLE;
    for (uint32_t v = 0; v < nverts; ++v) {
        for (size_t a = 0; a < p.attrs.size(); ++a) {
            const Attr& at = p.attrs[a];
            const VertexArray& va = g_va[at.loc];
            uint32_t bits[4] = {0, 0, 0, 0x3f800000u};           // 0, 0, 0, 1
            if (va.enabled) {
                int stride = va.stride ? va.stride : va.size * type_bytes(va.type);
                uint32_t base = va.ptr + (lo + v) * (uint32_t)stride;
                if (va.type == GL_FLOAT) {
                    for (int c = 0; c < va.size && c < 4; ++c) bits[c] = RD32(base + 4 * c);
                } else {
                    for (int c = 0; c < va.size && c < 4; ++c) {
                        float fv = attr_component(va, base, c);
                        memcpy(&bits[c], &fv, 4);
                    }
                }
            }
            if (plain) {
                // on the bits: +-0 is zero; at most 0 is a zero or a negative number (not a NaN)
                #define Z_(b) (((b) & 0x7fffffffu) == 0)
                #define NP_(b) (Z_(b) || ((int32_t)(b) < 0 && ((b) & 0x7fffffffu) <= 0x7f800000u))
                int ai = (int)a;
                if (ai == p.plainAt[0]) plain = Z_(bits[3]);                                      // ColorizeIn.a
                else if (ai == p.plainAt[1]) plain = Z_(bits[0]) && Z_(bits[1]) && Z_(bits[2]);   // ColorOffsetIn
                else if (ai == p.plainAt[2]) plain = NP_(bits[0]);                                // PixelationAmount
                else if (ai == p.plainAt[3]) plain = Z_(bits[0]) && Z_(bits[1]) && NP_(bits[2]);  // ClipPlane
                #undef Z_
                #undef NP_
            }
            for (int c = 0; c < at.comps; ++c) *out++ = bits[c];
            if (g_stats && a == 0) {
                ProgramStats& st = g_programStats[p.desc->label];
                float z;
                memcpy(&z, &bits[2], 4);
                if (z < st.zmin) st.zmin = z;
                if (z > st.zmax) st.zmax = z;
            }
        }
    }
    if (g_samples && gl_frame_count() >= g_samplesFrom) {
        std::vector<ShaderSample>& list = g_shaderSamples[(int)(p.desc - GLVK_PROGRAMS)];
        // the first draws, then one in 64 of the later ones, so the samples span the run
        static std::map<int, uint32_t> seen;
        uint32_t n = seen[(int)(p.desc - GLVK_PROGRAMS)]++;
        if (list.size() < SAMPLE_DRAWS && (n < SAMPLE_DRAWS / 2 || n % 64 == 0)) {
            ShaderSample s;
            s.push.assign((const float*)p.push, (const float*)p.push + (p.pushBytes + 3) / 4);
            const float* packed = (const float*)(f->vertices.map + f->vOff);
            for (int i = 0; i < count && i < SAMPLE_VERTS; ++i) {
                const float* v = packed + (size_t)(idx[(size_t)i] - lo) * (size_t)(p.stride / 4);
                s.verts.insert(s.verts.end(), v, v + p.stride / 4);
            }
            list.push_back(s);
        }
    }
    if (g_stats) {
        ProgramStats& st = g_programStats[p.desc->label];
        char b[96];
        sprintf(b, "%s blend %x,%x,%x,%x depth %s fbo %u", g_blend ? "on" : "off", g_src, g_dst, g_srcA, g_dstA,
                g_depthTest && g_passKey.depth ? "on" : "off", g_fbo);
        st.modes.insert(b);
        ++st.draws;
    }
    bool wide = hi - lo > 0xffff;
    if (wide) {
        uint32_t* ix = (uint32_t*)(f->indices.map + f->iOff);
        for (int i = 0; i < count; ++i) ix[i] = idx[(size_t)i] - lo;
    } else {
        uint16_t* ix = (uint16_t*)(f->indices.map + f->iOff);
        for (int i = 0; i < count; ++i) ix[i] = (uint16_t)(idx[(size_t)i] - lo);
    }
    VkCommandBuffer cmd = f->cmd;
    PipeKey k;
    memset(&k, 0, sizeof(k));
    k.program = (int)g_program;                     // program names are never reused
    k.blend = g_blend;
    if (g_blend) {
        k.src = g_src;
        k.dst = g_dst;
        k.srcA = g_srcA;
        k.dstA = g_dstA;
    }
    k.cull = g_cull;
    k.cullFace = g_cull ? g_cullFace : 0;
    k.frontFace = g_frontFace;
    k.hasDepth = g_passKey.depth != VK_NULL_HANDLE;
    k.depthTest = k.hasDepth && g_depthTest;
    k.depthWrite = k.depthTest && g_depthMask;
    k.depthFunc = k.depthTest ? g_depthFunc : 0x0207;
    // the converted shaders drop a fragment by writing depth 0, which fails only a GREATER test (shaders.py)
    if (!k.depthTest || g_depthFunc != GL_GREATER)
        log_once("a draw without the depth test GREATER (test %s, func 0x%x): its transparent texels are drawn",
                 k.depthTest ? "on" : "off", g_depthFunc);
    k.topology = (uint32_t)topo;
    k.plain = plain;
    if (g_fillStats) add_fill(p, lo, count, plain);
    VkPipeline pipe = pipeline(k, p);
    if (!pipe) return;
    ++g_counts.draws;
    if (p.fsPlain) {
        static uint64_t sprites = 0, plainOnes = 0;
        ++sprites;
        if (plain) ++plainOnes;
        if (sprites == 20000)
            recomp_log("gl_vk: %llu of the first 20000 draws with a plain twin drew plain",
                       (unsigned long long)plainOnes);
    }
    if (pipe != g_boundPipe) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        g_boundPipe = pipe;
    }
    // textures
    std::string texInfo;
    VkImageView views[SAMPLER_SLOTS];
    VkSampler samplers[SAMPLER_SLOTS];
    for (int i = 0; i < SAMPLER_SLOTS; ++i) {
        views[i] = g_dummy.view;
        samplers[i] = sampler_for(GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    }
    for (size_t u = 0; u < p.uniforms.size(); ++u) {
        if (p.uniforms[u].bytes >= 0) continue;
        int binding = p.uniforms[u].offset;
        uint32_t unit = (uint32_t)p.samplerUnit[binding];
        uint32_t tex = unit < MAX_UNITS ? g_units[unit] : 0;
        std::map<uint32_t, Texture>::iterator it = g_textures.find(tex);
        if (it == g_textures.end() || !it->second.img.image) continue;
        if (it->second.img.view == g_passKey.color) log_once("a draw samples the texture it renders to");
        views[binding] = it->second.img.view;
        it->second.drawFrame = gl_frame_count();
        ++it->second.draws;
        samplers[binding] = sampler_for(it->second.minFilter, it->second.magFilter, it->second.wrapS, it->second.wrapT);
        if (bloom && binding == 1) {
            views[1] = g_bloomView;
            samplers[1] = g_bloomSampler;
        }
        if (tracing()) {
            char b[64];
            snprintf(b, sizeof(b), " %d:tex%u %dx%d%s", binding, tex, it->second.img.w, it->second.img.h,
                     it->second.target ? " target" : "");
            texInfo += b;
        }
    }
    if (!g_lastSet || memcmp(views, g_lastViews, sizeof(views)) || memcmp(samplers, g_lastSamplers, sizeof(samplers))) {
        VkDescriptorSetAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = f->pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &g_setLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(g_dev, &ai, &set) != VK_SUCCESS) {
            log_once("descriptor pool exhausted");
            return;
        }
        VkDescriptorImageInfo img[SAMPLER_SLOTS];
        VkWriteDescriptorSet wr[SAMPLER_SLOTS];
        memset(wr, 0, sizeof(wr));
        for (int i = 0; i < SAMPLER_SLOTS; ++i) {
            img[i].sampler = samplers[i];
            img[i].imageView = views[i];
            img[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[i].dstSet = set;
            wr[i].dstBinding = (uint32_t)i;
            wr[i].descriptorCount = 1;
            wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr[i].pImageInfo = &img[i];
        }
        vkUpdateDescriptorSets(g_dev, SAMPLER_SLOTS, wr, 0, 0);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &set, 0, 0);
        g_lastSet = set;
        memcpy(g_lastViews, views, sizeof(views));
        memcpy(g_lastSamplers, samplers, sizeof(samplers));
    }
    if (p.pushBytes)
        vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           (uint32_t)((p.pushBytes + 3) & ~3), p.push);
    VkViewport vp;
    vp.x = (float)g_viewport[0];
    vp.y = (float)g_viewport[1];
    vp.width = (float)g_viewport[2];
    vp.height = (float)g_viewport[3];
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    if (vp.width <= 0 || vp.height <= 0) return;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc;
    sc.offset.x = sc.offset.y = 0;
    sc.extent.width = (uint32_t)g_passW;
    sc.extent.height = (uint32_t)g_passH;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkBuffer vb = f->vertices.buf;
    VkDeviceSize voff = f->vOff;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &voff);
    vkCmdBindIndexBuffer(cmd, f->indices.buf, f->iOff, wide ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    if (tracing()) {
        recomp_log("trace: draw %s%s, fbo %u, %d indices of %u vertices, viewport %d,%d %dx%d, blend %s %x,%x,%x,%x, "
                   "depth test %d write %d func %x, cull %d,%x front %x, textures%s", p.desc ? p.desc->label : "?",
                   plain ? " (plain)" : "", g_fbo,
                   count, nverts, g_viewport[0], g_viewport[1], g_viewport[2], g_viewport[3], k.blend ? "on" : "off", k.src,
                   k.dst, k.srcA, k.dstA, k.depthTest ? 1 : 0, k.depthWrite ? 1 : 0, k.depthFunc, k.cull ? 1 : 0,
                   k.cullFace, k.frontFace, texInfo.empty() ? " none" : texInfo.c_str());
        std::string vals;
        const float* v0 = (const float*)(f->vertices.map + f->vOff);
        for (int i = 0; i < (int)(p.stride / 4) && i < 16; ++i) {
            char b[24];
            snprintf(b, sizeof(b), " %g", v0[i]);
            vals += b;
        }
        recomp_log("trace:   first vertex%s", vals.c_str());
        vals.clear();
        for (int i = 0; i < (int)(p.pushBytes / 4) && i < 24; ++i) {
            char b[24];
            float x;
            memcpy(&x, p.push + 4 * i, 4);
            snprintf(b, sizeof(b), " %g", x);
            vals += b;
        }
        recomp_log("trace:   push%s", vals.c_str());
    }
    vkCmdDrawIndexed(cmd, (uint32_t)count, 1, 0, 0, 0);
    ++g_passDraws;
    if (p.desc && p.desc - GLVK_PROGRAMS < 64) g_effectMask |= 1ull << (p.desc - GLVK_PROGRAMS);
    if (g_gpuTraceSlot == g_frame) {
        char where[64];
        snprintf(where, sizeof(where), " into %dx%d", g_passW, g_passH);
        gpu_stamp(std::string(p.desc ? p.desc->label : "?") + where);
    }
    f->vOff = (f->vOff + vbytes + 15) & ~(VkDeviceSize)15;
    f->iOff = (f->iOff + (wide ? 4 : 2) * (VkDeviceSize)count + 15) & ~(VkDeviceSize)15;
}

// a pass's first clear made again in the pass being recorded, over rects (none: its whole area)
void clear_again(const PassClear& cleared, const VkClearRect* rects, uint32_t n) {
    VkClearAttachment att[2];
    uint32_t k = 0;
    if (cleared.color) {
        att[k].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        att[k].colorAttachment = 0;
        memcpy(att[k].clearValue.color.float32, cleared.rgba, sizeof(cleared.rgba));
        ++k;
    }
    if (cleared.depth && g_passKey.depth) {
        att[k].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (g_depthFormat == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
        att[k].colorAttachment = 0;
        att[k].clearValue.depthStencil.depth = cleared.z;
        att[k].clearValue.depthStencil.stencil = 0;
        ++k;
    }
    if (!k) return;
    VkClearRect whole;
    memset(&whole, 0, sizeof(whole));
    whole.rect.extent.width = (uint32_t)g_passW;
    whole.rect.extent.height = (uint32_t)g_passH;
    whole.layerCount = 1;
    vkCmdClearAttachments(g_frames[g_frame].cmd, k, att, n ? n : 1, n ? rects : &whole);
}

// grows the bound target's area to the viewport's reach; a pass on it already begun smaller ends, and the wider one
// that follows loads what was drawn and clears its new part as the pass's first clear did
void pass_area_for_draw() {
    Target t;
    if (!current_target(t) || g_viewport[2] <= 0 || g_viewport[3] <= 0) return;
    // 8 pixels past the viewport: the passes that sample a target read a few texels beyond the picture (hq4x one,
    // bloom six), which must hold the pass's clear as when passes covered the whole target
    int needW = g_viewport[0] + g_viewport[2] + kAreaGrid, needH = g_viewport[1] + g_viewport[3] + kAreaGrid;
    needW = (needW + kAreaGrid - 1) / kAreaGrid * kAreaGrid;
    needH = (needH + kAreaGrid - 1) / kAreaGrid * kAreaGrid;
    if (needW > t.w) needW = t.w;
    if (needH > t.h) needH = t.h;
    Image& im = *t.color;
    if (im.drawnW >= needW && im.drawnH >= needH) return;
    bool first = !im.drawnW;
    if (needW > im.drawnW) im.drawnW = needW;
    if (needH > im.drawnH) im.drawnH = needH;
    PassKey key = {t.color->view, t.depth ? t.depth->view : VK_NULL_HANDLE};
    if (first || !g_inPass || !(g_passKey == key) || (g_passW >= im.drawnW && g_passH >= im.drawnH)) return;
    int oldW = g_passW, oldH = g_passH;
    PassClear cleared = g_passClear;
    end_pass();
    if (!ensure_pass(false, false)) return;
    g_passClear = cleared;
    VkClearAttachment att[2];
    uint32_t n = 0;
    if (cleared.color) {
        att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        att[n].colorAttachment = 0;
        memcpy(att[n].clearValue.color.float32, cleared.rgba, sizeof(cleared.rgba));
        ++n;
    }
    if (cleared.depth && g_passKey.depth) {
        att[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (g_depthFormat == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
        att[n].colorAttachment = 0;
        att[n].clearValue.depthStencil.depth = cleared.z;
        att[n].clearValue.depthStencil.stencil = 0;
        ++n;
    }
    log_once("gl_vk: a pass grew past its target's drawn area (%dx%d to %dx%d)", oldW, oldH, g_passW, g_passH);
    if (!n) return;
    VkClearRect r[2];
    memset(r, 0, sizeof(r));
    uint32_t rects = 0;
    if (g_passW > oldW) {                           // the new columns, all rows
        r[rects].rect.offset.x = oldW;
        r[rects].rect.extent.width = (uint32_t)(g_passW - oldW);
        r[rects].rect.extent.height = (uint32_t)g_passH;
        r[rects++].layerCount = 1;
    }
    if (g_passH > oldH) {                           // the new rows under the old columns
        r[rects].rect.offset.y = oldH;
        r[rects].rect.extent.width = (uint32_t)(oldW < g_passW ? oldW : g_passW);
        r[rects].rect.extent.height = (uint32_t)(g_passH - oldH);
        r[rects++].layerCount = 1;
    }
    if (rects) vkCmdClearAttachments(g_frames[g_frame].cmd, n, att, rects, r);
}

void clear(uint32_t mask) {
    bool color = (mask & GL_COLOR_BUFFER_BIT) != 0, depth = (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0;
    if (!color && !depth) return;
    // a pass that begins with this clear does not load what it clears (vulkan4xenon: no draw-back)
    if (!ensure_pass(color, depth)) return;
    VkClearAttachment att[2];
    uint32_t n = 0;
    if (color) {
        att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        att[n].colorAttachment = 0;
        memcpy(att[n].clearValue.color.float32, g_clearColor, sizeof(g_clearColor));
        ++n;
    }
    if (depth && g_passKey.depth) {
        ++g_counts.depthClears;
        att[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (g_depthFormat == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
        att[n].colorAttachment = 0;
        att[n].clearValue.depthStencil.depth = g_clearDepth;
        att[n].clearValue.depthStencil.stencil = 0;
        ++n;
    }
    if (!n) return;
    if (tracing())
        recomp_log("trace: clear fbo %u colour %d (%g %g %g %g) depth %d (%g)", g_fbo, color ? 1 : 0, g_clearColor[0],
                   g_clearColor[1], g_clearColor[2], g_clearColor[3], depth && g_passKey.depth ? 1 : 0, g_clearDepth);
    VkClearRect r;
    memset(&r, 0, sizeof(r));
    r.rect.extent.width = (uint32_t)g_passW;
    r.rect.extent.height = (uint32_t)g_passH;
    r.layerCount = 1;
    vkCmdClearAttachments(g_frames[g_frame].cmd, n, att, 1, &r);
    if (color) {
        g_passClear.color = true;
        memcpy(g_passClear.rgba, g_clearColor, sizeof(g_clearColor));
    }
    if (depth && g_passKey.depth) {
        g_passClear.depth = true;
        g_passClear.z = g_clearDepth;
    }
}

bool read_image(Image& im, int x, int y, int w, int h, std::vector<uint8_t>& out);

// copies part of the bound framebuffer's colour into host memory (RGBA rows, GL order)
bool read_color(int x, int y, int w, int h, std::vector<uint8_t>& out) {
    Target t;
    if (!current_target(t)) return false;
    if (x < 0 || y < 0 || x + w > t.color->w || y + h > t.color->h) return false;
    return read_image(*t.color, x, y, w, h, out);
}

// part of a colour image (one not in the pass being recorded: it is ended) as RGBA rows
bool read_image(Image& im, int x, int y, int w, int h, std::vector<uint8_t>& out) {
    if ((VkDeviceSize)w * h * 4 > g_readback.size) {
        flush_wait();                                   // the old buffer may be in recorded copies
        free_buffer(g_readback);
        if (!make_buffer(g_readback, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
            return false;
    }
    begin_recording();
    end_pass();
    barrier(im.image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kAllAccess,
            VK_ACCESS_TRANSFER_READ_BIT, kAllGraphics, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(g_frames[g_frame].cmd, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_readback.buf, 1, &region);
    barrier(im.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT, kAllAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, kAllGraphics);
    flush_wait();
    out.assign(g_readback.map, g_readback.map + (size_t)w * h * 4);
    return true;
}

// ---- GL entry points ----------------------------------------------------------------------------------------
uint32_t guest_const_str(const char* s) {
    static std::map<std::string, uint32_t> cache;
    std::map<std::string, uint32_t>::iterator it = cache.find(s);
    if (it != cache.end()) return it->second;
    uint32_t p = guest_malloc((uint32_t)strlen(s) + 1);
    memcpy(G(p), s, strlen(s) + 1);
    cache[s] = p;
    return p;
}

void gen_names(uint32_t n, uint32_t out) {
    for (uint32_t i = 0; i < n; ++i) WR32(out + 4 * i, g_nextName++);
}

void vk_glGetString(X86* c) {
    uint32_t name = ARG(c, 0);
    std::string ext;
    for (int i = 0; i < kExtensionCount; ++i) ext += std::string(i ? " " : "") + kExtensions[i];
    const char* s = name == GL_VENDOR ? "isaac360" : name == GL_RENDERER ? "vulkan4xenon" : name == GL_VERSION ? "4.6.0 isaac360"
                  : name == GL_SHADING_LANGUAGE_VERSION ? "4.60" : name == GL_EXTENSIONS ? ext.c_str() : "";
    RET_STD(c, 1, guest_const_str(s));
}
void vk_glGetStringi(X86* c) {
    uint32_t i = ARG(c, 1);
    RET_STD(c, 2, ARG(c, 0) == GL_EXTENSIONS && i < (uint32_t)kExtensionCount ? guest_const_str(kExtensions[i]) : 0);
}
void vk_glGetError(X86* c) { RET_STD(c, 0, 0); }
void vk_glGetIntegerv(X86* c) {
    uint32_t pname = ARG(c, 0), out = ARG(c, 1);
    switch (pname) {
    case GL_NUM_EXTENSIONS: WR32(out, (uint32_t)kExtensionCount); break;
    case GL_CONTEXT_FLAGS: WR32(out, 0); break;
    case GL_RESET_NOTIFICATION_STRATEGY: WR32(out, GL_NO_RESET_NOTIFICATION); break;
    case GL_FRAMEBUFFER_BINDING: WR32(out, g_fbo); break;
    case GL_CONTEXT_PROFILE_MASK: WR32(out, 2); break;      // compatibility
    case GL_MAJOR_VERSION: WR32(out, 4); break;
    case GL_MINOR_VERSION: WR32(out, 6); break;
    case GL_MAX_TEXTURE_SIZE: case GL_MAX_RENDERBUFFER_SIZE: WR32(out, 8192); break;
    case GL_MAX_VIEWPORT_DIMS: WR32(out, 8192); WR32(out + 4, 8192); break;
    case GL_MAX_VERTEX_ATTRIBS: case GL_MAX_TEXTURE_IMAGE_UNITS: case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: WR32(out, 16); break;
    case GL_VIEWPORT: for (int i = 0; i < 4; ++i) WR32(out + 4 * i, (uint32_t)g_viewport[i]); break;
    case GL_TEXTURE_BINDING_2D: WR32(out, g_units[g_activeUnit]); break;
    case GL_CURRENT_PROGRAM: WR32(out, g_program); break;
    case GL_ACTIVE_TEXTURE: WR32(out, GL_TEXTURE0 + g_activeUnit); break;
    case GL_UNPACK_ALIGNMENT: case GL_PACK_ALIGNMENT: WR32(out, 4); break;
    default: log_once("glGetIntegerv 0x%x", pname); WR32(out, 0); break;
    }
    RET_STD(c, 2, 0);
}
void vk_glEnable_(X86* c, bool on) {
    switch (ARG(c, 0)) {
    case GL_BLEND: g_blend = on; break;
    case GL_CULL_FACE: g_cull = on; break;
    case GL_DEPTH_TEST: g_depthTest = on; break;
    case GL_ALPHA_TEST: g_alphaTest = on; break;
    default: if (on) log_once("glEnable 0x%x", ARG(c, 0)); break;
    }
    RET_STD(c, 1, 0);
}
void vk_glEnable(X86* c) { vk_glEnable_(c, true); }
void vk_glDisable(X86* c) { vk_glEnable_(c, false); }
void vk_glBlendFuncSeparate(X86* c) {
    g_src = ARG(c, 0);
    g_dst = ARG(c, 1);
    g_srcA = ARG(c, 2);
    g_dstA = ARG(c, 3);
    RET_STD(c, 4, 0);
}
void vk_glBlendFunc(X86* c) {
    g_src = g_srcA = ARG(c, 0);
    g_dst = g_dstA = ARG(c, 1);
    RET_STD(c, 2, 0);
}
void vk_glCullFace(X86* c) { g_cullFace = ARG(c, 0); RET_STD(c, 1, 0); }
void vk_glFrontFace(X86* c) { g_frontFace = ARG(c, 0); RET_STD(c, 1, 0); }
void vk_glDepthFunc(X86* c) { g_depthFunc = ARG(c, 0); RET_STD(c, 1, 0); }
void vk_glDepthMask(X86* c) { g_depthMask = ARG(c, 0) != 0; RET_STD(c, 1, 0); }
void vk_glAlphaFunc(X86* c) { g_alphaFunc = ARG(c, 0); g_alphaRef = ARGF(c, 1); RET_STD(c, 2, 0); }
void vk_glClampColorARB(X86* c) { RET_STD(c, 2, 0); }
void vk_glClearColor(X86* c) {
    for (int i = 0; i < 4; ++i) g_clearColor[i] = ARGF(c, i);
    RET_STD(c, 4, 0);
}
void vk_glClearDepth(X86* c) {
    // GL clamps to 0..1 (the game asks for -1000); Vulkan requires it
    double d = ARGD(c, 0);
    g_clearDepth = (float)(d < 0.0 ? 0.0 : d > 1.0 ? 1.0 : d);
    if (g_stats) g_clearDepths.insert(g_clearDepth);
    RET_STD(c, 2, 0);
}
void vk_glClear(X86* c) { clear(ARG(c, 0)); RET_STD(c, 1, 0); }
void vk_glViewport(X86* c) {
    for (int i = 0; i < 4; ++i) g_viewport[i] = (int)ARG(c, i);
    RET_STD(c, 4, 0);
}
void vk_glFlush(X86* c) { RET_STD(c, 0, 0); }

void vk_glGenTextures(X86* c) { gen_names(ARG(c, 0), ARG(c, 1)); RET_STD(c, 2, 0); }
Texture& texture(uint32_t name) {
    std::map<uint32_t, Texture>::iterator it = g_textures.find(name);
    if (it != g_textures.end()) return it->second;
    Texture& t = g_textures[name];
    memset(&t.img, 0, sizeof(t.img));
    t.minFilter = 0x2702;                   // GL_NEAREST_MIPMAP_LINEAR
    t.magFilter = GL_LINEAR;
    t.wrapS = t.wrapT = GL_REPEAT;
    t.target = t.data = false;
    t.uploadFrame = t.format = 0;
    t.drawFrame = t.draws = t.packed = 0;
    t.gen = 0;
    t.lw = t.lh = 0;
    t.solid = false;
    return t;
}

void vk_glDeleteTextures(X86* c) {
    uint32_t n = ARG(c, 0), names = ARG(c, 1);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t name = RD32(names + 4 * i);
        std::map<uint32_t, Texture>::iterator it = g_textures.find(name);
        if (it == g_textures.end()) continue;
        if (g_inPass && it->second.img.view == g_passKey.color) end_pass();
        if (!pool_put(it->second)) retire_image(it->second.img);
        drop_packed(it->second);
        g_textures.erase(it);
        for (int u = 0; u < MAX_UNITS; ++u)
            if (g_units[u] == name) g_units[u] = 0;
    }
    RET_STD(c, 2, 0);
}
void vk_glActiveTexture(X86* c) { g_activeUnit = (ARG(c, 0) - GL_TEXTURE0) & (MAX_UNITS - 1); RET_STD(c, 1, 0); }
void vk_glBindTexture(X86* c) {
    uint32_t name = ARG(c, 1);
    if (name) texture(name);
    g_units[g_activeUnit] = name;
    RET_STD(c, 2, 0);
}
void vk_glTexParameteri(X86* c) {
    uint32_t pname = ARG(c, 1), v = ARG(c, 2);
    Texture& t = texture(g_units[g_activeUnit]);
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: t.minFilter = v; break;
    case GL_TEXTURE_MAG_FILTER: t.magFilter = v; break;
    case GL_TEXTURE_WRAP_S: t.wrapS = v; break;
    case GL_TEXTURE_WRAP_T: t.wrapT = v; break;
    default: log_once("glTexParameteri 0x%x 0x%x", pname, v); break;
    }
    RET_STD(c, 3, 0);
}

// guest pixels -> RGBA8 rows (GL_UNPACK_ALIGNMENT 4)
bool to_rgba(uint32_t format, uint32_t type, int w, int h, uint32_t src, std::vector<uint8_t>& out) {
    int comps = format == GL_RGBA || format == GL_BGRA ? 4 : format == GL_RGB ? 3 : format == GL_LUMINANCE_ALPHA ? 2 : 1;
    if (type != GL_UNSIGNED_BYTE) {
        log_once("texture type 0x%x", type);
        return false;
    }
    out.resize((size_t)w * h * 4);
    int pitch = (w * comps + 3) & ~3;
    if (format == GL_RGBA) {                            // the game's textures: rows as they are
        for (int y = 0; y < h; ++y) memcpy(&out[(size_t)y * w * 4], g_mem + src + (size_t)y * pitch, (size_t)w * 4);
        return true;
    }
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = g_mem + src + (size_t)y * pitch;
        uint8_t* d = &out[(size_t)y * w * 4];
        for (int x = 0; x < w; ++x, s += comps, d += 4) {
            switch (format) {
            case GL_RGBA: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; break;
            case GL_BGRA: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; break;
            case GL_RGB: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
            case GL_LUMINANCE: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
            case GL_LUMINANCE_ALPHA: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
            case GL_ALPHA: d[0] = d[1] = d[2] = 0; d[3] = s[0]; break;
            default: log_once("texture format 0x%x", format); return false;
            }
        }
    }
    return true;
}

// glTexImage2D(target, level, internalformat, w, h, border, format, type, pixels)
void vk_glTexImage2D(X86* c) {
    struct Timed {
        WaitClock::time_point t0;
        ~Timed() { g_texMs += since(t0); }
    } timed = {WaitClock::now()};
    uint32_t level = ARG(c, 1), format = ARG(c, 6), type = ARG(c, 7), pixels = ARG(c, 8);
    int w = (int)ARG(c, 3), h = (int)ARG(c, 4);
    // ISAAC360_TEX_LOG=N: the first N uploads with pixels, their caller and whether the image cache named them
    static int texLog = getenv("ISAAC360_TEX_LOG") ? atoi(getenv("ISAAC360_TEX_LOG")) : 0;
    if (texLog > 0 && pixels) {
        --texLog;
        recomp_log("tex: frame %u, %dx%d format 0x%x type 0x%x, %s, from %08x %08x", gl_frame_count(), w, h, format, type,
                   g_next.buf == pixels ? "named" : "not named", RD32(c->r[4]), RD32(c->r[4] + 0x40));
    }
    RET_STD(c, 9, 0);
    if (level != 0) {
        log_once("glTexImage2D level %u", level);
        return;
    }
    Texture& t = texture(g_units[g_activeUnit]);
    t.data = pixels != 0;
    t.format = format;
    t.uploadFrame = gl_frame_count();
    drop_packed(t);
    if (w <= 0 || h <= 0) return;
    if (pixels) g_texBytes += (uint64_t)w * h * 4;
    if (pixels && take_named(g_units[g_activeUnit], t, pixels, format, type, w, h)) return;
    std::vector<uint8_t> rgba;
    uint8_t one[4];
    bool solid = pixels && format == GL_RGBA && type == GL_UNSIGNED_BYTE && solid_rgba(pixels, w, h, one);
    if (pixels && g_lazyTextures && !t.target) {
        // packed until a draw samples it (materialize); a GPU image it had goes
        t.lw = w;
        t.lh = h;
        t.packed = 0;
        if (solid) {
            t.solid = true;
            memcpy(t.solidRgba, one, 4);
        } else {
            if (!to_rgba(format, type, w, h, pixels, rgba)) rgba.assign((size_t)w * h * 4, 0);
            pack_later(g_units[g_activeUnit], t, rgba);
        }
        if (t.img.image) {
            if (g_inPass && t.img.view == g_passKey.color) end_pass();
            retire_image(t.img);
            g_lastSet = VK_NULL_HANDLE;
        }
        return;
    }
    bool fresh = false;
    if (!t.img.image || t.img.w != w || t.img.h != h) {
        if (g_inPass && t.img.view && (t.img.view == g_passKey.color)) end_pass();
        retire_image(t.img);
        if (!make_image(t.img, w, h, (VkFormat)COLOR_FORMAT, false)) return;
        fresh = true;
    }
    if (!pixels || solid) {                 // render targets: GL leaves them undefined, drivers zero them
        static const uint8_t zero[4] = {0, 0, 0, 0};
        clear_image(t.img, pixels ? one : zero, fresh);
        return;
    }
    if (!to_rgba(format, type, w, h, pixels, rgba)) rgba.assign((size_t)w * h * 4, 0);
    upload_rgba(t.img, 0, 0, w, h, &rgba[0], fresh);
}
// glTexSubImage2D(target, level, x, y, w, h, format, type, pixels)
void vk_glTexSubImage2D(X86* c) {
    int x = (int)ARG(c, 2), y = (int)ARG(c, 3), w = (int)ARG(c, 4), h = (int)ARG(c, 5);
    uint32_t format = ARG(c, 6), type = ARG(c, 7), pixels = ARG(c, 8);
    RET_STD(c, 9, 0);
    Texture& t = texture(g_units[g_activeUnit]);
    materialize(t);
    drop_packed(t);                                      // its pixels change: the image is the only copy now
    std::vector<uint8_t> rgba;
    if (!t.img.image || w <= 0 || h <= 0 || !pixels || !to_rgba(format, type, w, h, pixels, rgba)) return;
    upload_rgba(t.img, x, y, w, h, &rgba[0], false);
}

void vk_glGenFramebuffers(X86* c) {
    uint32_t n = ARG(c, 0), out = ARG(c, 1);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t name = g_nextName++;
        g_fbos[name].color = g_fbos[name].depth = 0;
        WR32(out + 4 * i, name);
    }
    RET_STD(c, 2, 0);
}
void vk_glBindFramebuffer(X86* c) { g_fbo = ARG(c, 1); RET_STD(c, 2, 0); }
void vk_glCheckFramebufferStatus(X86* c) { RET_STD(c, 1, GL_FRAMEBUFFER_COMPLETE); }
// glFramebufferTexture2D(target, attachment, textarget, texture, level)
void vk_glFramebufferTexture2D(X86* c) {
    if (ARG(c, 1) == GL_COLOR_ATTACHMENT0) {
        g_fbos[g_fbo].color = ARG(c, 3);
        if (ARG(c, 3)) {
            Texture& t = texture(ARG(c, 3));
            t.target = true;
            materialize(t);                              // drawn into: the image is the only copy from now on
            drop_packed(t);
        }
    }
    else log_once("glFramebufferTexture2D attachment 0x%x", ARG(c, 1));
    RET_STD(c, 5, 0);
}
// glFramebufferRenderbuffer(target, attachment, rbtarget, renderbuffer)
void vk_glFramebufferRenderbuffer(X86* c) {
    uint32_t a = ARG(c, 1);
    if (a == GL_DEPTH_ATTACHMENT || a == GL_DEPTH_STENCIL_ATTACHMENT || a == GL_STENCIL_ATTACHMENT) g_fbos[g_fbo].depth = ARG(c, 3);
    else log_once("glFramebufferRenderbuffer attachment 0x%x", a);
    RET_STD(c, 4, 0);
}
void vk_glDeleteFramebuffers(X86* c) {
    uint32_t n = ARG(c, 0), names = ARG(c, 1);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t name = RD32(names + 4 * i);
        g_fbos.erase(name);
        if (g_fbo == name) g_fbo = 0;
    }
    RET_STD(c, 2, 0);
}
void vk_glGenRenderbuffers(X86* c) { gen_names(ARG(c, 0), ARG(c, 1)); RET_STD(c, 2, 0); }
void vk_glBindRenderbuffer(X86* c) { g_rb = ARG(c, 1); RET_STD(c, 2, 0); }
// glRenderbufferStorage(target, internalformat, w, h): depth formats only (the game's one renderbuffer)
void vk_glRenderbufferStorage(X86* c) {
    uint32_t format = ARG(c, 1);
    int w = (int)ARG(c, 2), h = (int)ARG(c, 3);
    RET_STD(c, 4, 0);
    Renderbuffer& rb = g_renderbuffers[g_rb];
    if (g_inPass) end_pass();
    retire_image(rb.img);
    for (std::map<std::pair<int, int>, Image>::iterator it = rb.sized.begin(); it != rb.sized.end(); ++it)
        retire_image(it->second);
    rb.sized.clear();
    rb.format = format;
    rb.w = w;
    rb.h = h;
}
void vk_glGetRenderbufferParameteriv(X86* c) {
    uint32_t pname = ARG(c, 1), out = ARG(c, 2);
    Renderbuffer& rb = g_renderbuffers[g_rb];
    WR32(out, pname == GL_RENDERBUFFER_WIDTH ? (uint32_t)rb.w : pname == GL_RENDERBUFFER_HEIGHT ? (uint32_t)rb.h
              : pname == GL_RENDERBUFFER_INTERNAL_FORMAT ? rb.format : 0);
    RET_STD(c, 3, 0);
}
void vk_glDeleteRenderbuffers(X86* c) {
    uint32_t n = ARG(c, 0), names = ARG(c, 1);
    for (uint32_t i = 0; i < n; ++i) {
        std::map<uint32_t, Renderbuffer>::iterator it = g_renderbuffers.find(RD32(names + 4 * i));
        if (it == g_renderbuffers.end()) continue;
        if (g_inPass) end_pass();
        retire_image(it->second.img);
        for (std::map<std::pair<int, int>, Image>::iterator d = it->second.sized.begin(); d != it->second.sized.end(); ++d)
            retire_image(d->second);
        g_renderbuffers.erase(it);
    }
    RET_STD(c, 2, 0);
}

// Single-pixel reads without waiting for the GPU. The game samples the screen under some entities (camouflage takes
// the floor's colour) a few times a second, and each read made the GPU finish everything queued, mid-frame: 6-8% of
// every room's frame on the console (boot 18), and the GPU's frame then no longer overlapped the next. Each read now
// queues a copy of its pixel in the frame's commands, and is answered by the newest finished copy of that target
// within 160 pixels and 60 frames (the game reads every ~10 frames, while a large room's camera scrolls): the colour
// as the screen held it at an earlier read, which the game blends into a tint over many frames anyway. A read with
// none near (the first) still waits. ISAAC360_SYNC_READS=1 waits always.
struct PixelResult {
    VkImage image;
    int x, y;
    uint32_t frame;
    uint8_t rgba[4];
};
std::vector<PixelResult> g_pixelResults;            // the newest, at most 64

void keep_pixel_result(VkImage image, int x, int y, const uint8_t* rgba) {
    PixelResult r = {image, x, y, gl_frame_count(), {rgba[0], rgba[1], rgba[2], rgba[3]}};
    for (size_t i = 0; i < g_pixelResults.size(); ++i)
        if (g_pixelResults[i].image == image && g_pixelResults[i].x == x && g_pixelResults[i].y == y) {
            g_pixelResults[i] = r;
            return;
        }
    if (g_pixelResults.size() >= 64) g_pixelResults.erase(g_pixelResults.begin());
    g_pixelResults.push_back(r);
}

void take_pixel_reads(Frame& f) {
    for (size_t i = 0; i < f.pixelReads.size(); ++i)
        keep_pixel_result(f.pixelReads[i].image, f.pixelReads[i].x, f.pixelReads[i].y, f.reads.map + 4 * i);
    f.pixelReads.clear();
}

// queues a copy of the pixel and answers from the nearest recent one; false: none near
bool read_pixel_queued(int x, int y, uint8_t* rgba) {
    Target t;
    if (!current_target(t) || x < 0 || y < 0 || x >= t.color->w || y >= t.color->h) return false;
    Frame& f = g_frames[g_frame];
    if (f.reads.map && f.pixelReads.size() < PIXEL_READS) {
        begin_recording();
        end_pass();
        Image& im = *t.color;
        barrier(im.image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                kAllAccess, VK_ACCESS_TRANSFER_READ_BIT, kAllGraphics, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region;
        memset(&region, 0, sizeof(region));
        region.bufferOffset = 4 * f.pixelReads.size();
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageOffset.x = x;
        region.imageOffset.y = y;
        region.imageExtent.width = region.imageExtent.height = region.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(g_frames[g_frame].cmd, im.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.reads.buf, 1,
                               &region);
        barrier(im.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, kAllAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, kAllGraphics);
        PixelRead r = {im.image, x, y};
        f.pixelReads.push_back(r);
    }
    const PixelResult* best = 0;
    int bestD = 161;
    uint32_t now = gl_frame_count();
    for (size_t i = 0; i < g_pixelResults.size(); ++i) {
        const PixelResult& r = g_pixelResults[i];
        if (r.image != t.color->image || now - r.frame > 60) continue;
        int d = abs(r.x - x) + abs(r.y - y);
        if (d < bestD) {
            bestD = d;
            best = &r;
        }
    }
    if (!best) return false;
    memcpy(rgba, best->rgba, 4);
    return true;
}

// glReadPixels(x, y, w, h, format, type, pixels): from the bound framebuffer, GL rows
void vk_glReadPixels(X86* c) {
    int x = (int)ARG(c, 0), y = (int)ARG(c, 1), w = (int)ARG(c, 2), h = (int)ARG(c, 3);
    uint32_t format = ARG(c, 4), type = ARG(c, 5), out = ARG(c, 6);
    if (tracing()) {
        uint32_t ret[6] = {0, 0, 0, 0, 0, 0};
        recomp_guest_returns(c, ret, 6);
        recomp_log("trace: glReadPixels %d,%d %dx%d fbo %u format %x, called from %08x %08x %08x %08x %08x %08x", x, y, w, h,
                   g_fbo, format, ret[0], ret[1], ret[2], ret[3], ret[4], ret[5]);
    }
    RET_STD(c, 7, 0);
    ++g_readPixels;
    static bool named = false;                      // the first caller, to tell what reads the GPU back
    if (!named) {
        named = true;
        uint32_t ret[6] = {0, 0, 0, 0, 0, 0};
        recomp_guest_returns(c, ret, 6);
        recomp_log("gl_vk: the first glReadPixels: %d,%d %dx%d, fbo %u, from %08x %08x %08x %08x %08x %08x", x, y, w, h,
                   g_fbo, ret[0], ret[1], ret[2], ret[3], ret[4], ret[5]);
    }
    std::vector<uint8_t> px;
    if (type != GL_UNSIGNED_BYTE || (format != GL_RGBA && format != GL_RGB && format != GL_BGRA)) {
        log_once("glReadPixels format 0x%x type 0x%x", format, type);
        return;
    }
    static const bool sync = getenv("ISAAC360_SYNC_READS") != 0;
    uint8_t one[4];
    if (w == 1 && h == 1 && !sync && read_pixel_queued(x, y, one)) {
        px.assign(one, one + 4);
    } else {
        if (!read_color(x, y, w, h, px)) {
            log_once("glReadPixels %d,%d %dx%d outside the framebuffer", x, y, w, h);
            return;
        }
        ++g_readPixelsWaited;
        Target t;
        if (w == 1 && h == 1 && current_target(t)) keep_pixel_result(t.color->image, x, y, &px[0]);
    }
    int comps = format == GL_RGB ? 3 : 4, pitch = (w * comps + 3) & ~3;
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i) {
            const uint8_t* s = &px[((size_t)r * w + i) * 4];
            uint8_t* d = g_mem + out + (size_t)r * pitch + (size_t)i * comps;
            d[0] = format == GL_BGRA ? s[2] : s[0];
            d[1] = s[1];
            d[2] = format == GL_BGRA ? s[0] : s[2];
            if (comps == 4) d[3] = s[3];
        }
}

void vk_glCreateShader(X86* c) {
    uint32_t name = g_nextName++;
    ShaderObj& s = g_shaderObjs[name];
    s.type = ARG(c, 0);
    s.hash = 0;
    s.known = false;
    RET_STD(c, 1, name);
}
// glShaderSource(shader, count, strings, lengths)
void vk_glShaderSource(X86* c) {
    uint32_t shader = ARG(c, 0), count = ARG(c, 1), strs = ARG(c, 2), lens = ARG(c, 3);
    std::string src;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t s = RD32(strs + 4 * i);
        int32_t l = lens ? (int32_t)RD32(lens + 4 * i) : -1;
        src += l < 0 ? guest_str(s) : std::string((const char*)(g_mem + s), (size_t)l);
    }
    ShaderObj& o = g_shaderObjs[shader];
    o.hash = fnv64(src);
    o.known = false;
    for (size_t i = 0; i < sizeof(GLVK_PROGRAMS) / sizeof(GLVK_PROGRAMS[0]); ++i)
        if (GLVK_PROGRAMS[i].vsHash == o.hash || GLVK_PROGRAMS[i].fsHash == o.hash) o.known = true;
    if (!o.known) recomp_log("gl_vk: shader %016llx was not converted (%u bytes)", (unsigned long long)o.hash, (unsigned)src.size());
    RET_STD(c, 4, 0);
}
void vk_glCompileShader(X86* c) { RET_STD(c, 1, 0); }
void vk_glGetShaderiv(X86* c) {
    uint32_t pname = ARG(c, 1), out = ARG(c, 2);
    ShaderObj& s = g_shaderObjs[ARG(c, 0)];
    WR32(out, pname == GL_COMPILE_STATUS ? (s.known ? 1 : 0) : pname == GL_SHADER_TYPE ? s.type : 0);
    RET_STD(c, 3, 0);
}
void vk_glDeleteShader(X86* c) { RET_STD(c, 1, 0); }
// the game deletes its programs only as it shuts down (boot 14's quit); their pipelines live as long as the layer
void vk_glDeleteProgram(X86* c) { RET_STD(c, 1, 0); }
void vk_glDetachShader(X86* c) { RET_STD(c, 2, 0); }
void vk_glCreateProgram(X86* c) {
    uint32_t name = g_nextName++;
    Program& p = g_programs[name];
    p.nshaders = 0;
    p.desc = 0;
    p.linked = false;
    p.vs = p.fs = p.fsPlain = VK_NULL_HANDLE;
    for (int i = 0; i < 4; ++i) p.plainAt[i] = -1;
    RET_STD(c, 0, name);
}
void vk_glAttachShader(X86* c) {
    Program& p = g_programs[ARG(c, 0)];
    if (p.nshaders < 2) p.shaders[p.nshaders++] = ARG(c, 1);
    RET_STD(c, 2, 0);
}
void vk_glLinkProgram(X86* c) { link(g_programs[ARG(c, 0)]); RET_STD(c, 1, 0); }
void vk_glGetProgramiv(X86* c) {
    uint32_t pname = ARG(c, 1), out = ARG(c, 2);
    Program& p = g_programs[ARG(c, 0)];
    WR32(out, pname == GL_LINK_STATUS ? (p.linked ? 1 : 0) : 0);
    RET_STD(c, 3, 0);
}
void vk_glUseProgram(X86* c) { g_program = ARG(c, 0); RET_STD(c, 1, 0); }
// the guest's NUL-ended name at a against a program's: the game looks its attributes up by name around every draw
// (1.6% of a console frame in a room of many objects, boot 18, each a host string made and freed)
bool guest_name_is(uint32_t a, const std::string& name) {
    if (!a || a + name.size() >= g_mem_size) return false;
    return !memcmp(g_mem + a, name.data(), name.size()) && !g_mem[a + name.size()];
}
// The game asks for each attribute's location before every draw (~300 a frame, 1.1% of the console's main thread in
// boot 20), by names among its own constant strings: answers for names inside isaac-ng.exe's image are kept by
// (program, name address, uniform or not), and dropped whenever a program links.
struct LocCacheEntry {
    uint32_t program, name;
    int loc;
};
LocCacheEntry g_locCache[256];
bool cacheable_name(uint32_t name) { return name >= 0x00401000u && name < 0x00c00000u; }
void clear_loc_cache() { memset(g_locCache, 0, sizeof(g_locCache)); }
LocCacheEntry& loc_slot(uint32_t program, uint32_t name, bool uniform) {
    return g_locCache[((name >> 2) ^ (program * 31) ^ (uniform ? 128u : 0u)) & 255];
}
void vk_glGetAttribLocation(X86* c) {
    uint32_t prog = ARG(c, 0), name = ARG(c, 1);
    LocCacheEntry& e = loc_slot(prog, name, false);
    if (e.program == prog && e.name == name && prog) {
        RET_STD(c, 2, (uint32_t)e.loc);
        return;
    }
    Program& p = g_programs[prog];
    int loc = -1;
    for (size_t i = 0; i < p.attrs.size(); ++i)
        if (guest_name_is(name, p.attrs[i].name)) loc = p.attrs[i].loc;
    if (cacheable_name(name) && p.linked) {
        e.program = prog;
        e.name = name;
        e.loc = loc;
    }
    RET_STD(c, 2, (uint32_t)loc);
}
void vk_glGetUniformLocation(X86* c) {
    uint32_t prog = ARG(c, 0), name = ARG(c, 1);
    LocCacheEntry& e = loc_slot(prog, name, true);
    if (e.program == prog && e.name == name && prog) {
        RET_STD(c, 2, (uint32_t)e.loc);
        return;
    }
    Program& p = g_programs[prog];
    int loc = -1;
    for (size_t i = 0; i < p.uniforms.size(); ++i)
        if (guest_name_is(name, p.uniforms[i].name)) loc = (int)i;
    if (cacheable_name(name) && p.linked) {
        e.program = prog;
        e.name = name;
        e.loc = loc;
    }
    RET_STD(c, 2, (uint32_t)loc);
}
Uniform* uniform_at(int loc) {
    if (!g_program || loc < 0) return 0;
    Program& p = g_programs[g_program];
    return (size_t)loc < p.uniforms.size() ? &p.uniforms[(size_t)loc] : 0;
}
void vk_glUniform1i(X86* c) {
    int loc = (int)ARG(c, 0);
    uint32_t v = ARG(c, 1);
    RET_STD(c, 2, 0);
    Uniform* u = uniform_at(loc);
    if (!u) return;
    Program& p = g_programs[g_program];
    if (u->bytes < 0) p.samplerUnit[u->offset] = (int)v;
    else memcpy(p.push + u->offset, &v, 4);
}
void uniform_floats(int loc, uint32_t src, int n) {
    Uniform* u = uniform_at(loc);
    if (!u || u->bytes < 0) return;
    Program& p = g_programs[g_program];
    if (n * 4 > u->bytes) n = u->bytes / 4;
    for (int i = 0; i < n; ++i) {
        float f = RDF(src + 4 * i);
        memcpy(p.push + u->offset + 4 * i, &f, 4);
    }
}
// glUniformMatrix4fv(location, count, transpose, value)
void vk_glUniformMatrix4fv(X86* c) {
    int loc = (int)ARG(c, 0);
    uint32_t count = ARG(c, 1), transpose = ARG(c, 2), src = ARG(c, 3);
    RET_STD(c, 4, 0);
    if (transpose) {
        Uniform* u = uniform_at(loc);
        if (!u || u->bytes < 64) return;
        Program& p = g_programs[g_program];
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float f = RDF(src + 4 * (row * 4 + col));
                memcpy(p.push + u->offset + 4 * (col * 4 + row), &f, 4);
            }
        return;
    }
    uniform_floats(loc, src, 16 * (int)count);
}
void vk_glUniform1f(X86* c) {
    Uniform* u = uniform_at((int)ARG(c, 0));
    if (u && u->bytes >= 4) {
        float f = ARGF(c, 1);
        memcpy(g_programs[g_program].push + u->offset, &f, 4);
    }
    RET_STD(c, 2, 0);
}
void vk_glUniform4fv(X86* c) { uniform_floats((int)ARG(c, 0), ARG(c, 2), 4 * (int)ARG(c, 1)); RET_STD(c, 3, 0); }
void vk_glEnableVertexAttribArray(X86* c) { uint32_t i = ARG(c, 0); if (i < MAX_ATTRIBS) g_va[i].enabled = true; RET_STD(c, 1, 0); }
void vk_glDisableVertexAttribArray(X86* c) { uint32_t i = ARG(c, 0); if (i < MAX_ATTRIBS) g_va[i].enabled = false; RET_STD(c, 1, 0); }
// glVertexAttribPointer(index, size, type, normalized, stride, pointer): client memory (no buffer is ever bound)
void vk_glVertexAttribPointer(X86* c) {
    uint32_t i = ARG(c, 0);
    if (i < MAX_ATTRIBS) {
        g_va[i].size = (int)ARG(c, 1);
        g_va[i].type = ARG(c, 2);
        g_va[i].normalized = ARG(c, 3) != 0;
        g_va[i].stride = (int)ARG(c, 4);
        g_va[i].ptr = ARG(c, 5);
    }
    RET_STD(c, 6, 0);
}
// glDrawElements(mode, count, type, indices)
void vk_glDrawElements(X86* c) {
    uint32_t mode = ARG(c, 0), count = ARG(c, 1), type = ARG(c, 2), indices = ARG(c, 3);
    RET_STD(c, 4, 0);
    draw(mode, (int)count, type, indices, 0, true);
}
// glDrawArrays(mode, first, count)
void vk_glDrawArrays(X86* c) {
    uint32_t mode = ARG(c, 0), first = ARG(c, 1), count = ARG(c, 2);
    RET_STD(c, 3, 0);
    draw(mode, (int)count, 0, 0, (int)first, false);
}

struct Entry { const char* name; ShimFn fn; };
const Entry kEntries[] = {
    {"glGetString", vk_glGetString}, {"glGetStringi", vk_glGetStringi}, {"glGetError", vk_glGetError},
    {"glGetIntegerv", vk_glGetIntegerv}, {"glEnable", vk_glEnable}, {"glDisable", vk_glDisable},
    {"glBlendFuncSeparate", vk_glBlendFuncSeparate}, {"glBlendFunc", vk_glBlendFunc}, {"glCullFace", vk_glCullFace},
    {"glFrontFace", vk_glFrontFace}, {"glDepthFunc", vk_glDepthFunc}, {"glDepthMask", vk_glDepthMask},
    {"glAlphaFunc", vk_glAlphaFunc}, {"glClampColorARB", vk_glClampColorARB}, {"glClampColor", vk_glClampColorARB},
    {"glClearColor", vk_glClearColor}, {"glClearDepth", vk_glClearDepth}, {"glClear", vk_glClear},
    {"glViewport", vk_glViewport}, {"glFlush", vk_glFlush}, {"glFinish", vk_glFlush},
    {"glGenTextures", vk_glGenTextures}, {"glDeleteTextures", vk_glDeleteTextures}, {"glActiveTexture", vk_glActiveTexture},
    {"glBindTexture", vk_glBindTexture}, {"glTexParameteri", vk_glTexParameteri}, {"glTexImage2D", vk_glTexImage2D},
    {"glTexSubImage2D", vk_glTexSubImage2D}, {"glGenFramebuffers", vk_glGenFramebuffers},
    {"glBindFramebuffer", vk_glBindFramebuffer}, {"glCheckFramebufferStatus", vk_glCheckFramebufferStatus},
    {"glFramebufferTexture2D", vk_glFramebufferTexture2D}, {"glFramebufferRenderbuffer", vk_glFramebufferRenderbuffer},
    {"glDeleteFramebuffers", vk_glDeleteFramebuffers}, {"glGenRenderbuffers", vk_glGenRenderbuffers},
    {"glBindRenderbuffer", vk_glBindRenderbuffer}, {"glRenderbufferStorage", vk_glRenderbufferStorage},
    {"glGetRenderbufferParameteriv", vk_glGetRenderbufferParameteriv}, {"glDeleteRenderbuffers", vk_glDeleteRenderbuffers},
    {"glReadPixels", vk_glReadPixels}, {"glCreateShader", vk_glCreateShader}, {"glShaderSource", vk_glShaderSource},
    {"glCompileShader", vk_glCompileShader}, {"glGetShaderiv", vk_glGetShaderiv}, {"glDeleteShader", vk_glDeleteShader},
    {"glDeleteProgram", vk_glDeleteProgram}, {"glDetachShader", vk_glDetachShader},
    {"glCreateProgram", vk_glCreateProgram}, {"glAttachShader", vk_glAttachShader}, {"glLinkProgram", vk_glLinkProgram},
    {"glGetProgramiv", vk_glGetProgramiv}, {"glUseProgram", vk_glUseProgram}, {"glGetAttribLocation", vk_glGetAttribLocation},
    {"glGetUniformLocation", vk_glGetUniformLocation}, {"glUniform1i", vk_glUniform1i}, {"glUniform1f", vk_glUniform1f},
    {"glUniform4fv", vk_glUniform4fv}, {"glUniformMatrix4fv", vk_glUniformMatrix4fv},
    {"glEnableVertexAttribArray", vk_glEnableVertexAttribArray}, {"glDisableVertexAttribArray", vk_glDisableVertexAttribArray},
    {"glVertexAttribPointer", vk_glVertexAttribPointer}, {"glDrawElements", vk_glDrawElements}, {"glDrawArrays", vk_glDrawArrays},
};

// the traced frame's stamps, once its fence has passed: each one's time since the one before, charged to what it
// follows, summed by program and target and logged most first
void gpu_trace_report() {
    size_t n = g_gpuTraceLabels.size() + 1;
    std::vector<uint64_t> t(n);
    g_gpuTraceSlot = -1;
    if (vkGetQueryPoolResults(g_dev, g_queries, 2 * FRAMES, (uint32_t)n, n * sizeof(uint64_t), &t[0], sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS)
        return;
    std::map<std::string, std::pair<double, int> > by;
    double total = 0;
    for (size_t i = 1; i < n; ++i) {
        double ms = t[i] >= t[i - 1] ? (double)(t[i] - t[i - 1]) * g_tickNs / 1e6 : 0;
        total += ms;
        std::pair<double, int>& e = by[g_gpuTraceLabels[i - 1]];
        e.first += ms;
        ++e.second;
    }
    std::vector<std::pair<double, std::string> > order;
    for (std::map<std::string, std::pair<double, int> >::iterator it = by.begin(); it != by.end(); ++it) {
        char b[24];
        snprintf(b, sizeof(b), " (%d)", it->second.second);
        order.push_back(std::make_pair(it->second.first, it->first + b));
    }
    std::sort(order.rbegin(), order.rend());
    recomp_log("gpu trace of frame %u: %.2f ms over %u draws, each stamped after the GPU finished what came before",
               g_gpuTraceFrame, total, (unsigned)(n - 1));
    for (size_t i = 0; i < order.size() && i < 12; ++i)
        recomp_log("gpu trace: %.2f ms %s", order[i].first, order[i].second.c_str());
}

void next_frame() {
    g_frame = (g_frame + 1) % FRAMES;
    Frame& f = g_frames[g_frame];
    WaitClock::time_point t0 = WaitClock::now();
    vkWaitForFences(g_dev, 1, &f.fence, VK_TRUE, ~0ull);
    g_waitFence += since(t0);
    vkResetFences(g_dev, 1, &f.fence);
    if (g_queries && g_frameTimed[g_frame] && !g_frameFlushed[g_frame]) {
        uint64_t t[2];
        if (vkGetQueryPoolResults(g_dev, g_queries, (uint32_t)(2 * g_frame), 2, sizeof(t), t, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && t[1] >= t[0]) {
            double ms = (double)(t[1] - t[0]) * g_tickNs / 1e6;
            g_gpuMs += ms;
            for (int i = 0; i < 64; ++i)
                if ((g_frameMask[g_frame] >> i & 1) && g_effects[i].on) {
                    g_effects[i].gpuMs += ms;
                    ++g_effects[i].timed;
                }
            if (ms > g_gpuMax) g_gpuMax = ms;
            ++g_gpuFrames;
        }
    }
    if (g_gpuTraceSlot == g_frame) gpu_trace_report();
    take_pixel_reads(f);
    free_garbage(f);
    vkResetCommandBuffer(f.cmd, 0);
    vkResetDescriptorPool(g_dev, f.pool, 0);
    ring_high(f.vOff, f.iOff, f.uOff);
    f.vOff = f.iOff = f.uOff = 0;
    g_recording = false;
    begin_recording();
    g_frameTimed[g_frame] = g_queries != VK_NULL_HANDLE;
    g_frameFlushed[g_frame] = false;
    if (g_queries) {
        vkCmdResetQueryPool(f.cmd, g_queries, (uint32_t)(2 * g_frame), 2);
        vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_queries, (uint32_t)(2 * g_frame));
        if (g_gpuTraceNext && g_gpuTraceSlot < 0) {
            g_gpuTraceNext = false;
            g_gpuTraceSlot = g_frame;
            g_gpuTraceFrame = gl_frame_count();
            g_gpuTraceLabels.clear();
            vkCmdResetQueryPool(f.cmd, g_queries, 2 * FRAMES, GPU_TRACE_MAX);
            vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_queries, 2 * FRAMES);
        }
    }
}

}  // namespace

void gl_vk_configure(const GlVkHooks& hooks) {
    g_hooks = hooks;
    g_enabled = hooks.getInstanceProcAddr != 0;
    // settings from the environment, read here: the console sets them from isaac360_args.txt after static start-up
    const char* v;
    g_stats = getenv("ISAAC360_GL_STATS") != 0;
    g_samples = (v = getenv("ISAAC360_SHADER_SAMPLES")) != 0;
    g_samplesFrom = v ? (uint32_t)atoi(v) : 0;
    for (const char* t = getenv("ISAAC360_TRACE_FRAME"); t && *t;) {
        g_traceFrames.insert((uint32_t)atoi(t));
        while (*t && *t != ',') ++t;
        if (*t == ',') ++t;
    }
    g_lazyTextures = !(v = getenv("ISAAC360_TEX_LAZY")) || atoi(v) != 0;
    g_fillStats = (v = getenv("ISAAC360_FILL_STATS")) && atoi(v) != 0;
    g_textureBudget = (uint64_t)((v = getenv("ISAAC360_TEX_BUDGET")) ? atoi(v) : 96) << 20;
}

bool gl_vk_enabled() { return g_enabled; }

uint64_t gl_vk_image_bytes() { return g_imageBytes; }
uint64_t gl_vk_buffer_bytes() { return g_bufferBytes; }

void gl_vk_work(GlVkWork* w) {
    w->imagesMade = g_imagesMade;
    w->uploaded = g_uploaded;
    w->waitAcquire = g_waitAcquire;
    w->waitFence = g_waitFence;
    w->waitIdle = g_waitIdle;
    w->texMs = g_texMs;
    w->materializeMs = g_materializeMs;
    w->texBytes = g_texBytes;
    w->gpuMs = g_gpuMs;
    w->gpuFrames = g_gpuFrames;
    w->readPixels = g_readPixels;
    w->readPixelsWaited = g_readPixelsWaited;
    w->reused = g_poolReused;
    w->fillSprite = g_fillSprite;
    w->fillPlain = g_fillPlain;
    w->fillOther = g_fillOther;
    w->pipesMade = g_pipesMade;
    w->pipeMs = g_pipeMs;
}

void gl_vk_next_pixels(uint32_t buf, uint32_t bytes, const std::string& key,
                       const std::shared_ptr<const std::vector<uint8_t> >& packed, bool filled) {
    g_next.buf = buf;
    g_next.bytes = bytes;
    g_next.key = key;
    g_next.packed = packed;
    g_next.filled = filled;
    g_next.frame = gl_frame_count();
}
double gl_vk_gpu_max_reset() {
    double m = g_gpuMax;
    g_gpuMax = 0;
    return m;
}
void gl_vk_gpu_trace_next() { g_gpuTraceNext = g_queries != VK_NULL_HANDLE; }
uint64_t gl_vk_packed_bytes() { return g_packedBytes; }

void write_shader_samples() {
    FILE* f = fopen("build/recomp/shader_samples.txt", "w");
    if (!f) return;
    for (std::map<int, std::vector<ShaderSample> >::iterator it = g_shaderSamples.begin(); it != g_shaderSamples.end(); ++it) {
        const GlVkProgramDesc& d = GLVK_PROGRAMS[it->first];
        fprintf(f, "P %d %s\nA %s\n", it->first, d.label, d.attribs);
        for (size_t i = 0; i < it->second.size(); ++i) {
            const ShaderSample& s = it->second[i];
            fputs("U", f);
            for (size_t k = 0; k < s.push.size(); ++k) fprintf(f, " %.9g", s.push[k]);
            fputs("\nV", f);
            for (size_t k = 0; k < s.verts.size(); ++k) fprintf(f, " %.9g", s.verts[k]);
            fputs("\n", f);
        }
    }
    fclose(f);
}

void gl_vk_write_stats() {
    if (g_samples) write_shader_samples();
    if (!g_stats) return;
    FILE* f = fopen((recomp_out_dir() + "/gl_stats.txt").c_str(), "w");
    if (!f) return;
    for (std::set<float>::iterator it = g_clearDepths.begin(); it != g_clearDepths.end(); ++it) fprintf(f, "clear depth %g\n", *it);
    for (std::map<std::string, ProgramStats>::iterator it = g_programStats.begin(); it != g_programStats.end(); ++it) {
        fprintf(f, "%s: %llu draws, z %g..%g\n", it->first.c_str(), (unsigned long long)it->second.draws, it->second.zmin,
                it->second.zmax);
        for (std::set<std::string>::iterator m = it->second.modes.begin(); m != it->second.modes.end(); ++m)
            fprintf(f, "    %s\n", m->c_str());
    }
    fclose(f);
}

// ISAAC360_TEX_LIST: the live textures (name, size, format of the last upload) to build/recomp/textures.txt
void gl_vk_write_texture_list() {
    char path[64];
    sprintf(path, "/textures_%05u.txt", gl_frame_count());
    FILE* f = fopen((recomp_out_dir() + path).c_str(), "w");
    if (!f) return;
    for (std::map<uint32_t, Texture>::iterator it = g_textures.begin(); it != g_textures.end(); ++it)
        if (it->second.img.image || has_pixels(it->second))
            fprintf(f, "%u %dx%d %u KB %s%s fmt %x frame %u drawn %u last %u packed %u KB\n", it->first,
                    it->second.img.image ? it->second.img.w : it->second.lw, it->second.img.image ? it->second.img.h : it->second.lh,
                    (unsigned)(it->second.img.bytes >> 10),
                    it->second.target ? "target " : "", it->second.data ? "data" : "empty", it->second.format,
                    it->second.uploadFrame, it->second.draws, it->second.drawFrame, it->second.packed >> 10);
    fclose(f);
}

ShimFn gl_vk_shim(const std::string& name) {
    if (!g_enabled) return 0;
    for (size_t i = 0; i < sizeof(kEntries) / sizeof(kEntries[0]); ++i)
        if (name == kEntries[i].name) return kEntries[i].fn;
    return 0;
}

bool gl_vk_create_context() {
    if (g_ready) return true;
    if (!g_enabled) return false;
    memset(g_units, 0, sizeof(g_units));
    memset(g_va, 0, sizeof(g_va));
    if (!make_device() || !make_frames() || !make_output()) {
        recomp_log("gl_vk: Vulkan set-up failed");
        return false;
    }
    // the first frame's fence starts signalled: reset it for the frame being recorded
    vkResetFences(g_dev, 1, &g_frames[g_frame].fence);
    begin_recording();
    g_ready = true;
    return true;
}

static bool read_bgra(Image& im, std::vector<uint8_t>& bgra, int& w, int& h) {
    std::vector<uint8_t> rgba;
    if (!read_image(im, 0, 0, im.w, im.h, rgba)) return false;
    w = im.w;
    h = im.h;
    bgra.resize(rgba.size());
    for (size_t i = 0; i < rgba.size(); i += 4) {
        bgra[i] = rgba[i + 2];
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i];
        bgra[i + 3] = rgba[i + 3];
    }
    return true;
}

void gl_vk_wait_idle() {
    if (g_ready) vkQueueWaitIdle(g_queue);
}

bool gl_vk_read_frame(std::vector<uint8_t>& bgra, int& w, int& h) {
    if (!g_ready) return false;
    ensure_default_framebuffer();
    return g_defColor.image && read_bgra(g_defColor, bgra, w, h);
}

std::vector<uint32_t> gl_vk_targets() {
    std::vector<uint32_t> names;
    for (std::map<uint32_t, Texture>::iterator it = g_textures.begin(); it != g_textures.end(); ++it)
        if (it->second.target && it->second.img.image) names.push_back(it->first);
    return names;
}

bool gl_vk_read_texture(uint32_t name, std::vector<uint8_t>& bgra, int& w, int& h) {
    std::map<uint32_t, Texture>::iterator it = g_textures.find(name);
    return g_ready && it != g_textures.end() && it->second.img.image && read_bgra(it->second.img, bgra, w, h);
}

void gl_vk_frame_counts(char* buf, size_t size) {
    snprintf(buf, size, "%u passes, %u draws, %u depth clears; rings at most %u/%u/%u KB", g_lastCounts.passes,
             g_lastCounts.draws, g_lastCounts.depthClears, (unsigned)(g_ringHigh[0] >> 10), (unsigned)(g_ringHigh[1] >> 10),
             (unsigned)(g_ringHigh[2] >> 10));
}

void log_effect(int i) {
    EffectRun& e = g_effects[i];
    recomp_log("effect %s: frames %u to %u (drawn in %u), the GPU's work %.1f ms a frame (%u timed)",
               GLVK_PROGRAMS[i].label, e.first, e.last, e.frames, e.timed ? e.gpuMs / e.timed : 0.0, e.timed);
}

void effects_frame() {
    static const bool on = getenv("ISAAC360_PROFILE") || getenv("ISAAC360_EFFECT_LOG");
    uint64_t mask = g_effectMask;
    g_effectMask = 0;
    g_frameMask[g_frame] = mask;
    if (!on) return;
    uint32_t now = gl_frame_count();
    for (int i = 0; i < 64; ++i) {
        EffectRun& e = g_effects[i];
        if (mask >> i & 1) {
            if (!e.on) {
                memset(&e, 0, sizeof(e));
                e.on = true;
                e.first = now;
            }
            e.last = now;
            ++e.frames;
        } else if (e.on && now - e.last > (uint32_t)kEffectGap) {
            if (e.first > 3) log_effect(i);         // not the programs drawn since the start
            e.on = false;
        }
    }
}

void gl_vk_present() {
    if (!g_ready) return;
    effects_frame();
    g_lastCounts = g_counts;
    memset(&g_counts, 0, sizeof(g_counts));
    begin_recording();
    end_pass();
    collect_packed();
    evict_textures();
    Frame& f = g_frames[g_frame];
    uint32_t image = 0;
    WaitClock::time_point t0 = WaitClock::now();
    bool show = g_swapchain && g_defColor.image &&
                vkAcquireNextImageKHR(g_dev, g_swapchain, ~0ull, f.acquired, VK_NULL_HANDLE, &image) == VK_SUCCESS;
    g_waitAcquire += since(t0);
    if (show) {
        VkRenderPassBeginInfo rp;
        memset(&rp, 0, sizeof(rp));
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = render_pass(false, false, false, true);
        rp.framebuffer = g_swapFbs[image];
        rp.renderArea.extent = g_swapExtent;
        vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        // the picture keeps its aspect: letterboxed when the window's shape differs from the output's
        float sx = (float)g_swapExtent.width / g_defColor.w, sy = (float)g_swapExtent.height / g_defColor.h;
        float s = sx < sy ? sx : sy;
        VkViewport vp;
        vp.width = g_defColor.w * s;
        vp.height = g_defColor.h * s;
        vp.x = (g_swapExtent.width - vp.width) * 0.5f;
        vp.y = (g_swapExtent.height - vp.height) * 0.5f;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        VkRect2D sc = {{0, 0}, g_swapExtent};
        // black bars only where the picture leaves some (the pass begins DONT_CARE)
        if (vp.width < g_swapExtent.width - 0.5f || vp.height < g_swapExtent.height - 0.5f) {
            VkClearAttachment att;
            memset(&att, 0, sizeof(att));
            att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            VkClearRect cr;
            memset(&cr, 0, sizeof(cr));
            cr.rect = sc;
            cr.layerCount = 1;
            vkCmdClearAttachments(f.cmd, 1, &att, 1, &cr);
        }
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_presentWhole ? g_presentCopyPipe : g_presentPipe);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_presentLayout, 0, 1, &g_presentSet, 0, 0);
        // each output pixel the area-weighted average of the frame's pixels under it, or ISAAC360_PRESENT_FILTER=doubled
        // (shaders.py PRESENT_FS)
        static const float doubled = getenv("ISAAC360_PRESENT_FILTER") && !strcmp(getenv("ISAAC360_PRESENT_FILTER"), "doubled")
                                         ? 1.0f : 0.0f;
        const float pc[8] = {(float)g_defColor.w, (float)g_defColor.h, 1.0f / g_defColor.w, 1.0f / g_defColor.h,
                             g_defColor.w / vp.width, g_defColor.h / vp.height, doubled, 0.0f};
        vkCmdPushConstants(f.cmd, g_presentLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
        vkCmdSetViewport(f.cmd, 0, 1, &vp);
        vkCmdSetScissor(f.cmd, 0, 1, &sc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(f.cmd, 0, 1, &g_presentQuad.buf, &off);
        vkCmdDraw(f.cmd, 6, 1, 0, 0);
        vkCmdEndRenderPass(f.cmd);
        if (g_gpuTraceSlot == g_frame) {
            char where[64];
            snprintf(where, sizeof(where), "the present into %ux%u (and the pass ends before it)", g_swapExtent.width,
                     g_swapExtent.height);
            gpu_stamp(where);
        }
    }
    if (g_queries && g_frameTimed[g_frame])
        vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_queries, (uint32_t)(2 * g_frame + 1));
    vkEndCommandBuffer(f.cmd);
    g_recording = false;
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    if (show) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f.acquired;
        si.pWaitDstStageMask = &wait;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &g_swapDone[image];
    }
    vkQueueSubmit(g_queue, 1, &si, f.fence);
    if (show) {
        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &g_swapDone[image];
        pi.swapchainCount = 1;
        pi.pSwapchains = &g_swapchain;
        pi.pImageIndices = &image;
        vkQueuePresentKHR(g_queue, &pi);
    }
    next_frame();
}
