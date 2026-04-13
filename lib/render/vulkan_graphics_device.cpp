#include "cw/render/vulkan_graphics_device.hpp"

#include "cw/render/gl_window.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cw::render {

namespace {

PFN_vkCreateWin32SurfaceKHR g_vkCreateWin32SurfaceKHR = nullptr;
PFN_vkDestroySurfaceKHR g_vkDestroySurfaceKHR = nullptr;

bool load_instance_procs(VkInstance inst) {
  g_vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
      vkGetInstanceProcAddr(inst, "vkCreateWin32SurfaceKHR"));
  g_vkDestroySurfaceKHR =
      reinterpret_cast<PFN_vkDestroySurfaceKHR>(vkGetInstanceProcAddr(inst, "vkDestroySurfaceKHR"));
  return g_vkCreateWin32SurfaceKHR != nullptr && g_vkDestroySurfaceKHR != nullptr;
}

std::size_t align_up(std::size_t v, std::size_t a) noexcept {
  if (a == 0) {
    return v;
  }
  return (v + a - 1U) / a * a;
}

uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_bits, VkMemoryPropertyFlags props) noexcept {
  VkPhysicalDeviceMemoryProperties mp{};
  vkGetPhysicalDeviceMemoryProperties(physical, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((((type_bits >> i) & 1U) != 0U) &&
        (mp.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  return UINT32_MAX;
}

}  // namespace

struct VulkanGraphicsDevice::Impl {
  GlWindow* win = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swap_format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> swap_images;
  std::vector<VkImageView> swap_views;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> cmd_bufs;
  VkSemaphore sem_image_avail = VK_NULL_HANDLE;
  VkSemaphore sem_render_done = VK_NULL_HANDLE;
  /// One fence per swapchain image: wait after `vkAcquireNextImageKHR` so the GPU can overlap
  /// frames and acquire failures do not leave a waited-but-never-reset fence stuck unsignaled.
  std::vector<VkFence> image_fences;
  uint32_t image_index = 0;
  bool skip_submit = false;
  bool valid = false;

  VkBuffer staging_buf = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem = VK_NULL_HANDLE;
  void* staging_mapped = nullptr;
  VkDeviceSize staging_capacity = 0;
  bool pending_scene = false;
  int pending_w = 0;
  int pending_h = 0;
  std::size_t pending_padded_row = 0;
  std::size_t copy_row_pitch_align = 256;

  bool native_clear_only_ = false;
  float native_clear_rgba[4] = {0.02F, 0.03F, 0.05F, 1.F};

  ~Impl() { cleanup_all(); }

  void cache_copy_row_align() noexcept {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(phys, &p);
    const std::size_t a = static_cast<std::size_t>(p.limits.optimalBufferCopyRowPitchAlignment);
    copy_row_pitch_align = (a < 4) ? 256 : std::max<std::size_t>(4, a);
  }

  void destroy_staging() noexcept {
    if (device == VK_NULL_HANDLE) {
      staging_buf = VK_NULL_HANDLE;
      staging_mem = VK_NULL_HANDLE;
      staging_mapped = nullptr;
      staging_capacity = 0;
      return;
    }
    if (staging_mem != VK_NULL_HANDLE && staging_mapped != nullptr) {
      vkUnmapMemory(device, staging_mem);
    }
    staging_mapped = nullptr;
    if (staging_buf != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, staging_buf, nullptr);
      staging_buf = VK_NULL_HANDLE;
    }
    if (staging_mem != VK_NULL_HANDLE) {
      vkFreeMemory(device, staging_mem, nullptr);
      staging_mem = VK_NULL_HANDLE;
    }
    staging_capacity = 0;
  }

  [[nodiscard]] bool ensure_staging_total(VkDeviceSize need) noexcept {
    if (need < 1) {
      return false;
    }
    if (need <= staging_capacity && staging_buf != VK_NULL_HANDLE && staging_mapped != nullptr) {
      return true;
    }
    destroy_staging();
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = need;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &staging_buf) != VK_SUCCESS) {
      return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, staging_buf, &req);
    const uint32_t mt = find_memory_type(phys, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) {
      vkDestroyBuffer(device, staging_buf, nullptr);
      staging_buf = VK_NULL_HANDLE;
      return false;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device, &ai, nullptr, &staging_mem) != VK_SUCCESS) {
      vkDestroyBuffer(device, staging_buf, nullptr);
      staging_buf = VK_NULL_HANDLE;
      return false;
    }
    if (vkBindBufferMemory(device, staging_buf, staging_mem, 0) != VK_SUCCESS) {
      vkFreeMemory(device, staging_mem, nullptr);
      vkDestroyBuffer(device, staging_buf, nullptr);
      staging_mem = VK_NULL_HANDLE;
      staging_buf = VK_NULL_HANDLE;
      return false;
    }
    if (vkMapMemory(device, staging_mem, 0, VK_WHOLE_SIZE, 0, &staging_mapped) != VK_SUCCESS) {
      vkDestroyBuffer(device, staging_buf, nullptr);
      vkFreeMemory(device, staging_mem, nullptr);
      staging_buf = VK_NULL_HANDLE;
      staging_mem = VK_NULL_HANDLE;
      return false;
    }
    staging_capacity = need;
    return true;
  }

  void record_scene_transfer(uint32_t idx) noexcept {
    VkCommandBuffer cb = cmd_bufs[idx];
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = swap_images[idx];
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    to_transfer.srcAccessMask = 0;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_transfer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = static_cast<uint32_t>(pending_padded_row / 4U);
    region.bufferImageHeight = static_cast<uint32_t>(pending_h);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(pending_w), static_cast<uint32_t>(pending_h), 1U};
    vkCmdCopyBufferToImage(cb, staging_buf, swap_images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_present{};
    to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = swap_images[idx];
    to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_present.subresourceRange.levelCount = 1;
    to_present.subresourceRange.layerCount = 1;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_present);

    vkEndCommandBuffer(cb);
  }

  void cleanup_all() {
    if (device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device);
    }
    if (device != VK_NULL_HANDLE && sem_image_avail != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, sem_image_avail, nullptr);
      sem_image_avail = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE && sem_render_done != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, sem_render_done, nullptr);
      sem_render_done = VK_NULL_HANDLE;
    }
    destroy_swapchain_resources();
    destroy_staging();
    if (device != VK_NULL_HANDLE) {
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE && g_vkDestroySurfaceKHR != nullptr) {
      g_vkDestroySurfaceKHR(instance, surface, nullptr);
      surface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
    }
    phys = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    valid = false;
  }

  void destroy_swapchain_resources() {
    if (device == VK_NULL_HANDLE) {
      return;
    }
    for (VkFence& f : image_fences) {
      if (f != VK_NULL_HANDLE) {
        vkDestroyFence(device, f, nullptr);
        f = VK_NULL_HANDLE;
      }
    }
    image_fences.clear();
    if (cmd_pool != VK_NULL_HANDLE && !cmd_bufs.empty()) {
      vkFreeCommandBuffers(device, cmd_pool, static_cast<uint32_t>(cmd_bufs.size()), cmd_bufs.data());
    }
    cmd_bufs.clear();
    if (cmd_pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, cmd_pool, nullptr);
      cmd_pool = VK_NULL_HANDLE;
    }
    for (VkFramebuffer fb : framebuffers) {
      if (fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, fb, nullptr);
      }
    }
    framebuffers.clear();
    if (render_pass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device, render_pass, nullptr);
      render_pass = VK_NULL_HANDLE;
    }
    for (VkImageView v : swap_views) {
      if (v != VK_NULL_HANDLE) {
        vkDestroyImageView(device, v, nullptr);
      }
    }
    swap_views.clear();
    swap_images.clear();
    if (swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device, swapchain, nullptr);
      swapchain = VK_NULL_HANDLE;
    }
  }

  bool create_instance() {
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Clockwork";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_2;
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = sizeof(exts) / sizeof(exts[0]);
    ci.ppEnabledExtensionNames = exts;
    return vkCreateInstance(&ci, nullptr, &instance) == VK_SUCCESS && load_instance_procs(instance);
  }

  bool create_surface() {
    HWND hwnd = static_cast<HWND>(win->native_window_handle());
    if (hwnd == nullptr) {
      return false;
    }
    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = hwnd;
    return g_vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) == VK_SUCCESS;
  }

  bool pick_physical() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (n == 0) {
      return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    for (VkPhysicalDevice d : devs) {
      uint32_t qn = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qprops(qn);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qprops.data());
      for (uint32_t i = 0; i < qn; ++i) {
        VkBool32 present_ok = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present_ok);
        if (present_ok && (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
          phys = d;
          queue_family = i;
          return true;
        }
      }
    }
    return false;
  }

  bool create_logical_device() {
    float qp = 1.F;
    VkDeviceQueueCreateInfo dq{};
    dq.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dq.queueFamilyIndex = queue_family;
    dq.queueCount = 1;
    dq.pQueuePriorities = &qp;
    const char* dex[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dq;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dex;
    if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) {
      return false;
    }
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    return true;
  }

  bool choose_surface_format(VkSurfaceFormatKHR& out_fmt) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &n, nullptr);
    if (n == 0) {
      return false;
    }
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &n, fmts.data());
    for (const auto& f : fmts) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        out_fmt = f;
        return true;
      }
    }
    out_fmt = fmts[0];
    return true;
  }

  VkPresentModeKHR choose_present_mode() {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, modes.data());
    for (VkPresentModeKHR m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
        return m;
      }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  bool create_swapchain_internal() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    VkSurfaceFormatKHR sf{};
    if (!choose_surface_format(sf)) {
      return false;
    }
    swap_format = sf.format;

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) {
      const int cw = std::max(1, win->client_width());
      const int ch = std::max(1, win->client_height());
      ext.width = static_cast<uint32_t>(
          std::clamp(cw, static_cast<int>(caps.minImageExtent.width),
                     static_cast<int>(caps.maxImageExtent.width)));
      ext.height = static_cast<uint32_t>(
          std::clamp(ch, static_cast<int>(caps.minImageExtent.height),
                     static_cast<int>(caps.maxImageExtent.height)));
    }
    extent = ext;

    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount) {
      img_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = img_count;
    sci.imageFormat = swap_format;
    sci.imageColorSpace = sf.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = choose_present_mode();
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
      return false;
    }

    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &ic, nullptr);
    swap_images.resize(ic);
    vkGetSwapchainImagesKHR(device, swapchain, &ic, swap_images.data());

    swap_views.resize(ic);
    for (uint32_t i = 0; i < ic; ++i) {
      VkImageViewCreateInfo iv{};
      iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      iv.image = swap_images[i];
      iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
      iv.format = swap_format;
      iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      iv.subresourceRange.levelCount = 1;
      iv.subresourceRange.layerCount = 1;
      if (vkCreateImageView(device, &iv, nullptr, &swap_views[i]) != VK_SUCCESS) {
        return false;
      }
    }
    return true;
  }

  bool create_render_pass() {
    VkAttachmentDescription color{};
    color.format = swap_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    return vkCreateRenderPass(device, &rp, nullptr, &render_pass) == VK_SUCCESS;
  }

  bool create_framebuffers() {
    framebuffers.resize(swap_views.size());
    for (std::size_t i = 0; i < swap_views.size(); ++i) {
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = render_pass;
      fb.attachmentCount = 1;
      fb.pAttachments = &swap_views[i];
      fb.width = extent.width;
      fb.height = extent.height;
      fb.layers = 1;
      if (vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]) != VK_SUCCESS) {
        return false;
      }
    }
    return true;
  }

  bool create_command_pool_and_buffers() {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queue_family;
    if (vkCreateCommandPool(device, &pci, nullptr, &cmd_pool) != VK_SUCCESS) {
      return false;
    }
    cmd_bufs.resize(framebuffers.size());
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(cmd_bufs.size());
    return vkAllocateCommandBuffers(device, &ai, cmd_bufs.data()) == VK_SUCCESS;
  }

  bool create_sync() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    return vkCreateSemaphore(device, &si, nullptr, &sem_image_avail) == VK_SUCCESS &&
           vkCreateSemaphore(device, &si, nullptr, &sem_render_done) == VK_SUCCESS;
  }

  [[nodiscard]] bool create_image_fences() noexcept {
    image_fences.resize(swap_images.size());
    for (std::size_t i = 0; i < image_fences.size(); ++i) {
      VkFenceCreateInfo fi{};
      fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      if (vkCreateFence(device, &fi, nullptr, &image_fences[i]) != VK_SUCCESS) {
        for (std::size_t j = 0; j < i; ++j) {
          vkDestroyFence(device, image_fences[j], nullptr);
          image_fences[j] = VK_NULL_HANDLE;
        }
        image_fences.clear();
        return false;
      }
    }
    return true;
  }

  void record_clear(uint32_t idx) {
    VkCommandBuffer cb = cmd_bufs[idx];
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass;
    rp.framebuffer = framebuffers[idx];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent;
    VkClearValue clear{};
    if (native_clear_only_) {
      clear.color.float32[0] = native_clear_rgba[0];
      clear.color.float32[1] = native_clear_rgba[1];
      clear.color.float32[2] = native_clear_rgba[2];
      clear.color.float32[3] = native_clear_rgba[3];
    } else {
      // 与 OpenGL 路径 `glClearColor(0.02f, 0.03f, 0.05f, 1.f)` 一致。
      clear.color = {{0.02F, 0.03F, 0.05F, 1.F}};
    }
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
  }

  bool recreate_swapchain() {
    if (device == VK_NULL_HANDLE) {
      return false;
    }
    vkDeviceWaitIdle(device);
    destroy_swapchain_resources();
    if (!create_swapchain_internal()) {
      return false;
    }
    if (!create_render_pass()) {
      return false;
    }
    if (!create_framebuffers()) {
      return false;
    }
    if (!create_command_pool_and_buffers()) {
      return false;
    }
    return create_image_fences();
  }

  bool build_swapchain_pipeline() {
    if (!create_swapchain_internal()) {
      return false;
    }
    if (!create_render_pass()) {
      return false;
    }
    if (!create_framebuffers()) {
      return false;
    }
    if (!create_command_pool_and_buffers()) {
      return false;
    }
    return create_image_fences();
  }

  void sync_extent_from_window() {
    const uint32_t cw = static_cast<uint32_t>(std::max(1, win->client_width()));
    const uint32_t ch = static_cast<uint32_t>(std::max(1, win->client_height()));
    if (swapchain != VK_NULL_HANDLE && (cw != extent.width || ch != extent.height)) {
      static_cast<void>(recreate_swapchain());
    }
  }
};

