// Vulkan sprite renderer (sprite_vk.h), adapted from crab360's vulkan4xenon RHI. What it keeps to:
// * One pass a frame, straight into the swapchain image: the game draws 8-bit sprites with ordinary alpha
//   blending, so there is no linear scene to encode, and on Xenos every extra full-screen pass is an eDRAM
//   resolve and a draw-back.
// * The pass begins DONT_CARE and is cleared with vkCmdClearAttachments: on Xenos a pass that begins with
//   CLEAR on a target the size of the one before resolves that one again.
// * The view transform is a vec4 in the push constants (xost miscompiles a mat4 outside a block member).
// * Vertices go into a ring per frame in flight; textures upload once through a staging buffer that is only
//   copied from (TRANSFER_SRC), which vulkan4xenon backs with its cached memory type.
// * Colours are UNORM bytes in memory order and textures UNORM RGBA8, as the original D3D9 game drew them.
#include "sprite_vk.h"
#include "../app/port_app.h"
#include <stddef.h>
#include <string.h>
#include <vector>

namespace {

typedef unsigned int u32;
typedef unsigned char u8;

#include "shaders/spirv.inc"

#define BOI_VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceLayerProperties)
#define BOI_VK_INSTANCE(X)                                                                                        \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                           \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice)          \
    X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR)                         \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)
#define BOI_VK_DEVICE(X)                                                                                          \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkCreateBuffer) X(vkDestroyBuffer)                       \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage)                     \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)              \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule)                      \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline)       \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer)                  \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool)                     \
    X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)                             \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkFreeCommandBuffers)           \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence)     \
    X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCmdBeginRenderPass)       \
    X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers)              \
    X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw) X(vkCmdClearAttachments)           \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCreateSwapchainKHR)          \
    X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR)

#define BOI_VK_DECLARE(name) PFN_##name name = 0;
BOI_VK_GLOBAL(BOI_VK_DECLARE)
BOI_VK_INSTANCE(BOI_VK_DECLARE)
BOI_VK_DEVICE(BOI_VK_DECLARE)

enum {
    FRAMES = 2,
    VERTEX_RING = 4 << 20,              // bytes a frame: ~100,000 vertices
    MAX_SWAP_IMAGES = 4,
    OUTPUT_FORMAT = VK_FORMAT_B8G8R8A8_UNORM
};

struct Buffer {
    VkBuffer buf;
    VkDeviceMemory mem;
    u8* map;
    VkDeviceSize size;
    Buffer() : buf(VK_NULL_HANDLE), mem(VK_NULL_HANDLE), map(0), size(0) {}
};

struct Texture {
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    VkDescriptorSet set;
    int w, h;
    Texture() : image(VK_NULL_HANDLE), mem(VK_NULL_HANDLE), view(VK_NULL_HANDLE), set(VK_NULL_HANDLE), w(0), h(0) {}
};

struct Frame {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    Buffer vertices;
    Frame() : cmd(VK_NULL_HANDLE), fence(VK_NULL_HANDLE), acquired(VK_NULL_HANDLE) {}
};

