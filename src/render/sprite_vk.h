#ifndef ISAAC360_SPRITE_VK_H
#define ISAAC360_SPRITE_VK_H
// The one renderer (docs/XSDK_PLAN.md): Vulkan, kept to what vulkan4xenon supports, so the Windows binary and
// the console draw a PortDrawList the same way. The platform hands it the Vulkan loader and, unless headless,
// a surface; headless runs draw only the frames a test reads back.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#include "../app/port_draw.h"

struct PortVkHooks {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    const char* surfaceExtension;   // 0 when headless
    VkResult (*createSurface)(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, VkSurfaceKHR* out);
    bool validation;                // VK_LAYER_KHRONOS_validation when installed
    PortVkHooks() : getInstanceProcAddr(0), surfaceExtension(0), createSurface(0), validation(false) {}
};

class PortRenderer {
public:
    virtual ~PortRenderer() {}
    // Replaces a texture slot with RGBA8 pixels (top row first).
    virtual bool SetTexture(int slot, int w, int h, const unsigned char* rgba) = 0;
    // Draws a frame to the display (when there is one) and keeps the list for ReadPixels.
    virtual void Draw(const PortDrawList& list) = 0;
    virtual void Present() = 0;
    // The last list drawn again offscreen and read back as RGBA, top row first (tests).
    virtual bool ReadPixels(unsigned char* rgba) = 0;
    // Windowed test runs that save shots keep a copy of each list (headless runs always do).
    virtual void KeepLastFrame(bool keep) = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
};

PortRenderer* PortCreateRenderer(const PortVkHooks& hooks, int width, int height);

#endif