VulkanGraphicsDevice::VulkanGraphicsDevice(GlWindow& window)
    : impl_(std::make_unique<Impl>()), window_(window) {
  impl_->win = &window;
}

VulkanGraphicsDevice::~VulkanGraphicsDevice() = default;

bool VulkanGraphicsDevice::initialize() noexcept {
  if (!impl_->create_instance()) {
    return false;
  }
  if (!impl_->create_surface()) {
    impl_->cleanup_all();
    return false;
  }
  if (!impl_->pick_physical()) {
    impl_->cleanup_all();
    return false;
  }
  if (!impl_->create_logical_device()) {
    impl_->cleanup_all();
    return false;
  }
  if (!impl_->build_swapchain_pipeline()) {
    impl_->cleanup_all();
    return false;
  }
  if (!impl_->create_sync()) {
    impl_->cleanup_all();
    return false;
  }
  impl_->cache_copy_row_align();
  impl_->valid = true;
  return true;
}

void VulkanGraphicsDevice::set_viewport(int, int, int, int) noexcept {
  impl_->sync_extent_from_window();
}

void VulkanGraphicsDevice::begin_frame() noexcept {
  if (!impl_->valid || impl_->device == VK_NULL_HANDLE) {
    impl_->skip_submit = true;
    impl_->pending_scene = false;
    return;
  }
  impl_->sync_extent_from_window();
  impl_->skip_submit = false;
  VkResult ar = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX, impl_->sem_image_avail,
                                       VK_NULL_HANDLE, &impl_->image_index);
  if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_SUBOPTIMAL_KHR) {
    static_cast<void>(impl_->recreate_swapchain());
    impl_->skip_submit = true;
    impl_->pending_scene = false;
    return;
  }
  if (ar != VK_SUCCESS) {
    impl_->skip_submit = true;
    impl_->pending_scene = false;
    return;
  }
  if (impl_->image_index >= impl_->image_fences.size() ||
      impl_->image_index >= impl_->cmd_bufs.size()) {
    impl_->skip_submit = true;
    impl_->pending_scene = false;
    return;
  }
  vkWaitForFences(impl_->device, 1, &impl_->image_fences[impl_->image_index], VK_TRUE, UINT64_MAX);
  vkResetFences(impl_->device, 1, &impl_->image_fences[impl_->image_index]);
  vkResetCommandBuffer(impl_->cmd_bufs[impl_->image_index], 0);
}