class SpriteVk : public PortRenderer {
public:
    SpriteVk(const PortVkHooks& hooks, int w, int h);
    ~SpriteVk();
    bool Init();
    bool SetTexture(int slot, int w, int h, const unsigned char* rgba);
    void Draw(const PortDrawList& list);
    void Present();
    bool ReadPixels(unsigned char* rgba);
    int Width() const { return m_w; }
    int Height() const { return m_h; }
    void KeepLastFrame(bool keep) { m_keepLast = keep; }

private:
    bool MakeInstance();
    bool PickDevice();
    bool MakeDevice();
    bool MakeFrames();
    bool MakePipeline();
    bool MakeSwapchain();
    void DestroySwapchain();
    int FindMemory(u32 typeBits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags want) const;
    bool MakeBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags need, VkMemoryPropertyFlags want);
    void FreeBuffer(Buffer& b);
    void FreeTexture(Texture& t);
    VkCommandBuffer BeginOneShot();
    void EndOneShot(VkCommandBuffer cmd);
    void Record(VkCommandBuffer cmd, Buffer& vertices, VkRenderPass pass, VkFramebuffer fb, VkExtent2D extent);

    PortVkHooks m_hooks;
    int m_w, m_h;
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_gpu;
    VkPhysicalDeviceMemoryProperties m_memProps;
    u32 m_queueFamily;
    VkDevice m_device;
    VkQueue m_queue;
    VkCommandPool m_pool;
    Frame m_frames[FRAMES];
    int m_frame;
    VkRenderPass m_outputPass, m_readbackPass;
    VkDescriptorSetLayout m_setLayout;
    VkDescriptorPool m_descPool;
    VkPipelineLayout m_layout;
    VkPipeline m_pipeline;
    VkSampler m_sampler;
    Texture m_tex[PT_COUNT];
    VkSwapchainKHR m_swapchain;
    VkExtent2D m_swapExtent;
    u32 m_swapCount;
    VkImage m_swapImages[MAX_SWAP_IMAGES];
    VkImageView m_swapViews[MAX_SWAP_IMAGES];
    VkFramebuffer m_swapFbs[MAX_SWAP_IMAGES];
    VkSemaphore m_swapDone[MAX_SWAP_IMAGES];
    int m_presentImage;
    VkImage m_readImage;
    VkDeviceMemory m_readMem;
    VkImageView m_readView;
    VkFramebuffer m_readFb;
    Buffer m_readBuffer, m_readVertices;
    PortDrawList m_last;
    const PortDrawList* m_recording;
    bool m_ringFullLogged, m_keepLast;
};

SpriteVk::SpriteVk(const PortVkHooks& hooks, int w, int h)
    : m_hooks(hooks), m_w(w), m_h(h), m_instance(VK_NULL_HANDLE), m_surface(VK_NULL_HANDLE), m_gpu(VK_NULL_HANDLE),
      m_queueFamily(0), m_device(VK_NULL_HANDLE), m_queue(VK_NULL_HANDLE), m_pool(VK_NULL_HANDLE), m_frame(0),
      m_outputPass(VK_NULL_HANDLE), m_readbackPass(VK_NULL_HANDLE), m_setLayout(VK_NULL_HANDLE), m_descPool(VK_NULL_HANDLE),
      m_layout(VK_NULL_HANDLE), m_pipeline(VK_NULL_HANDLE), m_sampler(VK_NULL_HANDLE), m_swapchain(VK_NULL_HANDLE),
      m_swapCount(0), m_presentImage(-1), m_readImage(VK_NULL_HANDLE), m_readMem(VK_NULL_HANDLE), m_readView(VK_NULL_HANDLE),
      m_readFb(VK_NULL_HANDLE), m_recording(0), m_ringFullLogged(false), m_keepLast(false) {
    memset(&m_memProps, 0, sizeof(m_memProps));
    memset(m_swapImages, 0, sizeof(m_swapImages));
    memset(m_swapViews, 0, sizeof(m_swapViews));
    memset(m_swapFbs, 0, sizeof(m_swapFbs));
    memset(m_swapDone, 0, sizeof(m_swapDone));
    m_swapExtent.width = m_swapExtent.height = 0;
    m_last.Clear(0xff000000);
}

