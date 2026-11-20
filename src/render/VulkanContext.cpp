#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#define VOLK_IMPLEMENTATION
#include <volk.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "render/VulkanContext.hpp"
#include "render/VulkanShadersSpv.hpp"
#include "platform/PlatformApp.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define OC_VK_LOG(...) __android_log_print(ANDROID_LOG_INFO, "OpenCityVK", __VA_ARGS__)
#define OC_VK_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "OpenCityVK", __VA_ARGS__)
#else
#define OC_VK_LOG(...) do { std::fprintf(stdout, "[Vulkan] " __VA_ARGS__); std::fprintf(stdout, "\n"); } while (0)
#define OC_VK_ERR(...) do { std::fprintf(stderr, "[Vulkan ERROR] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#endif

namespace oc {

namespace {

constexpr std::uint32_t kMaxDynamic3dVerts = 65536;
constexpr std::uint32_t kMaxUi2dVerts = 131072;

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

} // namespace

struct VulkanContext::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = 0;
    std::uint32_t presentQueueFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    std::string gpuName = "Unknown Vulkan GPU";
    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits msaa = VK_SAMPLE_COUNT_1_BIT;
    bool hdr16Enabled = false;
    bool swapchainSupportsTransferSrc = false;
    bool ready = false;

    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{1440, 860};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // Render passes
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkRenderPass sceneRenderPass = VK_NULL_HANDLE;
    VkRenderPass swapchainRenderPass = VK_NULL_HANDLE;

    // Shadow target (2048x2048)
    AllocatedImage shadowImage{};
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;

    // Scene HDR + optional MSAA targets
    AllocatedImage msaaColorImage{};
    AllocatedImage sceneColorImage{}; // Resolved linear HDR sampled by post.frag
    AllocatedImage sceneDepthImage{};
    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
    VkSampler linearClampSampler = VK_NULL_HANDLE;

    // Font atlas R8_UNORM texture for Vulkan2D
    AllocatedImage fontAtlasImage{};
    VkSampler fontSampler = VK_NULL_HANDLE;

    // Descriptors
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout sceneDescLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout postDescLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDescLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> sceneDescSets{};
    VkDescriptorSet postDescSet = VK_NULL_HANDLE;
    VkDescriptorSet uiDescSet = VK_NULL_HANDLE;

    // Pipeline layouts & pipelines
    VkPipelineLayout scenePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout postPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout uiPipelineLayout = VK_NULL_HANDLE;

    VkPipeline depthPipeline = VK_NULL_HANDLE;
    VkPipeline skyPipeline = VK_NULL_HANDLE;
    VkPipeline worldOpaquePipeline = VK_NULL_HANDLE;
    VkPipeline worldAlphaPipeline = VK_NULL_HANDLE;
    VkPipeline worldAdditivePipeline = VK_NULL_HANDLE;
    VkPipeline postPipeline = VK_NULL_HANDLE;
    VkPipeline ui2dPipeline = VK_NULL_HANDLE;

