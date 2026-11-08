// The game's OpenGL drawn with Vulkan (gl_vk.cpp): the console's renderer through vulkan4xenon, and on the PC
// (BOI360_GL=vk) the same code checked against the host driver's pass-through.
#ifndef BOI360_RECOMP_GL_VK_H
#define BOI360_RECOMP_GL_VK_H
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#include <stdint.h>
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
uint64_t gl_vk_packed_bytes();      // host memory held by packed texture pixels (gl_vk.cpp, lazy textures)
void gl_vk_write_texture_list();    // build/recomp/textures.txt
void gl_vk_write_stats();           // build/recomp/gl_stats.txt (BOI360_GL_STATS)
ShimFn gl_vk_shim(const std::string& name);
bool gl_vk_create_context();
void gl_vk_present();
// the default framebuffer as BGRA rows, bottom row first (screenshots)
bool gl_vk_read_frame(std::vector<uint8_t>& bgra, int& w, int& h);
std::vector<uint32_t> gl_vk_targets();     // the textures the game renders to
bool gl_vk_read_texture(uint32_t name, std::vector<uint8_t>& bgra, int& w, int& h);
void gl_vk_frame_counts(char* buf, size_t size);   // the last frame's passes and draws (frame lines)

#endif