bool SpriteVk::MakeInstance() {
    PFN_vkGetInstanceProcAddr gipa = m_hooks.getInstanceProcAddr;
#define BOI_VK_LOAD_GLOBAL(name) name = (PFN_##name)gipa(VK_NULL_HANDLE, #name);
    BOI_VK_GLOBAL(BOI_VK_LOAD_GLOBAL)
    if (!vkCreateInstance) return false;
    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "boi360";
    app.pEngineName = "boi360";
    app.apiVersion = VK_API_VERSION_1_0;
    const char* exts[2];
    u32 extCount = 0;
    if (m_hooks.surfaceExtension) {
        exts[extCount++] = VK_KHR_SURFACE_EXTENSION_NAME;
        exts[extCount++] = m_hooks.surfaceExtension;
    }
    const char* layer = "VK_LAYER_KHRONOS_validation";
    bool validation = false;
    if (m_hooks.validation && vkEnumerateInstanceLayerProperties) {
        u32 n = 0;
        vkEnumerateInstanceLayerProperties(&n, 0);
        std::vector<VkLayerProperties> props(n);
        if (n) vkEnumerateInstanceLayerProperties(&n, &props[0]);
        for (u32 i = 0; i < n; ++i)
            if (strcmp(props[i].layerName, layer) == 0) validation = true;
        if (!validation) PortLog("vk: the validation layer isn't installed");
    }
    VkInstanceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extCount;
    ci.ppEnabledExtensionNames = extCount ? exts : 0;
    ci.enabledLayerCount = validation ? 1 : 0;
    ci.ppEnabledLayerNames = validation ? &layer : 0;
    VkResult r = vkCreateInstance(&ci, 0, &m_instance);
    if (r != VK_SUCCESS) {
        PortLog("vk: vkCreateInstance failed (%d)", (int)r);
        return false;
    }
#define BOI_VK_LOAD_INSTANCE(name) name = (PFN_##name)gipa(m_instance, #name);
    BOI_VK_INSTANCE(BOI_VK_LOAD_INSTANCE)
    if (validation) PortLog("vk: validation on");
    if (m_hooks.createSurface) {
        r = m_hooks.createSurface(m_instance, gipa, &m_surface);
        if (r != VK_SUCCESS) {
            PortLog("vk: surface creation failed (%d)", (int)r);
            return false;
        }
    }
    return true;
}

bool SpriteVk::PickDevice() {
    u32 n = 0;
    vkEnumeratePhysicalDevices(m_instance, &n, 0);
    if (!n) {
        PortLog("vk: no GPU");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(m_instance, &n, &gpus[0]);
    int best = -1, bestScore = -1;
    u32 bestFamily = 0;
    for (u32 i = 0; i < n; ++i) {
        u32 qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, 0);
        std::vector<VkQueueFamilyProperties> qs(qn);
        if (qn) vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, &qs[0]);
        for (u32 q = 0; q < qn; ++q) {
            if (!(qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (m_surface) {
                VkBool32 ok = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, m_surface, &ok);
                if (!ok) continue;
            }
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(gpus[i], &p);
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > bestScore) {
                best = (int)i;
                bestScore = score;
                bestFamily = q;
            }
            break;
        }
    }
    if (best < 0) {
        PortLog("vk: no GPU can draw%s", m_surface ? " and present" : "");
        return false;
    }
    m_gpu = gpus[best];
    m_queueFamily = bestFamily;
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(m_gpu, &p);
    PortLog("vk: %s, Vulkan %u.%u.%u", p.deviceName, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
            VK_VERSION_PATCH(p.apiVersion));
    vkGetPhysicalDeviceMemoryProperties(m_gpu, &m_memProps);
    return true;
}

bool SpriteVk::MakeDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi;
    memset(&qi, 0, sizeof(qi));
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = m_queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = m_surface ? 1 : 0;
    ci.ppEnabledExtensionNames = m_surface ? &ext : 0;
    VkResult r = vkCreateDevice(m_gpu, &ci, 0, &m_device);
    if (r != VK_SUCCESS) {
        PortLog("vk: vkCreateDevice failed (%d)", (int)r);
        return false;
    }
#define BOI_VK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(m_device, #name);
    BOI_VK_DEVICE(BOI_VK_LOAD_DEVICE)
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