    // Commands & sync
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> cmdBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailableSem{};
    std::array<VkSemaphore, kMaxFramesInFlight> renderFinishedSem{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFence{};

    // Per-frame UBO & dynamic vertex buffers
    std::array<AllocatedBuffer, kMaxFramesInFlight> sceneUboBufs{};
    std::array<AllocatedBuffer, kMaxFramesInFlight> dynamic3dBufs{};
    std::array<AllocatedBuffer, kMaxFramesInFlight> ui2dBufs{};

    // Active frame state
    int frameIdx = 0;
    std::uint32_t currentImageIndex = 0;
    bool frameActive = false;
    bool uiPipelineBound = false;
    std::uint32_t dynamic3dVertOffset = 0;
    std::uint32_t ui2dVertOffset = 0;
    WorldPipelineKind activeWorldPipeline = WorldPipelineKind::Opaque;
    bool worldPipelineBound = false;

    // Helpers
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags props) const {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        return 0;
    }

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      AllocatedBuffer& out, bool mapPersistent = false) {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bci, nullptr, &out.buffer) != VK_SUCCESS) return false;

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, out.buffer, &req);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
        if (vkAllocateMemory(device, &mai, nullptr, &out.memory) != VK_SUCCESS) {
            vkDestroyBuffer(device, out.buffer, nullptr);
            out.buffer = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(device, out.buffer, out.memory, 0);
        out.size = size;
        if (mapPersistent) {
            vkMapMemory(device, out.memory, 0, size, 0, &out.mapped);
        }
        return true;
    }

    void destroyBuffer(AllocatedBuffer& buf) {
        if (!device) return;
        if (buf.mapped && buf.memory) {
            vkUnmapMemory(device, buf.memory);
            buf.mapped = nullptr;
        }
        if (buf.buffer) {
            vkDestroyBuffer(device, buf.buffer, nullptr);
            buf.buffer = VK_NULL_HANDLE;
        }
        if (buf.memory) {
            vkFreeMemory(device, buf.memory, nullptr);
            buf.memory = VK_NULL_HANDLE;
        }
        buf.size = 0;
    }

    bool createImage(std::uint32_t w, std::uint32_t h, VkFormat fmt, VkSampleCountFlagBits samples,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, AllocatedImage& out) {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = {w, h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = fmt;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = usage;
        ici.samples = samples;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &ici, nullptr, &out.image) != VK_SUCCESS) return false;

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, out.image, &req);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &mai, nullptr, &out.memory) != VK_SUCCESS) {
            vkDestroyImage(device, out.image, nullptr);
            out.image = VK_NULL_HANDLE;
            return false;
        }
        vkBindImageMemory(device, out.image, out.memory, 0);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = out.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange.aspectMask = aspect;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &out.view) != VK_SUCCESS) {
            vkDestroyImage(device, out.image, nullptr);
            vkFreeMemory(device, out.memory, nullptr);
            out.image = VK_NULL_HANDLE;
            out.memory = VK_NULL_HANDLE;
            return false;
        }
        out.format = fmt;
        out.width = w;
        out.height = h;
        return true;
    }

    void destroyImage(AllocatedImage& img) {
        if (!device) return;
        if (img.view) { vkDestroyImageView(device, img.view, nullptr); img.view = VK_NULL_HANDLE; }
        if (img.image) { vkDestroyImage(device, img.image, nullptr); img.image = VK_NULL_HANDLE; }
        if (img.memory) { vkFreeMemory(device, img.memory, nullptr); img.memory = VK_NULL_HANDLE; }
        img.width = 0;
        img.height = 0;
    }

    VkCommandBuffer beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = commandPool;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &ai, &cmd);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void endSingleTimeCommands(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    bool uploadBufferDeviceLocal(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkBuffer& outBuf, VkDeviceMemory& outMem) {
        AllocatedBuffer staging{};
        if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, true)) {
            return false;
        }
        std::memcpy(staging.mapped, data, static_cast<std::size_t>(size));

        AllocatedBuffer dst{};
        if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dst, false)) {
            destroyBuffer(staging);
            return false;
        }

        VkCommandBuffer cmd = beginSingleTimeCommands();
        VkBufferCopy copyRegion{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &copyRegion);
        endSingleTimeCommands(cmd);

        destroyBuffer(staging);
        outBuf = dst.buffer;
        outMem = dst.memory;
        return true;
    }

    VkShaderModule createShaderModule(const std::uint32_t* code, std::size_t wordCount) {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = wordCount * sizeof(std::uint32_t);
        ci.pCode = code;
        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
            OC_VK_ERR("vkCreateShaderModule failed");
            return VK_NULL_HANDLE;
        }
        return mod;
    }

    void destroySwapchainResources() {
        if (!device) return;
        for (auto fb : swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        swapchainFramebuffers.clear();
        for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
        swapchainImageViews.clear();
        swapchainImages.clear();

        if (sceneFramebuffer) { vkDestroyFramebuffer(device, sceneFramebuffer, nullptr); sceneFramebuffer = VK_NULL_HANDLE; }
        destroyImage(msaaColorImage);
        destroyImage(sceneColorImage);
        destroyImage(sceneDepthImage);

        if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
    }

    bool createRenderPasses() {
        // 1. Shadow Depth Pass
        {
            VkAttachmentDescription depthAtt{};
            depthAtt.format = depthFormat;
            depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.pDepthStencilAttachment = &depthRef;

            std::array<VkSubpassDependency, 2> deps{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo rpci{};
            rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpci.attachmentCount = 1;
            rpci.pAttachments = &depthAtt;
            rpci.subpassCount = 1;
            rpci.pSubpasses = &subpass;
            rpci.dependencyCount = static_cast<std::uint32_t>(deps.size());
            rpci.pDependencies = deps.data();
            if (vkCreateRenderPass(device, &rpci, nullptr, &shadowRenderPass) != VK_SUCCESS) return false;
        }

        // 2. Scene HDR (+ MSAA optional) Pass
        {
            std::vector<VkAttachmentDescription> atts;
            VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

            if (msaa != VK_SAMPLE_COUNT_1_BIT) {
                VkAttachmentDescription msaaColor{};
                msaaColor.format = hdrFormat;
                msaaColor.samples = msaa;
                msaaColor.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                msaaColor.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                msaaColor.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                msaaColor.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                msaaColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                msaaColor.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkAttachmentDescription msaaDepth{};
                msaaDepth.format = depthFormat;
                msaaDepth.samples = msaa;
                msaaDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                msaaDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                msaaDepth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                msaaDepth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                msaaDepth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                msaaDepth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkAttachmentDescription resolvedColor{};
                resolvedColor.format = hdrFormat;
                resolvedColor.samples = VK_SAMPLE_COUNT_1_BIT;
                resolvedColor.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolvedColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                resolvedColor.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolvedColor.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                resolvedColor.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                resolvedColor.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                atts = {msaaColor, msaaDepth, resolvedColor};
            } else {
                VkAttachmentDescription colorAtt{};
                colorAtt.format = hdrFormat;
                colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
                colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentDescription depthAtt{};
                depthAtt.format = depthFormat;
                depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
                depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                atts = {colorAtt, depthAtt};
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;
            if (msaa != VK_SAMPLE_COUNT_1_BIT) {
                subpass.pResolveAttachments = &resolveRef;
            }

            std::array<VkSubpassDependency, 2> deps{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo rpci{};
            rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpci.attachmentCount = static_cast<std::uint32_t>(atts.size());
            rpci.pAttachments = atts.data();
            rpci.subpassCount = 1;
            rpci.pSubpasses = &subpass;
            rpci.dependencyCount = static_cast<std::uint32_t>(deps.size());
            rpci.pDependencies = deps.data();
            if (vkCreateRenderPass(device, &rpci, nullptr, &sceneRenderPass) != VK_SUCCESS) return false;
        }

        // 3. Swapchain Post-Process + 2D UI Pass
        {
            VkAttachmentDescription colorAtt{};
            colorAtt.format = swapchainFormat;
            colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            VkSubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass = 0;
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.srcAccessMask = 0;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpci{};
            rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpci.attachmentCount = 1;
            rpci.pAttachments = &colorAtt;
            rpci.subpassCount = 1;
            rpci.pSubpasses = &subpass;
            rpci.dependencyCount = 1;
            rpci.pDependencies = &dep;
            if (vkCreateRenderPass(device, &rpci, nullptr, &swapchainRenderPass) != VK_SUCCESS) return false;
        }
        return true;
    }

    bool createPipelines() {
        // Common Vertex3D binding
        VkVertexInputBindingDescription v3Bind{};
        v3Bind.binding = 0;
        v3Bind.stride = sizeof(Vertex3D);
        v3Bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 4> v3Attrs{};
        v3Attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex3D, px))};
        v3Attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex3D, nx))};
        v3Attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    static_cast<std::uint32_t>(offsetof(Vertex3D, u))};
        v3Attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex3D, r))};

        VkPipelineVertexInputStateCreateInfo v3Input{};
        v3Input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        v3Input.vertexBindingDescriptionCount = 1;
        v3Input.pVertexBindingDescriptions = &v3Bind;
        v3Input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(v3Attrs.size());
        v3Input.pVertexAttributeDescriptions = v3Attrs.data();

        VkPipelineInputAssemblyStateCreateInfo iaTri{};
        iaTri.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaTri.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vpState{};
        vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpState.viewportCount = 1;
        vpState.scissorCount = 1;

        std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynCi{};
        dynCi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynCi.dynamicStateCount = static_cast<std::uint32_t>(dynStates.size());
        dynCi.pDynamicStates = dynStates.data();

        // Pipeline layouts
        {
            VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawPush)};
            VkPipelineLayoutCreateInfo lci{};
            lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            lci.setLayoutCount = 1;
            lci.pSetLayouts = &sceneDescLayout;
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
            if (vkCreatePipelineLayout(device, &lci, nullptr, &scenePipelineLayout) != VK_SUCCESS) return false;
        }
        {
            VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
            VkPipelineLayoutCreateInfo lci{};
            lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            lci.setLayoutCount = 1;
            lci.pSetLayouts = &postDescLayout;
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
            if (vkCreatePipelineLayout(device, &lci, nullptr, &postPipelineLayout) != VK_SUCCESS) return false;
        }
        {
            VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPush)};
            VkPipelineLayoutCreateInfo lci{};
            lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            lci.setLayoutCount = 1;
            lci.pSetLayouts = &uiDescLayout;
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
            if (vkCreatePipelineLayout(device, &lci, nullptr, &uiPipelineLayout) != VK_SUCCESS) return false;
        }

        // Shader modules
        VkShaderModule modDepthV = createShaderModule(spv::kDepthVertSpv, std::size(spv::kDepthVertSpv));
        VkShaderModule modDepthF = createShaderModule(spv::kDepthFragSpv, std::size(spv::kDepthFragSpv));
        VkShaderModule modSkyV   = createShaderModule(spv::kSkyVertSpv,   std::size(spv::kSkyVertSpv));
        VkShaderModule modSkyF   = createShaderModule(spv::kSkyFragSpv,   std::size(spv::kSkyFragSpv));
        VkShaderModule modWorldV = createShaderModule(spv::kWorldVertSpv, std::size(spv::kWorldVertSpv));
        VkShaderModule modWorldF = createShaderModule(spv::kWorldFragSpv, std::size(spv::kWorldFragSpv));
        VkShaderModule modPostV  = createShaderModule(spv::kPostVertSpv,  std::size(spv::kPostVertSpv));
        VkShaderModule modPostF  = createShaderModule(spv::kPostFragSpv,  std::size(spv::kPostFragSpv));
        VkShaderModule modUiV    = createShaderModule(spv::kUi2dVertSpv,  std::size(spv::kUi2dVertSpv));
        VkShaderModule modUiF    = createShaderModule(spv::kUi2dFragSpv,  std::size(spv::kUi2dFragSpv));

        auto makeStages = [](VkShaderModule v, VkShaderModule f) {
            std::array<VkPipelineShaderStageCreateInfo, 2> s{};
            s[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            s[0].module = v;
            s[0].pName = "main";
            s[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            s[1].module = f;
            s[1].pName = "main";
            return s;
        };

        bool ok = true;

        // 1. Depth Shadow Pipeline
        {
            auto stages = makeStages(modDepthV, modDepthF);
            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.cullMode = VK_CULL_MODE_NONE;
            rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rast.depthBiasEnable = VK_TRUE;
            rast.depthBiasConstantFactor = 1.25f;
            rast.depthBiasSlopeFactor = 1.75f;
            rast.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_TRUE;
            ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 0;

            VkGraphicsPipelineCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pci.stageCount = 2;
            pci.pStages = stages.data();
            pci.pVertexInputState = &v3Input;
            pci.pInputAssemblyState = &iaTri;
            pci.pViewportState = &vpState;
            pci.pRasterizationState = &rast;
            pci.pMultisampleState = &ms;
            pci.pDepthStencilState = &ds;
            pci.pColorBlendState = &cb;
            pci.pDynamicState = &dynCi;
            pci.layout = scenePipelineLayout;
            pci.renderPass = shadowRenderPass;
            ok = ok && (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &depthPipeline) == VK_SUCCESS);
        }

        // Helper for Scene Pass pipelines
        auto createScenePipeline = [&](VkShaderModule vs, VkShaderModule fs,
                                       bool depthTest, bool depthWrite,
                                       bool blendEnable, bool additive,
                                       VkPipeline& outPipe) -> bool {
            auto stages = makeStages(vs, fs);
            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.cullMode = VK_CULL_MODE_NONE;
            rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rast.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = msaa;

            VkPipelineDepthStencilStateCreateInfo ds{};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
            ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
            ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cba.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
            if (blendEnable) {
                cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                cba.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                cba.colorBlendOp = VK_BLEND_OP_ADD;
                cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                cba.alphaBlendOp = VK_BLEND_OP_ADD;
            }
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            VkGraphicsPipelineCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pci.stageCount = 2;
            pci.pStages = stages.data();
            pci.pVertexInputState = &v3Input;
            pci.pInputAssemblyState = &iaTri;
            pci.pViewportState = &vpState;
            pci.pRasterizationState = &rast;
            pci.pMultisampleState = &ms;
            pci.pDepthStencilState = &ds;
            pci.pColorBlendState = &cb;
            pci.pDynamicState = &dynCi;
            pci.layout = scenePipelineLayout;
            pci.renderPass = sceneRenderPass;
            return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &outPipe) == VK_SUCCESS;
        };

        ok = ok && createScenePipeline(modSkyV,   modSkyF,   false, false, false, false, skyPipeline);
        ok = ok && createScenePipeline(modWorldV, modWorldF, true,  true,  false, false, worldOpaquePipeline);
        ok = ok && createScenePipeline(modWorldV, modWorldF, true,  false, true,  false, worldAlphaPipeline);
        ok = ok && createScenePipeline(modWorldV, modWorldF, true,  false, true,  true,  worldAdditivePipeline);

        // 5. Post-Process Fullscreen Triangle Pipeline
        {
            auto stages = makeStages(modPostV, modPostF);
            VkPipelineVertexInputStateCreateInfo emptyVi{};
            emptyVi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.cullMode = VK_CULL_MODE_NONE;
            rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rast.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cba.blendEnable = VK_FALSE;
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            VkGraphicsPipelineCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pci.stageCount = 2;
            pci.pStages = stages.data();
            pci.pVertexInputState = &emptyVi;
            pci.pInputAssemblyState = &iaTri;
            pci.pViewportState = &vpState;
            pci.pRasterizationState = &rast;
            pci.pMultisampleState = &ms;
            pci.pColorBlendState = &cb;
            pci.pDynamicState = &dynCi;
            pci.layout = postPipelineLayout;
            pci.renderPass = swapchainRenderPass;
            ok = ok && (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &postPipeline) == VK_SUCCESS);
        }

        // 6. 2D UI & Korean Font Atlas Pipeline
        {
            auto stages = makeStages(modUiV, modUiF);
            VkVertexInputBindingDescription v2Bind{0, sizeof(Vertex2D), VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription, 3> v2Attrs{};
            v2Attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,       static_cast<std::uint32_t>(offsetof(Vertex2D, x))};
            v2Attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,       static_cast<std::uint32_t>(offsetof(Vertex2D, u))};
            v2Attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex2D, r))};

            VkPipelineVertexInputStateCreateInfo v2Vi{};
            v2Vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            v2Vi.vertexBindingDescriptionCount = 1;
            v2Vi.pVertexBindingDescriptions = &v2Bind;
            v2Vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(v2Attrs.size());
            v2Vi.pVertexAttributeDescriptions = v2Attrs.data();

            VkPipelineRasterizationStateCreateInfo rast{};
            rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rast.polygonMode = VK_POLYGON_MODE_FILL;
            rast.cullMode = VK_CULL_MODE_NONE;
            rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rast.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState cba{};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            VkGraphicsPipelineCreateInfo pci{};
            pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pci.stageCount = 2;
            pci.pStages = stages.data();
            pci.pVertexInputState = &v2Vi;
            pci.pInputAssemblyState = &iaTri;
            pci.pViewportState = &vpState;
            pci.pRasterizationState = &rast;
            pci.pMultisampleState = &ms;
            pci.pColorBlendState = &cb;
            pci.pDynamicState = &dynCi;
            pci.layout = uiPipelineLayout;
            pci.renderPass = swapchainRenderPass;
            ok = ok && (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &ui2dPipeline) == VK_SUCCESS);
        }

        vkDestroyShaderModule(device, modDepthV, nullptr);
        vkDestroyShaderModule(device, modDepthF, nullptr);
        vkDestroyShaderModule(device, modSkyV,   nullptr);
        vkDestroyShaderModule(device, modSkyF,   nullptr);
        vkDestroyShaderModule(device, modWorldV, nullptr);
        vkDestroyShaderModule(device, modWorldF, nullptr);
        vkDestroyShaderModule(device, modPostV,  nullptr);
        vkDestroyShaderModule(device, modPostF,  nullptr);
        vkDestroyShaderModule(device, modUiV,    nullptr);
        vkDestroyShaderModule(device, modUiF,    nullptr);
        return ok;
    }

    bool createSwapchainAndSceneTargets(const PlatformApp& app) {
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps) != VK_SUCCESS) {
            return false;
        }
        std::uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        if (fmtCount > 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, formats.data());
        }

        VkSurfaceFormatKHR chosen = formats.empty()
            ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
            : formats[0];
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                chosen = f;
                break;
            }
        }
        swapchainFormat = chosen.format;

        if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max() &&
            caps.currentExtent.width > 0 && caps.currentExtent.height > 0) {
            swapchainExtent = caps.currentExtent;
        } else {
            swapchainExtent.width = std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(std::max(1, app.screenWidth())),
                caps.minImageExtent.width, caps.maxImageExtent.width);
            swapchainExtent.height = std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(std::max(1, app.screenHeight())),
                caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (swapchainExtent.width == 0 || swapchainExtent.height == 0) return false;

        std::uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainSupportsTransferSrc = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (swapchainSupportsTransferSrc) {
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        VkCompositeAlphaFlagBitsKHR compAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
            if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
                compAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
            } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
                compAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
            } else {
                compAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
            }
        }

        VkSwapchainCreateInfoKHR sci{};
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface = surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = chosen.format;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = swapchainExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = usage;
        std::uint32_t qFamilies[2] = {graphicsQueueFamily, presentQueueFamily};
        if (graphicsQueueFamily != presentQueueFamily) {
            sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices = qFamilies;
        } else {
            sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = compAlpha;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
            OC_VK_ERR("vkCreateSwapchainKHR failed");
            return false;
        }

        std::uint32_t scCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &scCount, nullptr);
        swapchainImages.resize(scCount);
        vkGetSwapchainImagesKHR(device, swapchain, &scCount, swapchainImages.data());

        swapchainImageViews.resize(scCount);
        for (std::uint32_t i = 0; i < scCount; ++i) {
            VkImageViewCreateInfo ivci{};
            ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivci.image = swapchainImages[i];
            ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format = swapchainFormat;
            ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &ivci, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) return false;
        }

        // Create render passes on first init if not yet created
        if (swapchainRenderPass == VK_NULL_HANDLE) {
            if (!createRenderPasses()) return false;
        }

        swapchainFramebuffers.resize(scCount);
        for (std::uint32_t i = 0; i < scCount; ++i) {
            VkFramebufferCreateInfo fbci{};
            fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbci.renderPass = swapchainRenderPass;
            fbci.attachmentCount = 1;
            fbci.pAttachments = &swapchainImageViews[i];
            fbci.width = swapchainExtent.width;
            fbci.height = swapchainExtent.height;
            fbci.layers = 1;
            if (vkCreateFramebuffer(device, &fbci, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) return false;
        }

        // Create HDR scene targets (+ MSAA attachments if enabled)
        if (!createImage(swapchainExtent.width, swapchainExtent.height, hdrFormat, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, sceneColorImage)) {
            return false;
        }
        if (!createImage(swapchainExtent.width, swapchainExtent.height, depthFormat, msaa,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, sceneDepthImage)) {
            return false;
        }
        std::vector<VkImageView> sceneAtts;
        if (msaa != VK_SAMPLE_COUNT_1_BIT) {
            if (!createImage(swapchainExtent.width, swapchainExtent.height, hdrFormat, msaa,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, msaaColorImage)) {
                return false;
            }
            sceneAtts = {msaaColorImage.view, sceneDepthImage.view, sceneColorImage.view};
        } else {
            sceneAtts = {sceneColorImage.view, sceneDepthImage.view};
        }

        VkFramebufferCreateInfo sfb{};
        sfb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        sfb.renderPass = sceneRenderPass;
        sfb.attachmentCount = static_cast<std::uint32_t>(sceneAtts.size());
        sfb.pAttachments = sceneAtts.data();
        sfb.width = swapchainExtent.width;
        sfb.height = swapchainExtent.height;
        sfb.layers = 1;
        if (vkCreateFramebuffer(device, &sfb, nullptr, &sceneFramebuffer) != VK_SUCCESS) return false;

        // Update Post-Process descriptor with the newly created HDR sceneColorImage!
        if (postDescSet != VK_NULL_HANDLE) {
            VkDescriptorImageInfo ii{};
            ii.sampler = linearClampSampler;
            ii.imageView = sceneColorImage.view;
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = postDescSet;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &ii;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
        return true;
    }

    bool uploadFontR8(const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h) {
        if (!device || !pixels || w == 0 || h == 0) return false;
        vkDeviceWaitIdle(device);
        destroyImage(fontAtlasImage);

        if (!createImage(w, h, VK_FORMAT_R8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, fontAtlasImage)) {
            return false;
        }

        const VkDeviceSize byteSize = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h);
        AllocatedBuffer staging{};
        if (!createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, true)) {
            return false;
        }
        std::memcpy(staging.mapped, pixels, static_cast<std::size_t>(byteSize));

        VkCommandBuffer cmd = beginSingleTimeCommands();
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = fontAtlasImage.image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, fontAtlasImage.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = fontAtlasImage.image;
        toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toShader);
        endSingleTimeCommands(cmd);
        destroyBuffer(staging);

        VkDescriptorImageInfo ii{};
        ii.sampler = fontSampler;
        ii.imageView = fontAtlasImage.view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wds{};
        wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds.dstSet = uiDescSet;
        wds.dstBinding = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo = &ii;
        vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);
        return true;
    }
};

