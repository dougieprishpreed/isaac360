// The game's OpenGL drawn with Vulkan (gl_vk.cpp): the console's renderer through vulkan4xenon, and on the PC
// (ISAAC360_GL=vk) the same code checked against the host driver's pass-through.
#ifndef ISAAC360_RECOMP_GL_VK_H
#define ISAAC360_RECOMP_GL_VK_H
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>
#include "runtime.h"

struct GlVkHooks {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    const char* surfaceExtension;      // 0: headless (frames are only read back)
    VkResult (*createSurface)(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, VkSurfaceKHR* out);
    bool validation;
};

// one program the game can link, converted by tools/recomp/shaders.py (build/recomp/gl_shaders.inc)
struct GlVkProgramDesc {
    uint64_t vsHash, fsHash;           // FNV-1a 64 of the sources the game hands glShaderSource
    const uint32_t* vs;
    uint32_t vsWords;
    const uint32_t* fs;
    uint32_t fsWords;
    const char* attribs;               // "name:location:components,..."
    const char* samplers;              // "name:binding,..."
    const char* uniforms;              // "name:offset:bytes,..." in the push constants
    const char* label;
};

void gl_vk_configure(const GlVkHooks& hooks);   // the platform, before recomp_run; turns the layer on
bool gl_vk_enabled();
uint64_t gl_vk_image_bytes();       // device memory held by images
uint64_t gl_vk_buffer_bytes();      // and by buffers (vertex, index, upload rings, read-backs)
// what the frames did so far, summed: images made, bytes uploaded, ms waited for the display (acquiring an image),
// for a frame's GPU work (its fence, two frames on) and for the GPU to finish (read-backs, full rings)
struct GlVkWork {
    uint32_t imagesMade;
    uint64_t uploaded;
    double waitAcquire, waitFence, waitIdle;
    double texMs, materializeMs;    // in glTexImage2D (converting and packing), making packed textures' images
    uint64_t texBytes;              // pixels glTexImage2D was given (as RGBA)
    double gpuMs;                   // the GPU's work on the frames timed (first to last command), and how many
    uint32_t gpuFrames;
    uint64_t readPixels;            // glReadPixels calls
    uint64_t readPixelsWaited;      // those that waited for the GPU
    uint64_t reused;                // textures given the image a file's earlier load had (the reload pool)
    double fillSprite, fillPlain, fillOther;   // ISAAC360_FILL_STATS: pixels the draws covered (sprites with their
                                               // effects, sprites drawn plain, the rest)
};
void gl_vk_work(GlVkWork* w);
// hle_image.cpp: the next glTexImage2D given the guest buffer buf (bytes long) gets the pixels of the image file key,
// whose packed copy (lz.h, RGBA rows) is packed; filled: buf holds them too (false: a cached load left it unwritten,
// for the texture to take the packed copy or the file's earlier image). buf 0 clears it.
void gl_vk_next_pixels(uint32_t buf, uint32_t bytes, const std::string& key,
                       const std::shared_ptr<const std::vector<uint8_t> >& packed, bool filled);
double gl_vk_gpu_max_reset();       // the longest frame of GPU work since the last call
void gl_vk_gpu_trace_next();        // ISAAC360_PROFILE: stamp the next frame's draws one by one (gpu trace lines)
uint64_t gl_vk_packed_bytes();      // host memory held by packed texture pixels (gl_vk.cpp, lazy textures)
void gl_vk_write_texture_list();    // build/recomp/textures.txt
void gl_vk_write_stats();           // build/recomp/gl_stats.txt (ISAAC360_GL_STATS)
ShimFn gl_vk_shim(const std::string& name);
bool gl_vk_create_context();
void gl_vk_present();
void gl_vk_wait_idle();              // the GPU done with everything submitted (before a quit)
// the default framebuffer as BGRA rows, bottom row first (screenshots)
bool gl_vk_read_frame(std::vector<uint8_t>& bgra, int& w, int& h);
std::vector<uint32_t> gl_vk_targets();     // the textures the game renders to
bool gl_vk_read_texture(uint32_t name, std::vector<uint8_t>& bgra, int& w, int& h);
void gl_vk_frame_counts(char* buf, size_t size);   // the last frame's passes and draws (frame lines)

#endif