int SpriteVk::FindMemory(u32 typeBits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags want) const {
    int fallback = -1;
    for (u32 i = 0; i < m_memProps.memoryTypeCount; ++i) {
        if (!(typeBits & (1u << i))) continue;
        VkMemoryPropertyFlags f = m_memProps.memoryTypes[i].propertyFlags;
        if ((f & need) != need) continue;
        if ((f & want) == want) return (int)i;
        if (fallback < 0) fallback = (int)i;
    }
    return fallback;
}

bool SpriteVk::MakeBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags need,
                          VkMemoryPropertyFlags want) {
    VkBufferCreateInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, 0, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, b.buf, &req);
    int type = FindMemory(req.memoryTypeBits, need, want);
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (u32)type;
    if (type < 0 || vkAllocateMemory(m_device, &ai, 0, &b.mem) != VK_SUCCESS) {
        PortLog("vk: no memory for a %u-byte buffer", (u32)size);
        vkDestroyBuffer(m_device, b.buf, 0);
        b.buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    b.size = size;
    b.map = 0;
    if (need & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void* p = 0;
        vkMapMemory(m_device, b.mem, 0, VK_WHOLE_SIZE, 0, &p);
        b.map = (u8*)p;
    }
    return true;
}

void SpriteVk::FreeBuffer(Buffer& b) {
    if (b.buf) vkDestroyBuffer(m_device, b.buf, 0);
    if (b.mem) vkFreeMemory(m_device, b.mem, 0);
    b = Buffer();
}

void SpriteVk::FreeTexture(Texture& t) {
    if (t.view) vkDestroyImageView(m_device, t.view, 0);
    if (t.image) vkDestroyImage(m_device, t.image, 0);
    if (t.mem) vkFreeMemory(m_device, t.mem, 0);
    VkDescriptorSet set = t.set;          // sets are kept: one a slot, rewritten on replacement
    t = Texture();
    t.set = set;
}

bool SpriteVk::MakeFrames() {
    VkCommandPoolCreateInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = m_queueFamily;
    if (vkCreateCommandPool(m_device, &pi, 0, &m_pool) != VK_SUCCESS) return false;
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (int i = 0; i < FRAMES; ++i) {
        Frame& f = m_frames[i];
        VkCommandBufferAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_device, &ai, &f.cmd) != VK_SUCCESS) return false;
        VkFenceCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(m_device, &fi, 0, &f.fence) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(m_device, &si, 0, &f.acquired) != VK_SUCCESS) return false;
        // headless runs draw only into the readback image: no per-frame rings
        if (m_surface && !MakeBuffer(f.vertices, VERTEX_RING, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, 0)) return false;
    }
    return true;
}

VkRenderPass MakePass(VkDevice device, VkImageLayout finalLayout, bool copiedAfter) {
    VkAttachmentDescription a;
    memset(&a, 0, sizeof(a));
    a.format = (VkFormat)OUTPUT_FORMAT;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = finalLayout;
    VkAttachmentReference ref;
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub;
    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2];
    memset(deps, 0, sizeof(deps));
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = copiedAfter ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[1].dstAccessMask = copiedAfter ? VK_ACCESS_TRANSFER_READ_BIT : 0;
    VkRenderPassCreateInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &a;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VkRenderPass pass = VK_NULL_HANDLE;
    vkCreateRenderPass(device, &ci, 0, &pass);
    return pass;
}