VulkanContext::VulkanContext() : impl_(new Impl()) {}
VulkanContext::~VulkanContext() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool VulkanContext::init(const PlatformApp& app) {
    if (volkInitialize() != VK_SUCCESS) {
        OC_VK_ERR("volkInitialize failed (no Vulkan loader present)");
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Open City Seoul";
    appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.pEngineName = "OpenCityVulkan";
    appInfo.engineVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    auto exts = app.getVulkanInstanceExtensions();
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();

    if (vkCreateInstance(&ici, nullptr, &impl_->instance) != VK_SUCCESS) {
        OC_VK_ERR("vkCreateInstance failed");
        return false;
    }
    volkLoadInstance(impl_->instance);

    if (!app.createVulkanSurface(impl_->instance, &impl_->surface)) {
        OC_VK_ERR("createVulkanSurface failed");
        return false;
    }

    std::uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        OC_VK_ERR("No Vulkan physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(impl_->instance, &gpuCount, gpus.data());

    int bestScore = -1;
    for (VkPhysicalDevice dev : gpus) {
        std::uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> devExts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, devExts.data());
        bool hasSwapchain = false;
        for (const auto& e : devExts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
                break;
            }
        }
        if (!hasSwapchain) continue;

        std::uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

        int gfxIdx = -1, presIdx = -1;
        for (std::uint32_t i = 0; i < qCount; ++i) {
            if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxIdx = static_cast<int>(i);
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, impl_->surface, &presentSupport);
            if (presentSupport) presIdx = static_cast<int>(i);
            if (gfxIdx >= 0 && presIdx == gfxIdx) break;
        }
        if (gfxIdx < 0 || presIdx < 0) continue;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        int score = 100;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 600;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 300;
        if (score > bestScore) {
            bestScore = score;
            impl_->physicalDevice = dev;
            impl_->graphicsQueueFamily = static_cast<std::uint32_t>(gfxIdx);
            impl_->presentQueueFamily = static_cast<std::uint32_t>(presIdx);
            impl_->gpuName = props.deviceName;
        }
    }
    if (impl_->physicalDevice == VK_NULL_HANDLE) {
        OC_VK_ERR("No suitable Vulkan GPU with swapchain + graphics queue");
        return false;
    }

    // Choose HDR format, depth format & MSAA sample count safely
    VkPhysicalDeviceProperties devProps{};
    vkGetPhysicalDeviceProperties(impl_->physicalDevice, &devProps);
    const VkSampleCountFlags counts =
        devProps.limits.framebufferColorSampleCounts & devProps.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_4_BIT) impl_->msaa = VK_SAMPLE_COUNT_4_BIT;
    else if (counts & VK_SAMPLE_COUNT_2_BIT) impl_->msaa = VK_SAMPLE_COUNT_2_BIT;
    else impl_->msaa = VK_SAMPLE_COUNT_1_BIT;

    // On software rasterizers (SwiftShader), 2x MSAA is crisp and 2x faster than 4x while still smooth
    if (devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && (counts & VK_SAMPLE_COUNT_2_BIT)) {
        impl_->msaa = VK_SAMPLE_COUNT_2_BIT;
    }

    VkFormatProperties hdrProps{};
    vkGetPhysicalDeviceFormatProperties(impl_->physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &hdrProps);
    const VkFormatFeatureFlags hdrReq =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((hdrProps.optimalTilingFeatures & hdrReq) == hdrReq) {
        impl_->hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        impl_->hdr16Enabled = true;
    } else {
        impl_->hdrFormat = VK_FORMAT_R8G8B8A8_UNORM;
        impl_->hdr16Enabled = false;
    }

    const std::array<VkFormat, 3> depthCandidates = {
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM
    };
    impl_->depthFormat = VK_FORMAT_D32_SFLOAT;
    for (VkFormat cand : depthCandidates) {
        VkFormatProperties dp{};
        vkGetPhysicalDeviceFormatProperties(impl_->physicalDevice, cand, &dp);
        if ((dp.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ==
            (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            impl_->depthFormat = cand;
            break;
        }
    }

    // Create logical device
    float qPriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    VkDeviceQueueCreateInfo qci0{};
    qci0.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci0.queueFamilyIndex = impl_->graphicsQueueFamily;
    qci0.queueCount = 1;
    qci0.pQueuePriorities = &qPriority;
    qcis.push_back(qci0);
    if (impl_->presentQueueFamily != impl_->graphicsQueueFamily) {
        VkDeviceQueueCreateInfo qci1{};
        qci1.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci1.queueFamilyIndex = impl_->presentQueueFamily;
        qci1.queueCount = 1;
        qci1.pQueuePriorities = &qPriority;
        qcis.push_back(qci1);
    }

    const char* devExtNames[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures enabledFeatures{};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExtNames;
    dci.pEnabledFeatures = &enabledFeatures;
    if (vkCreateDevice(impl_->physicalDevice, &dci, nullptr, &impl_->device) != VK_SUCCESS) {
        OC_VK_ERR("vkCreateDevice failed");
        return false;
    }
    volkLoadDevice(impl_->device);
    vkGetDeviceQueue(impl_->device, impl_->graphicsQueueFamily, 0, &impl_->graphicsQueue);
    vkGetDeviceQueue(impl_->device, impl_->presentQueueFamily, 0, &impl_->presentQueue);

    // Command pool
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = impl_->graphicsQueueFamily;
    if (vkCreateCommandPool(impl_->device, &cpci, nullptr, &impl_->commandPool) != VK_SUCCESS) return false;

    // Samplers
    {
        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = VK_FILTER_NEAREST;
        sci.minFilter = VK_FILTER_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(impl_->device, &sci, nullptr, &impl_->shadowSampler) != VK_SUCCESS) return false;

        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        if (vkCreateSampler(impl_->device, &sci, nullptr, &impl_->linearClampSampler) != VK_SUCCESS) return false;
        if (vkCreateSampler(impl_->device, &sci, nullptr, &impl_->fontSampler) != VK_SUCCESS) return false;
    }

    // Descriptor layouts
    {
        std::array<VkDescriptorSetLayoutBinding, 2> sb{};
        sb[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        sb[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 2;
        lci.pBindings = sb.data();
        if (vkCreateDescriptorSetLayout(impl_->device, &lci, nullptr, &impl_->sceneDescLayout) != VK_SUCCESS) return false;

        VkDescriptorSetLayoutBinding imgBind{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        lci.bindingCount = 1;
        lci.pBindings = &imgBind;
        if (vkCreateDescriptorSetLayout(impl_->device, &lci, nullptr, &impl_->postDescLayout) != VK_SUCCESS) return false;
        if (vkCreateDescriptorSetLayout(impl_->device, &lci, nullptr, &impl_->uiDescLayout) != VK_SUCCESS) return false;
    }

    // Descriptor pool & sets
    {
        std::array<VkDescriptorPoolSize, 2> ps{};
        ps[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<std::uint32_t>(kMaxFramesInFlight)};
        ps[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(kMaxFramesInFlight + 2)};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = static_cast<std::uint32_t>(kMaxFramesInFlight + 2);
        dpci.poolSizeCount = static_cast<std::uint32_t>(ps.size());
        dpci.pPoolSizes = ps.data();
        if (vkCreateDescriptorPool(impl_->device, &dpci, nullptr, &impl_->descPool) != VK_SUCCESS) return false;

        std::array<VkDescriptorSetLayout, kMaxFramesInFlight> sLayouts{};
        sLayouts.fill(impl_->sceneDescLayout);
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = impl_->descPool;
        dsai.descriptorSetCount = kMaxFramesInFlight;
        dsai.pSetLayouts = sLayouts.data();
        if (vkAllocateDescriptorSets(impl_->device, &dsai, impl_->sceneDescSets.data()) != VK_SUCCESS) return false;

        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &impl_->postDescLayout;
        if (vkAllocateDescriptorSets(impl_->device, &dsai, &impl_->postDescSet) != VK_SUCCESS) return false;

        dsai.pSetLayouts = &impl_->uiDescLayout;
        if (vkAllocateDescriptorSets(impl_->device, &dsai, &impl_->uiDescSet) != VK_SUCCESS) return false;
    }

    // Create Swapchain + RenderPasses + Scene HDR Targets
    if (!impl_->createSwapchainAndSceneTargets(app)) return false;

    // Create 2048x2048 Shadow Map target & framebuffer
    if (!impl_->createImage(kShadowMapSize, kShadowMapSize, impl_->depthFormat, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, impl_->shadowImage)) {
        return false;
    }
    {
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = impl_->shadowRenderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &impl_->shadowImage.view;
        fbci.width = kShadowMapSize;
        fbci.height = kShadowMapSize;
        fbci.layers = 1;
        if (vkCreateFramebuffer(impl_->device, &fbci, nullptr, &impl_->shadowFramebuffer) != VK_SUCCESS) return false;
    }

    // Create per-frame UBOs, dynamic vertex buffers, command buffers, and sync primitives
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = impl_->commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = kMaxFramesInFlight;
    if (vkAllocateCommandBuffers(impl_->device, &cbai, impl_->cmdBuffers.data()) != VK_SUCCESS) return false;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (!impl_->createBuffer(sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 impl_->sceneUboBufs[i], true)) return false;
        if (!impl_->createBuffer(kMaxDynamic3dVerts * sizeof(Vertex3D), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 impl_->dynamic3dBufs[i], true)) return false;
        if (!impl_->createBuffer(kMaxUi2dVerts * sizeof(Vertex2D), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 impl_->ui2dBufs[i], true)) return false;

        VkDescriptorBufferInfo bi{impl_->sceneUboBufs[i].buffer, 0, sizeof(SceneUBO)};
        VkDescriptorImageInfo ii{impl_->shadowSampler, impl_->shadowImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> w{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = impl_->sceneDescSets[i];
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = impl_->sceneDescSets[i];
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &ii;
        vkUpdateDescriptorSets(impl_->device, 2, w.data(), 0, nullptr);

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->imageAvailableSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(impl_->device, &sci, nullptr, &impl_->renderFinishedSem[i]) != VK_SUCCESS ||
            vkCreateFence(impl_->device, &fci, nullptr, &impl_->inFlightFence[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Upload initial 2x2 white dummy font atlas so uiDescSet is always valid
    const std::uint8_t dummyWhite[4] = {255, 255, 255, 255};
    impl_->uploadFontR8(dummyWhite, 2, 2);

    // Create all graphics pipelines
    if (!impl_->createPipelines()) {
        OC_VK_ERR("createPipelines failed");
        return false;
    }

    impl_->ready = true;
    OC_VK_LOG("Initialized Native Vulkan 1.0 on '%s' (%ux%u, HDR16=%d, MSAA=%dx)",
              impl_->gpuName.c_str(), impl_->swapchainExtent.width, impl_->swapchainExtent.height,
              impl_->hdr16Enabled ? 1 : 0, static_cast<int>(impl_->msaa));
    return true;
}

void VulkanContext::shutdown() {
    if (!impl_) return;
    if (impl_->device) {
        vkDeviceWaitIdle(impl_->device);
        impl_->destroySwapchainResources();
        if (impl_->shadowFramebuffer) { vkDestroyFramebuffer(impl_->device, impl_->shadowFramebuffer, nullptr); impl_->shadowFramebuffer = VK_NULL_HANDLE; }
        impl_->destroyImage(impl_->shadowImage);
        impl_->destroyImage(impl_->fontAtlasImage);

        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            impl_->destroyBuffer(impl_->sceneUboBufs[i]);
            impl_->destroyBuffer(impl_->dynamic3dBufs[i]);
            impl_->destroyBuffer(impl_->ui2dBufs[i]);
            if (impl_->imageAvailableSem[i]) { vkDestroySemaphore(impl_->device, impl_->imageAvailableSem[i], nullptr); impl_->imageAvailableSem[i] = VK_NULL_HANDLE; }
            if (impl_->renderFinishedSem[i]) { vkDestroySemaphore(impl_->device, impl_->renderFinishedSem[i], nullptr); impl_->renderFinishedSem[i] = VK_NULL_HANDLE; }
            if (impl_->inFlightFence[i]) { vkDestroyFence(impl_->device, impl_->inFlightFence[i], nullptr); impl_->inFlightFence[i] = VK_NULL_HANDLE; }
        }

        auto destroyPipe = [&](VkPipeline& p) { if (p) { vkDestroyPipeline(impl_->device, p, nullptr); p = VK_NULL_HANDLE; } };
        destroyPipe(impl_->depthPipeline);
        destroyPipe(impl_->skyPipeline);
        destroyPipe(impl_->worldOpaquePipeline);
        destroyPipe(impl_->worldAlphaPipeline);
        destroyPipe(impl_->worldAdditivePipeline);
        destroyPipe(impl_->postPipeline);
        destroyPipe(impl_->ui2dPipeline);

        if (impl_->scenePipelineLayout) { vkDestroyPipelineLayout(impl_->device, impl_->scenePipelineLayout, nullptr); impl_->scenePipelineLayout = VK_NULL_HANDLE; }
        if (impl_->postPipelineLayout) { vkDestroyPipelineLayout(impl_->device, impl_->postPipelineLayout, nullptr); impl_->postPipelineLayout = VK_NULL_HANDLE; }
        if (impl_->uiPipelineLayout) { vkDestroyPipelineLayout(impl_->device, impl_->uiPipelineLayout, nullptr); impl_->uiPipelineLayout = VK_NULL_HANDLE; }

        if (impl_->shadowRenderPass) { vkDestroyRenderPass(impl_->device, impl_->shadowRenderPass, nullptr); impl_->shadowRenderPass = VK_NULL_HANDLE; }
        if (impl_->sceneRenderPass) { vkDestroyRenderPass(impl_->device, impl_->sceneRenderPass, nullptr); impl_->sceneRenderPass = VK_NULL_HANDLE; }
        if (impl_->swapchainRenderPass) { vkDestroyRenderPass(impl_->device, impl_->swapchainRenderPass, nullptr); impl_->swapchainRenderPass = VK_NULL_HANDLE; }

        if (impl_->descPool) { vkDestroyDescriptorPool(impl_->device, impl_->descPool, nullptr); impl_->descPool = VK_NULL_HANDLE; }
        if (impl_->sceneDescLayout) { vkDestroyDescriptorSetLayout(impl_->device, impl_->sceneDescLayout, nullptr); impl_->sceneDescLayout = VK_NULL_HANDLE; }
        if (impl_->postDescLayout) { vkDestroyDescriptorSetLayout(impl_->device, impl_->postDescLayout, nullptr); impl_->postDescLayout = VK_NULL_HANDLE; }
        if (impl_->uiDescLayout) { vkDestroyDescriptorSetLayout(impl_->device, impl_->uiDescLayout, nullptr); impl_->uiDescLayout = VK_NULL_HANDLE; }

        if (impl_->shadowSampler) { vkDestroySampler(impl_->device, impl_->shadowSampler, nullptr); impl_->shadowSampler = VK_NULL_HANDLE; }
        if (impl_->linearClampSampler) { vkDestroySampler(impl_->device, impl_->linearClampSampler, nullptr); impl_->linearClampSampler = VK_NULL_HANDLE; }
        if (impl_->fontSampler) { vkDestroySampler(impl_->device, impl_->fontSampler, nullptr); impl_->fontSampler = VK_NULL_HANDLE; }

        if (impl_->commandPool) { vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr); impl_->commandPool = VK_NULL_HANDLE; }
        vkDestroyDevice(impl_->device, nullptr);
        impl_->device = VK_NULL_HANDLE;
    }
    if (impl_->surface && impl_->instance) {
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
        impl_->surface = VK_NULL_HANDLE;
    }
    if (impl_->instance) {
        vkDestroyInstance(impl_->instance, nullptr);
        impl_->instance = VK_NULL_HANDLE;
    }
    impl_->ready = false;
}

bool VulkanContext::recreateSwapchain(const PlatformApp& app) {
    if (!impl_->device || !impl_->surface) return false;
    if (app.screenWidth() <= 0 || app.screenHeight() <= 0) return false;
    vkDeviceWaitIdle(impl_->device);
    impl_->destroySwapchainResources();
    return impl_->createSwapchainAndSceneTargets(app);
}

bool VulkanContext::recreateSurfaceAndSwapchain(const PlatformApp& app) {
    if (!impl_->device || !impl_->instance) return false;
    vkDeviceWaitIdle(impl_->device);
    impl_->destroySwapchainResources();
    if (impl_->surface) {
        vkDestroySurfaceKHR(impl_->instance, impl_->surface, nullptr);
        impl_->surface = VK_NULL_HANDLE;
    }
    if (!app.createVulkanSurface(impl_->instance, &impl_->surface)) return false;
    return impl_->createSwapchainAndSceneTargets(app);
}

void VulkanContext::waitIdle() {
    if (impl_ && impl_->device) vkDeviceWaitIdle(impl_->device);
}

bool VulkanContext::isReady() const { return impl_ && impl_->ready && impl_->swapchain != VK_NULL_HANDLE; }
int VulkanContext::width() const { return impl_ ? static_cast<int>(impl_->swapchainExtent.width) : 0; }
int VulkanContext::height() const { return impl_ ? static_cast<int>(impl_->swapchainExtent.height) : 0; }
const std::string& VulkanContext::deviceName() const { return impl_->gpuName; }
bool VulkanContext::usingHdr16() const { return impl_->hdr16Enabled; }
int VulkanContext::msaaSamples() const { return static_cast<int>(impl_->msaa); }

GpuMesh VulkanContext::createMesh(const std::vector<Vertex3D>& vertices,
                                  const std::vector<std::uint32_t>& indices) {
    GpuMesh m{};
    if (!impl_->device || vertices.empty()) return m;
    const VkDeviceSize vbSize = vertices.size() * sizeof(Vertex3D);
    if (!impl_->uploadBufferDeviceLocal(vertices.data(), vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        m.vertexBuffer, m.vertexMemory)) {
        return {};
    }
    m.vertexCount = static_cast<std::uint32_t>(vertices.size());

    if (!indices.empty()) {
        const VkDeviceSize ibSize = indices.size() * sizeof(std::uint32_t);
        if (!impl_->uploadBufferDeviceLocal(indices.data(), ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            m.indexBuffer, m.indexMemory)) {
            destroyMesh(m);
            return {};
        }
        m.indexCount = static_cast<std::uint32_t>(indices.size());
    }
    return m;
}

void VulkanContext::destroyMesh(GpuMesh& mesh) {
    if (!impl_ || !impl_->device) return;
    if (mesh.indexBuffer) { vkDestroyBuffer(impl_->device, mesh.indexBuffer, nullptr); mesh.indexBuffer = VK_NULL_HANDLE; }
    if (mesh.indexMemory) { vkFreeMemory(impl_->device, mesh.indexMemory, nullptr); mesh.indexMemory = VK_NULL_HANDLE; }
    if (mesh.vertexBuffer) { vkDestroyBuffer(impl_->device, mesh.vertexBuffer, nullptr); mesh.vertexBuffer = VK_NULL_HANDLE; }
    if (mesh.vertexMemory) { vkFreeMemory(impl_->device, mesh.vertexMemory, nullptr); mesh.vertexMemory = VK_NULL_HANDLE; }
    mesh.vertexCount = 0;
    mesh.indexCount = 0;
}

bool VulkanContext::uploadFontAtlasR8(const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h) {
    return impl_->uploadFontR8(pixels, w, h);
}

bool VulkanContext::beginFrame() {
    if (!isReady()) return false;
    const int fi = impl_->frameIdx;
    vkWaitForFences(impl_->device, 1, &impl_->inFlightFence[fi], VK_TRUE, UINT64_MAX);

    VkResult res = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                         impl_->imageAvailableSem[fi], VK_NULL_HANDLE,
                                         &impl_->currentImageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        return false;
    }

    vkResetFences(impl_->device, 1, &impl_->inFlightFence[fi]);
    VkCommandBuffer cmd = impl_->cmdBuffers[fi];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) return false;

    impl_->frameActive = true;
    impl_->dynamic3dVertOffset = 0;
    impl_->ui2dVertOffset = 0;
    impl_->uiPipelineBound = false;
    impl_->worldPipelineBound = false;
    return true;
}

void VulkanContext::updateSceneUBO(const SceneUBO& ubo) {
    if (!impl_->frameActive) return;
    std::memcpy(impl_->sceneUboBufs[impl_->frameIdx].mapped, &ubo, sizeof(SceneUBO));
}

void VulkanContext::beginShadowPass() {
    if (!impl_->frameActive) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];

    VkClearValue clearDepth{};
    clearDepth.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = impl_->shadowRenderPass;
    rpbi.framebuffer = impl_->shadowFramebuffer;
    rpbi.renderArea.extent = {kShadowMapSize, kShadowMapSize};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearDepth;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {kShadowMapSize, kShadowMapSize}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->depthPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->scenePipelineLayout,
                            0, 1, &impl_->sceneDescSets[impl_->frameIdx], 0, nullptr);
}

void VulkanContext::drawShadowMesh(const GpuMesh& mesh, const Mat4& model) {
    if (!impl_->frameActive || !mesh.valid()) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];
    DrawPush push{};
    push.model = model;
    vkCmdPushConstants(cmd, impl_->scenePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(DrawPush), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &off);
    if (mesh.indexCount > 0) {
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, mesh.vertexCount, 1, 0, 0);
    }
}