void VulkanGraphicsDevice::end_frame() noexcept {
  if (!impl_->valid || impl_->skip_submit) {
    impl_->pending_scene = false;
    return;
  }
  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (impl_->pending_scene && impl_->staging_buf != VK_NULL_HANDLE && impl_->staging_mapped != nullptr &&
      impl_->pending_w == static_cast<int>(impl_->extent.width) &&
      impl_->pending_h == static_cast<int>(impl_->extent.height)) {
    impl_->record_scene_transfer(impl_->image_index);
    wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    impl_->pending_scene = false;
  } else {
    if (impl_->pending_scene) {
      impl_->pending_scene = false;
    }
    impl_->record_clear(impl_->image_index);
  }
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &impl_->sem_image_avail;
  si.pWaitDstStageMask = &wait_stage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &impl_->cmd_bufs[impl_->image_index];
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &impl_->sem_render_done;
  static_cast<void>(vkQueueSubmit(impl_->queue, 1, &si, impl_->image_fences[impl_->image_index]));
}

void VulkanGraphicsDevice::present() noexcept {
  if (!impl_->valid || impl_->skip_submit) {
    return;
  }
  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &impl_->sem_render_done;
  pi.swapchainCount = 1;
  pi.pSwapchains = &impl_->swapchain;
  pi.pImageIndices = &impl_->image_index;
  VkResult pr = vkQueuePresentKHR(impl_->queue, &pi);
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
    static_cast<void>(impl_->recreate_swapchain());
  }
}