bool SpriteVk::MakePipeline() {
    m_outputPass = MakePass(m_device, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false);
    m_readbackPass = MakePass(m_device, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, true);
    if (!m_outputPass || !m_readbackPass) return false;

    VkDescriptorSetLayoutBinding b;
    memset(&b, 0, sizeof(b));
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li;
    memset(&li, 0, sizeof(li));
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &b;
    if (vkCreateDescriptorSetLayout(m_device, &li, 0, &m_setLayout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize ps;
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = PT_COUNT;
    VkDescriptorPoolCreateInfo pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = PT_COUNT;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(m_device, &pi, 0, &m_descPool) != VK_SUCCESS) return false;
    for (int i = 0; i < PT_COUNT; ++i) {
        VkDescriptorSetAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_setLayout;
        if (vkAllocateDescriptorSets(m_device, &ai, &m_tex[i].set) != VK_SUCCESS) return false;
    }
    VkPushConstantRange pc;
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = 16;                                  // pixels -> clip space: scale.xy, offset.zw
    VkPipelineLayoutCreateInfo pli;
    memset(&pli, 0, sizeof(pli));
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, 0, &m_layout) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;           // the original's point sampling
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &si, 0, &m_sampler) != VK_SUCCESS) return false;

    VkShaderModule mods[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    const u32* code[2] = {SPV_sprite_vert, SPV_sprite_frag};
    size_t bytes[2] = {sizeof(SPV_sprite_vert), sizeof(SPV_sprite_frag)};
    for (int i = 0; i < 2; ++i) {
        VkShaderModuleCreateInfo mi;
        memset(&mi, 0, sizeof(mi));
        mi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mi.codeSize = bytes[i];
        mi.pCode = code[i];
        if (vkCreateShaderModule(m_device, &mi, 0, &mods[i]) != VK_SUCCESS) {
            PortLog("vk: a shader module failed");
            return false;
        }
    }
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = mods[i];
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription binding;
    binding.binding = 0;
    binding.stride = (u32)sizeof(PortSpriteVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[4];
    memset(attrs, 0, sizeof(attrs));
    attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(PortSpriteVertex, x);
    attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = offsetof(PortSpriteVertex, u);
    attrs[2].location = 2; attrs[2].format = VK_FORMAT_R8G8B8A8_UNORM; attrs[2].offset = offsetof(PortSpriteVertex, color);
    attrs[3].location = 3; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = offsetof(PortSpriteVertex, add);
    VkPipelineVertexInputStateCreateInfo vin;
    memset(&vin, 0, sizeof(vin));
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 4;
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
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend;
    memset(&blend, 0, sizeof(blend));
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;            // D3DBLEND_SRCALPHA / INVSRCALPHA
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
    gp.layout = m_layout;
    gp.renderPass = m_outputPass;                 // compatible with the readback pass: same single attachment
    VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, 0, &m_pipeline);
    vkDestroyShaderModule(m_device, mods[0], 0);
    vkDestroyShaderModule(m_device, mods[1], 0);
    if (r != VK_SUCCESS) PortLog("vk: the sprite pipeline failed (%d)", (int)r);
    return r == VK_SUCCESS;
}