void VulkanContext::endShadowPass() {
    if (!impl_->frameActive) return;
    vkCmdEndRenderPass(impl_->cmdBuffers[impl_->frameIdx]);
}

void VulkanContext::beginScenePass(float clearR, float clearG, float clearB) {
    if (!impl_->frameActive) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];

    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{clearR, clearG, clearB, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = impl_->sceneRenderPass;
    rpbi.framebuffer = impl_->sceneFramebuffer;
    rpbi.renderArea.extent = impl_->swapchainExtent;
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f,
                  static_cast<float>(impl_->swapchainExtent.width),
                  static_cast<float>(impl_->swapchainExtent.height), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, impl_->swapchainExtent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->scenePipelineLayout,
                            0, 1, &impl_->sceneDescSets[impl_->frameIdx], 0, nullptr);
    impl_->worldPipelineBound = false;
}

void VulkanContext::bindWorldPipeline(WorldPipelineKind kind) {
    if (!impl_->frameActive) return;
    if (impl_->worldPipelineBound && impl_->activeWorldPipeline == kind) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];
    VkPipeline pipe = impl_->worldOpaquePipeline;
    switch (kind) {
        case WorldPipelineKind::Opaque:   pipe = impl_->worldOpaquePipeline; break;
        case WorldPipelineKind::Alpha:    pipe = impl_->worldAlphaPipeline; break;
        case WorldPipelineKind::Additive: pipe = impl_->worldAdditivePipeline; break;
        case WorldPipelineKind::Sky:      pipe = impl_->skyPipeline; break;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    impl_->activeWorldPipeline = kind;
    impl_->worldPipelineBound = true;
}