void VulkanGraphicsDevice::upload_swapchain_from_cpu_bgra(int width, int height, std::size_t row_bytes,
                                                            const unsigned char* pixels) noexcept {
  if (impl_->native_clear_only_) {
    impl_->pending_scene = false;
    return;
  }
  if (!impl_->valid || pixels == nullptr || width < 1 || height < 1 ||
      row_bytes < static_cast<std::size_t>(width) * 4U || impl_->device == VK_NULL_HANDLE) {
    impl_->pending_scene = false;
    return;
  }
  const std::size_t pr = align_up(row_bytes, impl_->copy_row_pitch_align);
  const VkDeviceSize total =
      static_cast<VkDeviceSize>(pr) * static_cast<VkDeviceSize>(static_cast<std::size_t>(height));
  if (!impl_->ensure_staging_total(total)) {
    impl_->pending_scene = false;
    return;
  }
  auto* dst = static_cast<unsigned char*>(impl_->staging_mapped);
  for (int y = 0; y < height; ++y) {
    const unsigned char* src_row = pixels + static_cast<std::size_t>(height - 1 - y) * row_bytes;
    unsigned char* dst_row = dst + static_cast<std::size_t>(y) * pr;
    std::memcpy(dst_row, src_row, row_bytes);
    if (pr > row_bytes) {
      std::memset(dst_row + row_bytes, 0, pr - row_bytes);
    }
  }
  impl_->pending_w = width;
  impl_->pending_h = height;
  impl_->pending_padded_row = pr;
  impl_->pending_scene = true;
}

void VulkanGraphicsDevice::set_vulkan_native_scene_clear_only(bool enabled) noexcept {
  if (impl_ != nullptr) {
    impl_->native_clear_only_ = enabled;
  }
}

bool VulkanGraphicsDevice::vulkan_native_scene_clear_only() const noexcept {
  return impl_ != nullptr && impl_->native_clear_only_;
}

void VulkanGraphicsDevice::set_vulkan_native_scene_anim_param(float sim_time_seconds) noexcept {
  if (impl_ == nullptr || !impl_->valid) {
    return;
  }
  const float u = 0.5F + 0.5F * std::sin(static_cast<double>(sim_time_seconds) * 0.4);
  impl_->native_clear_rgba[0] = 0.02F + 0.05F * static_cast<float>(u);
  impl_->native_clear_rgba[1] = 0.03F + 0.04F * static_cast<float>(1.0 - u);
  impl_->native_clear_rgba[2] = 0.05F + 0.10F * static_cast<float>(u);
  impl_->native_clear_rgba[3] = 1.F;
}

}  // namespace cw::render