bool SpriteVk::MakeSwapchain() {
    if (!m_surface) return true;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = (u32)m_w;
        extent.height = (u32)m_h;
    }
    m_swapExtent = extent;
    if (extent.width == 0 || extent.height == 0) return true;      // minimised: nothing to show
    u32 nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &nf, 0);
    std::vector<VkSurfaceFormatKHR> formats(nf);
    if (nf) vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &nf, &formats[0]);
    bool found = false;
    for (u32 i = 0; i < nf; ++i)
        if (formats[i].format == (VkFormat)OUTPUT_FORMAT) found = true;
    if (!found) {
        PortLog("vk: the surface has no B8G8R8A8_UNORM format");
        return false;
    }
    u32 count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > MAX_SWAP_IMAGES) count = MAX_SWAP_IMAGES;
    VkSwapchainCreateInfoKHR ci;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = m_surface;
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
    VkResult r = vkCreateSwapchainKHR(m_device, &ci, 0, &m_swapchain);
    if (r != VK_SUCCESS) {
        PortLog("vk: vkCreateSwapchainKHR failed (%d)", (int)r);
        return false;
    }
    m_swapCount = MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapCount, m_swapImages);
    for (u32 i = 0; i < m_swapCount; ++i) {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)OUTPUT_FORMAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vi, 0, &m_swapViews[i]) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = m_outputPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &m_swapViews[i];
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(m_device, &fi, 0, &m_swapFbs[i]) != VK_SUCCESS) return false;
        VkSemaphoreCreateInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(m_device, &si, 0, &m_swapDone[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void SpriteVk::DestroySwapchain() {
    for (u32 i = 0; i < m_swapCount; ++i) {
        if (m_swapFbs[i]) vkDestroyFramebuffer(m_device, m_swapFbs[i], 0);
        if (m_swapViews[i]) vkDestroyImageView(m_device, m_swapViews[i], 0);
        if (m_swapDone[i]) vkDestroySemaphore(m_device, m_swapDone[i], 0);
        m_swapFbs[i] = VK_NULL_HANDLE;
        m_swapViews[i] = VK_NULL_HANDLE;
        m_swapDone[i] = VK_NULL_HANDLE;
    }
    m_swapCount = 0;
    if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, 0);
    m_swapchain = VK_NULL_HANDLE;
}

bool SpriteVk::Init() {
    if (!m_hooks.getInstanceProcAddr) return false;
    return MakeInstance() && PickDevice() && MakeDevice() && MakeFrames() && MakePipeline() && MakeSwapchain();
}

SpriteVk::~SpriteVk() {
    if (!m_device) {
        if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, 0);
        if (m_instance) vkDestroyInstance(m_instance, 0);
        return;
    }
    vkDeviceWaitIdle(m_device);
    for (int i = 0; i < FRAMES; ++i) {
        Frame& f = m_frames[i];
        FreeBuffer(f.vertices);
        if (f.fence) vkDestroyFence(m_device, f.fence, 0);
        if (f.acquired) vkDestroySemaphore(m_device, f.acquired, 0);
    }
    for (int i = 0; i < PT_COUNT; ++i) FreeTexture(m_tex[i]);
    DestroySwapchain();
    if (m_readFb) vkDestroyFramebuffer(m_device, m_readFb, 0);
    if (m_readView) vkDestroyImageView(m_device, m_readView, 0);
    if (m_readImage) vkDestroyImage(m_device, m_readImage, 0);
    if (m_readMem) vkFreeMemory(m_device, m_readMem, 0);
    FreeBuffer(m_readBuffer);
    FreeBuffer(m_readVertices);
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, 0);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, 0);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, 0);
    if (m_descPool) vkDestroyDescriptorPool(m_device, m_descPool, 0);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, 0);
    if (m_outputPass) vkDestroyRenderPass(m_device, m_outputPass, 0);
    if (m_readbackPass) vkDestroyRenderPass(m_device, m_readbackPass, 0);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, 0);
    vkDestroyDevice(m_device, 0);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, 0);
    vkDestroyInstance(m_instance, 0);
}

VkCommandBuffer SpriteVk::BeginOneShot() {
    VkCommandBufferAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void SpriteVk::EndOneShot(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, 0, 0, 0, 1, &b);
}