void VulkanContext::clearSceneDepth() {
    if (!impl_->frameActive) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];
    VkClearAttachment att{};
    att.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    att.clearValue.depthStencil = {1.0f, 0};
    VkClearRect rect{};
    rect.rect = {{0, 0}, impl_->swapchainExtent};
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;
    vkCmdClearAttachments(cmd, 1, &att, 1, &rect);
}

void VulkanContext::drawWorldMesh(const GpuMesh& mesh, const DrawPush& push) {
    if (!impl_->frameActive || !mesh.valid()) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];
    vkCmdPushConstants(cmd, impl_->scenePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(DrawPush), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &off);
    if (mesh.indexCount > 0) {
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, mesh.vertexCount, 1, 0, 0);
    }
}

void VulkanContext::drawDynamicTriangles3D(const Vertex3D* verts, std::uint32_t vertCount,
                                           WorldPipelineKind kind, const DrawPush& push) {
    if (!impl_->frameActive || !verts || vertCount == 0) return;
    if (impl_->dynamic3dVertOffset + vertCount > kMaxDynamic3dVerts) return;

    const int fi = impl_->frameIdx;
    auto* dst = static_cast<Vertex3D*>(impl_->dynamic3dBufs[fi].mapped) + impl_->dynamic3dVertOffset;
    std::memcpy(dst, verts, vertCount * sizeof(Vertex3D));

    bindWorldPipeline(kind);
    VkCommandBuffer cmd = impl_->cmdBuffers[fi];
    vkCmdPushConstants(cmd, impl_->scenePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(DrawPush), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &impl_->dynamic3dBufs[fi].buffer, &off);
    vkCmdDraw(cmd, vertCount, 1, impl_->dynamic3dVertOffset, 0);
    impl_->dynamic3dVertOffset += vertCount;
}

void VulkanContext::endScenePass() {
    if (!impl_->frameActive) return;
    vkCmdEndRenderPass(impl_->cmdBuffers[impl_->frameIdx]);
}

void VulkanContext::beginSwapchainPostAndUiPass(const PostPush& postPush) {
    if (!impl_->frameActive) return;
    VkCommandBuffer cmd = impl_->cmdBuffers[impl_->frameIdx];

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = impl_->swapchainRenderPass;
    rpbi.framebuffer = impl_->swapchainFramebuffers[impl_->currentImageIndex];
    rpbi.renderArea.extent = impl_->swapchainExtent;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f,
                  static_cast<float>(impl_->swapchainExtent.width),
                  static_cast<float>(impl_->swapchainExtent.height), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, impl_->swapchainExtent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // Draw fullscreen post-process triangle (HDR Kawase Bloom + ACES + sRGB encode)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->postPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->postPipelineLayout,
                            0, 1, &impl_->postDescSet, 0, nullptr);
    vkCmdPushConstants(cmd, impl_->postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PostPush), &postPush);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    impl_->uiPipelineBound = false;
}

void VulkanContext::setUiScissor(int x, int y, int w, int h) {
    if (!impl_->frameActive) return;
    const int sx = std::clamp(x, 0, static_cast<int>(impl_->swapchainExtent.width));
    const int sy = std::clamp(y, 0, static_cast<int>(impl_->swapchainExtent.height));
    const int ex = std::clamp(x + w, sx, static_cast<int>(impl_->swapchainExtent.width));
    const int ey = std::clamp(y + h, sy, static_cast<int>(impl_->swapchainExtent.height));
    VkRect2D sc{{sx, sy}, {static_cast<std::uint32_t>(ex - sx), static_cast<std::uint32_t>(ey - sy)}};
    vkCmdSetScissor(impl_->cmdBuffers[impl_->frameIdx], 0, 1, &sc);
}