bool SpriteVk::SetTexture(int slot, int w, int h, const unsigned char* rgba) {
    if (slot < 0 || slot >= PT_COUNT || w <= 0 || h <= 0 || !rgba) return false;
    vkDeviceWaitIdle(m_device);
    Texture& t = m_tex[slot];
    FreeTexture(t);
    VkImageCreateInfo ii;
    memset(&ii, 0, sizeof(ii));
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width = (u32)w;
    ii.extent.height = (u32)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ii, 0, &t.image) != VK_SUCCESS) {
        PortLog("vk: vkCreateImage %dx%d failed", w, h);
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, t.image, &req);
    VkMemoryAllocateInfo ai;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    int type = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (type < 0) type = FindMemory(req.memoryTypeBits, 0, 0);
    ai.memoryTypeIndex = (u32)type;
    if (type < 0 || vkAllocateMemory(m_device, &ai, 0, &t.mem) != VK_SUCCESS) {
        PortLog("vk: no memory for a %dx%d texture", w, h);
        FreeTexture(t);
        return false;
    }
    vkBindImageMemory(m_device, t.image, t.mem, 0);
    VkImageViewCreateInfo vi;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &vi, 0, &t.view) != VK_SUCCESS) {
        FreeTexture(t);
        return false;
    }
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
    Buffer staging;
    if (!MakeBuffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
        FreeTexture(t);
        return false;
    }
    memcpy(staging.map, rgba, (size_t)bytes);
    VkCommandBuffer cmd = BeginOneShot();
    ImageBarrier(cmd, t.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (u32)w;
    region.imageExtent.height = (u32)h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, staging.buf, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ImageBarrier(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    EndOneShot(cmd);
    FreeBuffer(staging);
    t.w = w;
    t.h = h;
    VkDescriptorImageInfo img;
    img.sampler = m_sampler;
    img.imageView = t.view;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr;
    memset(&wr, 0, sizeof(wr));
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = t.set;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &img;
    vkUpdateDescriptorSets(m_device, 1, &wr, 0, 0);
    PortLog("vk: texture %d %dx%d", slot, w, h);
    return true;
}

// One pass over a list: cleared, then each run with its texture.
void SpriteVk::Record(VkCommandBuffer cmd, Buffer& vertices, VkRenderPass pass, VkFramebuffer fb, VkExtent2D extent) {
    const PortDrawList& list = *m_recording;
    size_t count = list.vertices.size();
    if (count * sizeof(PortSpriteVertex) > vertices.size) {
        if (!m_ringFullLogged) PortLog("vk: %u vertices do not fit the frame's ring; the rest are dropped", (u32)count);
        m_ringFullLogged = true;
        count = (size_t)(vertices.size / sizeof(PortSpriteVertex));
    }
    if (count) memcpy(vertices.map, &list.vertices[0], count * sizeof(PortSpriteVertex));
    VkRenderPassBeginInfo rp;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = pass;
    rp.framebuffer = fb;
    rp.renderArea.extent = extent;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp;
    vp.x = vp.y = 0.0f;
    vp.width = (float)extent.width;
    vp.height = (float)extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc;
    sc.offset.x = sc.offset.y = 0;
    sc.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkClearAttachment ca;
    memset(&ca, 0, sizeof(ca));
    ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ca.colorAttachment = 0;
    ca.clearValue.color.float32[0] = ((list.clearArgb >> 16) & 255) / 255.0f;
    ca.clearValue.color.float32[1] = ((list.clearArgb >> 8) & 255) / 255.0f;
    ca.clearValue.color.float32[2] = (list.clearArgb & 255) / 255.0f;
    ca.clearValue.color.float32[3] = 1.0f;
    VkClearRect cr;
    memset(&cr, 0, sizeof(cr));
    cr.rect.extent = extent;
    cr.layerCount = 1;
    vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
    if (count) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        // the list's pixels (m_w x m_h) to clip space; Vulkan's y points down
        const float xform[4] = {2.0f / (float)m_w, 2.0f / (float)m_h, -1.0f, -1.0f};
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, xform);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices.buf, &zero);
        int bound = -1;
        for (size_t i = 0; i < list.runs.size(); ++i) {
            const PortDrawRun& run = list.runs[i];
            if (run.texture < 0 || run.texture >= PT_COUNT || !m_tex[run.texture].view) continue;
            if (run.first >= count) break;
            u32 n = run.count;
            if (run.first + n > count) n = (u32)(count - run.first) / 3 * 3;
            if (run.texture != bound) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_tex[run.texture].set, 0, 0);
                bound = run.texture;
            }
            if (n) vkCmdDraw(cmd, n, 1, run.first, 0);
        }
    }
    vkCmdEndRenderPass(cmd);
}