void VulkanContext::resetUiScissor() {
    if (!impl_->frameActive) return;
    VkRect2D sc{{0, 0}, impl_->swapchainExtent};
    vkCmdSetScissor(impl_->cmdBuffers[impl_->frameIdx], 0, 1, &sc);
}

void VulkanContext::drawUiVertices(const Vertex2D* verts, std::uint32_t vertCount) {
    if (!impl_->frameActive || !verts || vertCount == 0) return;
    if (impl_->ui2dVertOffset + vertCount > kMaxUi2dVerts) return;

    const int fi = impl_->frameIdx;
    VkCommandBuffer cmd = impl_->cmdBuffers[fi];
    if (!impl_->uiPipelineBound) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->ui2dPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->uiPipelineLayout,
                                0, 1, &impl_->uiDescSet, 0, nullptr);
        UiPush push{};
        push.screenScale[0] = 2.0f / std::max(1.0f, static_cast<float>(impl_->swapchainExtent.width));
        push.screenScale[1] = 2.0f / std::max(1.0f, static_cast<float>(impl_->swapchainExtent.height));
        vkCmdPushConstants(cmd, impl_->uiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(UiPush), &push);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &impl_->ui2dBufs[fi].buffer, &off);
        impl_->uiPipelineBound = true;
    }

    auto* dst = static_cast<Vertex2D*>(impl_->ui2dBufs[fi].mapped) + impl_->ui2dVertOffset;
    std::memcpy(dst, verts, vertCount * sizeof(Vertex2D));
    vkCmdDraw(cmd, vertCount, 1, impl_->ui2dVertOffset, 0);
    impl_->ui2dVertOffset += vertCount;
}