void SpriteVk::Draw(const PortDrawList& list) {
    if (!m_surface || m_keepLast) m_last = list; // a copy only where a test may read it back
    if (!m_surface) return;                      // headless: drawn only when read back
    if (!m_swapchain) {                          // minimised before: try again
        DestroySwapchain();
        MakeSwapchain();
        if (!m_swapchain) return;
    }
    m_frame = (m_frame + 1) % FRAMES;
    Frame& f = m_frames[m_frame];
    vkWaitForFences(m_device, 1, &f.fence, VK_TRUE, UINT64_MAX);
    u32 index = 0;
    VkResult r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(m_device);
        DestroySwapchain();
        MakeSwapchain();
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(m_device, 1, &f.fence);
    vkResetCommandBuffer(f.cmd, 0);
    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    m_recording = &list;
    Record(f.cmd, f.vertices, m_outputPass, m_swapFbs[index], m_swapExtent);
    m_recording = 0;
    vkEndCommandBuffer(f.cmd);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.acquired;
    si.pWaitDstStageMask = &waitStage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_swapDone[index];
    r = vkQueueSubmit(m_queue, 1, &si, f.fence);
    if (r != VK_SUCCESS) PortLog("vk: vkQueueSubmit failed (%d)", (int)r);
    m_presentImage = (int)index;
}

void SpriteVk::Present() {
    if (m_presentImage < 0 || !m_swapchain) return;
    u32 index = (u32)m_presentImage;
    VkPresentInfoKHR pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_swapDone[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &index;
    VkResult r = vkQueuePresentKHR(m_queue, &pi);
    m_presentImage = -1;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(m_device);
        DestroySwapchain();
        MakeSwapchain();
    }
}

bool SpriteVk::ReadPixels(unsigned char* rgba) {
    if (!m_readImage) {
        VkImageCreateInfo ii;
        memset(&ii, 0, sizeof(ii));
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = (VkFormat)OUTPUT_FORMAT;
        ii.extent.width = (u32)m_w;
        ii.extent.height = (u32)m_h;
        ii.extent.depth = 1;
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateImage(m_device, &ii, 0, &m_readImage) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_device, m_readImage, &req);
        VkMemoryAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = (u32)FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (vkAllocateMemory(m_device, &ai, 0, &m_readMem) != VK_SUCCESS) return false;
        vkBindImageMemory(m_device, m_readImage, m_readMem, 0);
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_readImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)OUTPUT_FORMAT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vi, 0, &m_readView) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = m_readbackPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &m_readView;
        fi.width = (u32)m_w;
        fi.height = (u32)m_h;
        fi.layers = 1;
        if (vkCreateFramebuffer(m_device, &fi, 0, &m_readFb) != VK_SUCCESS) return false;
        if (!MakeBuffer(m_readBuffer, (VkDeviceSize)m_w * m_h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
            return false;
        if (!MakeBuffer(m_readVertices, VERTEX_RING, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0))
            return false;
    }
    vkDeviceWaitIdle(m_device);
    VkCommandBuffer cmd = BeginOneShot();
    VkExtent2D extent;
    extent.width = (u32)m_w;
    extent.height = (u32)m_h;
    m_recording = &m_last;
    Record(cmd, m_readVertices, m_readbackPass, m_readFb, extent);
    m_recording = 0;
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (u32)m_w;
    region.imageExtent.height = (u32)m_h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, m_readImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readBuffer.buf, 1, &region);
    EndOneShot(cmd);
    const u8* src = m_readBuffer.map;
    for (int i = 0, n = m_w * m_h; i < n; ++i) {        // B, G, R, A -> R, G, B, A
        rgba[i * 4 + 0] = src[i * 4 + 2];
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }
    return true;
}

} // namespace

PortRenderer* PortCreateRenderer(const PortVkHooks& hooks, int width, int height) {
    SpriteVk* r = new SpriteVk(hooks, width, height);
    if (!r->Init()) {
        PortLog("vk: no Vulkan renderer");
        delete r;
        return 0;
    }
    return r;
}