void VulkanContext::endSwapchainPostAndUiPass() {
    if (!impl_->frameActive) return;
    vkCmdEndRenderPass(impl_->cmdBuffers[impl_->frameIdx]);
}

bool VulkanContext::endFrameAndPresent(const std::string* screenshotPngPath) {
    if (!impl_->frameActive) return false;
    const int fi = impl_->frameIdx;
    VkCommandBuffer cmd = impl_->cmdBuffers[fi];

    const bool doCapture = (screenshotPngPath != nullptr && !screenshotPngPath->empty() &&
                            impl_->swapchainSupportsTransferSrc);
    AllocatedBuffer captureBuf{};
    if (doCapture) {
        const VkDeviceSize byteSize =
            static_cast<VkDeviceSize>(impl_->swapchainExtent.width) *
            static_cast<VkDeviceSize>(impl_->swapchainExtent.height) * 4u;
        if (impl_->createBuffer(byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                captureBuf, true)) {
            VkImage scImg = impl_->swapchainImages[impl_->currentImageIndex];
            VkImageMemoryBarrier toSrc{};
            toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image = scImg;
            toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {impl_->swapchainExtent.width, impl_->swapchainExtent.height, 1};
            vkCmdCopyImageToBuffer(cmd, scImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   captureBuf.buffer, 1, &region);

            VkImageMemoryBarrier toPres = toSrc;
            toPres.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toPres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPres.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toPres.dstAccessMask = 0;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPres);
        }
    }

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &impl_->imageAvailableSem[fi];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &impl_->renderFinishedSem[fi];

    if (vkQueueSubmit(impl_->graphicsQueue, 1, &si, impl_->inFlightFence[fi]) != VK_SUCCESS) {
        impl_->destroyBuffer(captureBuf);
        impl_->frameActive = false;
        return false;
    }

    if (captureBuf.mapped != nullptr) {
        vkWaitForFences(impl_->device, 1, &impl_->inFlightFence[fi], VK_TRUE, UINT64_MAX);
        const std::uint32_t w = impl_->swapchainExtent.width;
        const std::uint32_t h = impl_->swapchainExtent.height;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4u);
        const auto* src = static_cast<const std::uint8_t*>(captureBuf.mapped);
        const bool isBgra = (impl_->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                             impl_->swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
            const std::size_t k = i * 4u;
            if (isBgra) {
                rgba[k + 0] = src[k + 2];
                rgba[k + 1] = src[k + 1];
                rgba[k + 2] = src[k + 0];
                rgba[k + 3] = 255;
            } else {
                rgba[k + 0] = src[k + 0];
                rgba[k + 1] = src[k + 1];
                rgba[k + 2] = src[k + 2];
                rgba[k + 3] = 255;
            }
        }
        stbi_write_png(screenshotPngPath->c_str(), static_cast<int>(w), static_cast<int>(h),
                       4, rgba.data(), static_cast<int>(w * 4u));
        OC_VK_LOG("Saved Vulkan screenshot to %s (%ux%u)", screenshotPngPath->c_str(), w, h);
        impl_->destroyBuffer(captureBuf);
    }

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &impl_->renderFinishedSem[fi];
    pi.swapchainCount = 1;
    pi.pSwapchains = &impl_->swapchain;
    pi.pImageIndices = &impl_->currentImageIndex;
    VkResult presRes = vkQueuePresentKHR(impl_->presentQueue, &pi);

    impl_->frameIdx = (impl_->frameIdx + 1) % kMaxFramesInFlight;
    impl_->frameActive = false;
    return presRes == VK_SUCCESS;
}

} // namespace oc
