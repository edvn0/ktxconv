#include <format>
#include <memory>
#include <source_location>
#include <volk.h>
#include <vulkan/vulkan_core.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <VkBootstrap.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libpng18/png.h>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usize = std::size_t;

namespace {
constexpr u32 image_width{1024};
constexpr u32 image_height{1024};

constexpr u32 max_input_images{1024};
constexpr u32 max_samplers{64};
constexpr u32 max_output_images{1024};

auto log_error(std::string_view msg,
               std::source_location loc = std::source_location::current())
    -> void {
  std::cerr << std::format("[{}:{}] {}\n", loc.file_name(), loc.line(), msg);
}

[[noreturn]] auto
fatal(std::string_view msg,
      std::source_location loc = std::source_location::current()) -> void {
  log_error(msg, loc);
  throw std::runtime_error(std::string(msg));
}

auto check(VkResult result, std::string_view what,
           std::source_location loc = std::source_location::current()) -> void {
  if (result != VK_SUCCESS)
    fatal(std::format("vulkan error in {}: {}", loc.function_name(), what),
          loc);
}

auto flush_allocation(
    VmaAllocator allocator, VmaAllocation allocation, std::string_view what,
    std::source_location loc = std::source_location::current()) -> void {
  check(vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE), what, loc);
}

auto invalidate_allocation(
    VmaAllocator allocator, VmaAllocation allocation, std::string_view what,
    std::source_location loc = std::source_location::current()) -> void {
  check(vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE), what,
        loc);
}

auto debug_utils_available(VkDevice device) -> bool {
  return vkSetDebugUtilsObjectNameEXT != nullptr && device != VK_NULL_HANDLE;
}

template <typename Handle>
auto set_debug_name(VkDevice device, VkObjectType object_type, Handle handle,
                    std::string_view name) -> void {
  if (!debug_utils_available(device))
    return;

  if (handle == VK_NULL_HANDLE)
    return;

  VkDebugUtilsObjectNameInfoEXT name_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = object_type,
      .objectHandle =
          static_cast<u64>(reinterpret_cast<std::uintptr_t>(handle)),
      .pObjectName = name.data(),
  };

  vkSetDebugUtilsObjectNameEXT(device, &name_info);
}

auto set_vma_allocation_name(VmaAllocator allocator, VmaAllocation allocation,
                             std::string_view name) -> void {
  if (allocator == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE)
    return;

  vmaSetAllocationName(allocator, allocation, name.data());
}

namespace detail {
template <typename> struct non_copyable {
  non_copyable(non_copyable const &) = delete;
  auto operator=(non_copyable const &) -> non_copyable & = delete;

protected:
  non_copyable() = default;
  ~non_copyable() = default;
};

template <typename T, bool BufferOrImage> struct vma_destroy {
  auto device(this auto &&self) -> VkDevice {
    VmaAllocatorInfo alloc_info{};
    vmaGetAllocatorInfo(self.alloc, &alloc_info);
    return alloc_info.device;
  }

  auto instance(this auto &&self) -> VkInstance {
    VmaAllocatorInfo alloc_info{};
    vmaGetAllocatorInfo(self.alloc, &alloc_info);
    return alloc_info.instance;
  }

  auto destroy(this auto &&self) -> void {
    if constexpr (BufferOrImage) {
      if (self.buffer) {
        vmaDestroyBuffer(self.alloc, self.buffer, self.allocation);
      }
    } else {
      if (self.image) {
        vmaDestroyImage(self.alloc, self.image, self.allocation);
      }
    }
  }
};

} // namespace detail

struct OneTimeCommand {
  struct Submission : detail::non_copyable<Submission> {
    Submission() = default;

    Submission(Submission &&other) noexcept
        : device(std::exchange(other.device, VK_NULL_HANDLE)),
          queue(std::exchange(other.queue, VK_NULL_HANDLE)),
          command_pool(std::exchange(other.command_pool, VK_NULL_HANDLE)),
          command_buffer(std::exchange(other.command_buffer, VK_NULL_HANDLE)),
          fence(std::exchange(other.fence, VK_NULL_HANDLE)),
          completed(std::exchange(other.completed, true)) {}

    auto operator=(Submission &&other) noexcept -> Submission & {
      if (this == &other)
        return *this;

      destroy();

      device = std::exchange(other.device, VK_NULL_HANDLE);
      queue = std::exchange(other.queue, VK_NULL_HANDLE);
      command_pool = std::exchange(other.command_pool, VK_NULL_HANDLE);
      command_buffer = std::exchange(other.command_buffer, VK_NULL_HANDLE);
      fence = std::exchange(other.fence, VK_NULL_HANDLE);
      completed = std::exchange(other.completed, true);
      return *this;
    }

    ~Submission() { destroy(); }

    auto wait() -> void {
      if (completed || device == VK_NULL_HANDLE || fence == VK_NULL_HANDLE)
        return;

      check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
      completed = true;
    }

    auto destroy() -> void {
      if (device == VK_NULL_HANDLE)
        return;

      wait();

      if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
      }

      if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;
      }

      command_buffer = VK_NULL_HANDLE;
      queue = VK_NULL_HANDLE;
      device = VK_NULL_HANDLE;
    }

    VkDevice device{};
    VkQueue queue{};
    VkCommandPool command_pool{};
    VkCommandBuffer command_buffer{};
    VkFence fence{};
    bool completed{true};
  };

  VkDevice device{};
  VkQueue queue{};
  u32 queue_family_index{};
  std::string_view debug_name{"one_time_command"};

  template <typename Func> auto submit(Func &&func) const -> Submission {
    Submission submission{};
    submission.device = device;
    submission.queue = queue;
    submission.completed = false;

    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue_family_index,
    };

    check(vkCreateCommandPool(device, &pool_info, nullptr,
                              &submission.command_pool),
          "vkCreateCommandPool");

    if (!debug_name.empty()) {
      set_debug_name(device, VK_OBJECT_TYPE_COMMAND_POOL,
                     submission.command_pool,
                     std::format("{}.command_pool", debug_name));
    }

    VkCommandBufferAllocateInfo cmd_alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = submission.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    check(vkAllocateCommandBuffers(device, &cmd_alloc,
                                   &submission.command_buffer),
          "vkAllocateCommandBuffers");

    if (!debug_name.empty()) {
      set_debug_name(device, VK_OBJECT_TYPE_COMMAND_BUFFER,
                     submission.command_buffer,
                     std::format("{}.command_buffer", debug_name));
    }

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    check(vkBeginCommandBuffer(submission.command_buffer, &begin_info),
          "vkBeginCommandBuffer");

    std::forward<Func>(func)(submission.command_buffer);

    check(vkEndCommandBuffer(submission.command_buffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    check(vkCreateFence(device, &fence_info, nullptr, &submission.fence),
          "vkCreateFence");
    if (!debug_name.empty()) {
      set_debug_name(device, VK_OBJECT_TYPE_FENCE, submission.fence,
                     std::format("{}.fence", debug_name));
    }

    VkCommandBufferSubmitInfo cmd_submit_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = submission.command_buffer,
    };

    VkSubmitInfo2 submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_submit_info,
    };

    check(vkQueueSubmit2(queue, 1, &submit_info, submission.fence),
          "vkQueueSubmit2");

    return submission;
  }
};

struct HostBuffer : detail::non_copyable<HostBuffer>,
                    detail::vma_destroy<HostBuffer, true> {
  static auto create(VmaAllocator &alloc, usize size,
                     std::string_view debug_name)
      -> std::unique_ptr<HostBuffer> {
    auto buffer = std::make_unique<HostBuffer>();
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
    buffer->alloc = alloc;

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(size),
        .usage =
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo alloc_info{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                 VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    };
    check(vmaCreateBuffer(alloc, &buffer_info, &alloc_info, &buffer->buffer,
                          &buffer->allocation, &buffer->allocation_info),
          "vmaCreateBuffer");
    set_vma_allocation_name(alloc, buffer->allocation, debug_name);

    set_debug_name(buffer->device(), VK_OBJECT_TYPE_BUFFER, buffer->buffer,
                   debug_name);
    return buffer;
  }

  ~HostBuffer() { destroy(); }

  VkBuffer buffer{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocation_info{};
  VmaAllocator alloc{};

  auto mapped_data() const -> void * { return allocation_info.pMappedData; }
};

struct Texture : detail::non_copyable<Texture>,
                 detail::vma_destroy<Texture, false> {
  VkImage image{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocation_info{};
  VmaAllocator alloc{};

  ~Texture() { destroy(); }

  struct Metadata {
    u32 width{};
    u32 height{};
    u32 mip_levels{};
    u32 array_layers{};
    VkFormat format{};
  } metadata{};

  auto upload(VkCommandBuffer cmd, VmaAllocator alloc,
              std::span<std::uint8_t const> data, HostBuffer &host_buffer,
              usize row_pitch) const -> void {
    usize const total_pixel_bytes = static_cast<usize>(metadata.width) *
                                    metadata.height * 4 * metadata.mip_levels *
                                    metadata.array_layers;
    assert(data.size() == total_pixel_bytes);

    std::vector<VkBufferImageCopy> copy_regions{};

    for (u32 layer = 0; layer < metadata.array_layers; ++layer) {
      for (u32 level = 0; level < metadata.mip_levels; ++level) {
        u32 const level_width = std::max(1u, metadata.width >> level);
        u32 const level_height = std::max(1u, metadata.height >> level);

        VkBufferImageCopy region{};
        region.bufferOffset =
            static_cast<VkDeviceSize>(layer * metadata.mip_levels + level) *
            static_cast<VkDeviceSize>(metadata.width) * metadata.height * 4;
        region.bufferRowLength = static_cast<u32>(row_pitch / 4);
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            level_width,
            level_height,
            1,
        };

        copy_regions.push_back(region);
      }
    }

    for (usize i = 0; i < copy_regions.size(); ++i) {
      auto const &region = copy_regions[i];
      auto const src_ptr = data.data() + region.bufferOffset;
      auto const dst_ptr =
          static_cast<std::uint8_t *>(host_buffer.mapped_data()) +
          region.bufferOffset;
      std::memcpy(dst_ptr, src_ptr,
                  static_cast<usize>(region.imageExtent.width) *
                      region.imageExtent.height * 4);
    }

    flush_allocation(alloc, host_buffer.allocation,
                     "vmaFlushAllocation(texture_upload)");

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = metadata.mip_levels,
        .baseArrayLayer = 0,
        .layerCount = metadata.array_layers,
    };

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);

    vkCmdCopyBufferToImage(
        cmd, host_buffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<u32>(copy_regions.size()), copy_regions.data());

    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &dependency);
  }

  static auto create(VmaAllocator &alloc, std::string_view name)
      -> std::unique_ptr<Texture> {
    auto tex = std::make_unique<Texture>();
    tex->alloc = alloc;
    set_debug_name(tex->device(), VK_OBJECT_TYPE_IMAGE, tex->image, name);
    return tex;
  }
};

struct DescriptorBuffer : detail::non_copyable<DescriptorBuffer>,
                          detail::vma_destroy<DescriptorBuffer, true> {
  static auto create(VmaAllocator &alloc, std::string_view name)
      -> std::unique_ptr<DescriptorBuffer> {
    auto buffer = std::make_unique<DescriptorBuffer>();
    buffer->alloc = alloc;
    set_debug_name(buffer->device(), VK_OBJECT_TYPE_BUFFER, buffer->buffer,
                   name);
    return buffer;
  }

  ~DescriptorBuffer() { destroy(); }

  VkBuffer buffer{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocation_info{};
  VkDeviceAddress address{};
  VmaAllocator alloc{};

  auto mapped_ptr(VkDeviceSize offset = 0) const -> void * {
    return static_cast<char *>(allocation_info.pMappedData) + offset;
  }
};

struct BindlessSet : detail::non_copyable<BindlessSet> {
  ~BindlessSet() {
    if (set_layout != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
  }

  auto layout_handle() const -> VkDescriptorSetLayout { return set_layout; }
  auto buffer_address() const -> VkDeviceAddress {
    return desc_buf ? desc_buf->address : 0u;
  }

  static auto create(VkDevice device, VmaAllocator allocator,
                     VkPhysicalDeviceDescriptorBufferPropertiesEXT const &props,
                     std::span<VkImageView const> sampled_images,
                     std::span<VkSampler const> samplers,
                     std::span<VkImageView const> storage_images,
                     std::string_view debug_name = "bindless_set")
      -> std::unique_ptr<BindlessSet> {
    auto bs = std::make_unique<BindlessSet>();
    bs->device = device;
    bs->allocator = allocator;
    bs->sampled_image_desc_size = props.sampledImageDescriptorSize;
    bs->sampler_desc_size = props.samplerDescriptorSize;
    bs->storage_image_desc_size = props.storageImageDescriptorSize;
    bs->debug_name = std::string(debug_name);
    bs->rebuild(static_cast<u32>(sampled_images.size()),
                static_cast<u32>(samplers.size()),
                static_cast<u32>(storage_images.size()));
    bs->write_all(sampled_images, samplers, storage_images);
    return bs;
  }

  auto populate(std::span<VkImageView const> sampled_images,
                std::span<VkSampler const> samplers,
                std::span<VkImageView const> storage_images) -> bool {
    bool const needs_rebuild =
        static_cast<u32>(sampled_images.size()) > sampled_image_capacity ||
        static_cast<u32>(samplers.size()) > sampler_capacity ||
        static_cast<u32>(storage_images.size()) > storage_image_capacity;

    if (needs_rebuild) {
      const auto next_power_of_2 = [](u32 v) -> u32 {
#ifdef __GNUC__
        if (v == 0)
          return 1;
        if ((v & (v - 1)) == 0)
          return v;
        return 1u << (32 - __builtin_clz(v));
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
        if (v == 0)
          return 1;
        if ((v & (v - 1)) == 0)
          return v;
#else
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
#endif
      };

      const auto new_sampled_images_size = next_power_of_2(std::max(
          static_cast<u32>(sampled_images.size()), sampled_image_capacity));
      const auto new_samplers_size = next_power_of_2(
          std::max(static_cast<u32>(samplers.size()), sampler_capacity));
      const auto new_storage_images_size = next_power_of_2(std::max(
          static_cast<u32>(storage_images.size()), storage_image_capacity));

      rebuild(new_sampled_images_size, new_samplers_size,
              new_storage_images_size);
    }

    write_all(sampled_images, samplers, storage_images);
    return needs_rebuild;
  }

private:
  auto rebuild(u32 sampled_cap, u32 sampler_cap, u32 storage_cap) -> void {
    if (set_layout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
      set_layout = VK_NULL_HANDLE;
    }
    desc_buf.reset();

    sampled_image_capacity = sampled_cap;
    sampler_capacity = sampler_cap;
    storage_image_capacity = storage_cap;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = sampled_cap,
         .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
         .descriptorCount = sampler_cap,
         .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = storage_cap,
         .stageFlags = VK_SHADER_STAGE_ALL},
    }};

    std::array<VkDescriptorBindingFlags, 3> binding_flags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<u32>(binding_flags.size()),
        .pBindingFlags = binding_flags.data(),
    };

    VkDescriptorSetLayoutCreateInfo layout_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_ci,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };

    check(vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &set_layout),
          "vkCreateDescriptorSetLayout");
    set_debug_name(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, set_layout,
                   std::format("{}.set_layout", debug_name));

    VkDeviceSize layout_size{};
    vkGetDescriptorSetLayoutSizeEXT(device, set_layout, &layout_size);
    vkGetDescriptorSetLayoutBindingOffsetEXT(device, set_layout, 0,
                                             &sampled_image_offset);
    vkGetDescriptorSetLayoutBindingOffsetEXT(device, set_layout, 1,
                                             &sampler_offset);
    vkGetDescriptorSetLayoutBindingOffsetEXT(device, set_layout, 2,
                                             &storage_image_offset);

    desc_buf = DescriptorBuffer::create(allocator, "descriptor_buffer");

    VkBufferCreateInfo buf_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = layout_size,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    check(vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &desc_buf->buffer,
                          &desc_buf->allocation, &desc_buf->allocation_info),
          "vmaCreateBuffer(descriptor)");

    auto const buf_name = std::format("{}.buffer", debug_name);
    set_vma_allocation_name(allocator, desc_buf->allocation, buf_name);
    set_debug_name(desc_buf->device(), VK_OBJECT_TYPE_BUFFER, desc_buf->buffer,
                   buf_name);

    VkBufferDeviceAddressInfo addr_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = desc_buf->buffer,
    };
    desc_buf->address = vkGetBufferDeviceAddress(device, &addr_info);
  }

  auto write_all(std::span<VkImageView const> sampled_images,
                 std::span<VkSampler const> samplers,
                 std::span<VkImageView const> storage_images) -> void {
    for (u32 i = 0; i < static_cast<u32>(sampled_images.size()); ++i) {
      VkDescriptorImageInfo img_info{
          .imageView = sampled_images[i],
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      };
      VkDescriptorGetInfoEXT get_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .data = {.pSampledImage = &img_info},
      };
      vkGetDescriptorEXT(
          device, &get_info, sampled_image_desc_size,
          static_cast<char *>(desc_buf->mapped_ptr()) + sampled_image_offset +
              static_cast<VkDeviceSize>(i) * sampled_image_desc_size);
    }

    for (u32 i = 0; i < static_cast<u32>(samplers.size()); ++i) {
      VkDescriptorGetInfoEXT get_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
          .type = VK_DESCRIPTOR_TYPE_SAMPLER,
          .data = {.pSampler = &samplers[i]},
      };
      vkGetDescriptorEXT(device, &get_info, sampler_desc_size,
                         static_cast<char *>(desc_buf->mapped_ptr()) +
                             sampler_offset +
                             static_cast<VkDeviceSize>(i) * sampler_desc_size);
    }

    for (u32 i = 0; i < static_cast<u32>(storage_images.size()); ++i) {
      VkDescriptorImageInfo img_info{
          .imageView = storage_images[i],
          .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
      VkDescriptorGetInfoEXT get_info{
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .data = {.pStorageImage = &img_info},
      };
      vkGetDescriptorEXT(
          device, &get_info, storage_image_desc_size,
          static_cast<char *>(desc_buf->mapped_ptr()) + storage_image_offset +
              static_cast<VkDeviceSize>(i) * storage_image_desc_size);
    }

    flush_allocation(allocator, desc_buf->allocation,
                     "vmaFlushAllocation(descriptor)");
  }

  VkDevice device{VK_NULL_HANDLE};
  VmaAllocator allocator{VK_NULL_HANDLE};
  std::string debug_name{"bindless_set"};

  VkDeviceSize sampled_image_desc_size{};
  VkDeviceSize sampler_desc_size{};
  VkDeviceSize storage_image_desc_size{};

  VkDescriptorSetLayout set_layout{VK_NULL_HANDLE};
  std::unique_ptr<DescriptorBuffer> desc_buf{};

  u32 sampled_image_capacity{};
  u32 sampler_capacity{};
  u32 storage_image_capacity{};

  VkDeviceSize sampled_image_offset{};
  VkDeviceSize sampler_offset{};
  VkDeviceSize storage_image_offset{};
};

struct FileDeleter {
  auto operator()(FILE *fp) const -> void { std::fclose(fp); }
};

using unique_file = std::unique_ptr<FILE, FileDeleter>;

auto write_png(std::string_view path, std::span<std::uint8_t const> rgba,
               u32 width, u32 height, usize row_pitch) -> void {
  unique_file fp(std::fopen(path.data(), "wb"));
  if (!fp)
    fatal("failed to open output file");

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png)
    fatal("png_create_write_struct failed");

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    fatal("png_create_info_struct failed");
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fatal("libpng error during write");
  }

  png_init_io(png, fp.get());
  png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  for (u32 y = 0; y < height; ++y) {
    auto row_ptr = const_cast<png_bytep>(rgba.data() + y * row_pitch);
    png_write_row(png, row_ptr);
  }

  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
}

struct PushConstants {
  u32 width;
  u32 height;
  u32 input_image_index;
  u32 sampler_index;
  u32 output_image_index;
};

struct ComputeDispatchContext {
  VkDevice device{};
  VkQueue queue{};
  VkCommandPool command_pool{};
  VkCommandBuffer cmd{};
  VkSemaphore timeline{};
  u64 &timeline_value;

  VkPipeline pipeline{};
  VkPipelineLayout pipeline_layout{};
  BindlessSet const *bindless_set{};

  Texture const *input_tex{};
  Texture const *output_tex{};
  HostBuffer const *upload_buffer{};
  HostBuffer const *readback_buffer{};

  VkQueryPool timing_query_pool{};
  PushConstants pc{};
};

auto record_and_submit(ComputeDispatchContext const &ctx) -> u64 {
  check(vkResetCommandPool(ctx.device, ctx.command_pool, 0),
        "vkResetCommandPool");
  vkResetQueryPool(ctx.device, ctx.timing_query_pool, 0, 2);

  VkCommandBufferBeginInfo begin{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  check(vkBeginCommandBuffer(ctx.cmd, &begin), "vkBeginCommandBuffer");

  VkImageMemoryBarrier2 input_to_transfer_dst{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = ctx.input_tex->image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

VkDependencyInfo init_dep{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &input_to_transfer_dst,
};

  vkCmdPipelineBarrier2(ctx.cmd, &init_dep);

  VkBufferImageCopy upload_region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .imageOffset = {0, 0, 0},
      .imageExtent = {image_width, image_height, 1},
  };

  vkCmdCopyBufferToImage(
      ctx.cmd, ctx.upload_buffer->buffer, ctx.input_tex->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload_region);

  VkImageMemoryBarrier2 input_to_sampled{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = ctx.input_tex->image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

  VkDependencyInfo input_ready_dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &input_to_sampled,
  };

  vkCmdPipelineBarrier2(ctx.cmd, &input_ready_dep);

  vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipeline);

  VkDescriptorBufferBindingInfoEXT desc_binding_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
      .address = ctx.bindless_set->buffer_address(),
      .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
               VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT,
  };

  vkCmdBindDescriptorBuffersEXT(ctx.cmd, 1, &desc_binding_info);

  u32 buffer_index = 0;
  VkDeviceSize buffer_offset = 0;
  vkCmdSetDescriptorBufferOffsetsEXT(ctx.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                     ctx.pipeline_layout, 0, 1, &buffer_index,
                                     &buffer_offset);

  vkCmdPushConstants(ctx.cmd, ctx.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(ctx.pc), &ctx.pc);

  vkCmdWriteTimestamp2(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       ctx.timing_query_pool, 0);
  vkCmdDispatch(ctx.cmd, (image_width + 7) / 8, (image_height + 7) / 8, 1);
  vkCmdWriteTimestamp2(ctx.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       ctx.timing_query_pool, 1);

  VkImageMemoryBarrier2 output_to_transfer{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = ctx.output_tex->image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

  VkDependencyInfo output_ready_dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &output_to_transfer,
  };

  vkCmdPipelineBarrier2(ctx.cmd, &output_ready_dep);

  VkBufferImageCopy readback_region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .imageOffset = {0, 0, 0},
      .imageExtent = {image_width, image_height, 1},
  };

  vkCmdCopyImageToBuffer(ctx.cmd, ctx.output_tex->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         ctx.readback_buffer->buffer, 1, &readback_region);

  VkImageMemoryBarrier2 output_back_to_general{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
      .dstAccessMask = VK_ACCESS_2_NONE,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = ctx.output_tex->image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };

  VkBufferMemoryBarrier2 readback_ready{
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
      .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
      .buffer = ctx.readback_buffer->buffer,
      .offset = 0,
      .size = VK_WHOLE_SIZE,
  };

  std::array<VkBufferMemoryBarrier2, 1> readback_buffer_barriers{
      readback_ready,
  };
  std::array<VkImageMemoryBarrier2, 1> final_image_barriers{
      output_back_to_general,
  };

  VkDependencyInfo readback_dep{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount =
          static_cast<u32>(readback_buffer_barriers.size()),
      .pBufferMemoryBarriers = readback_buffer_barriers.data(),
      .imageMemoryBarrierCount = static_cast<u32>(final_image_barriers.size()),
      .pImageMemoryBarriers = final_image_barriers.data(),
  };

  vkCmdPipelineBarrier2(ctx.cmd, &readback_dep);

  check(vkEndCommandBuffer(ctx.cmd), "vkEndCommandBuffer");

  VkCommandBufferSubmitInfo cmd_submit_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = ctx.cmd,
  };

  ++ctx.timeline_value;
  VkSemaphoreSubmitInfo signal_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = ctx.timeline,
      .value = ctx.timeline_value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  };

  VkSubmitInfo2 submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmd_submit_info,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal_info,
  };

  check(vkQueueSubmit2(ctx.queue, 1, &submit, VK_NULL_HANDLE),
        "vkQueueSubmit2");

  VkSemaphoreWaitInfo wait_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &ctx.timeline,
      .pValues = &ctx.timeline_value,
  };

  check(vkWaitSemaphores(ctx.device, &wait_info, UINT64_MAX),
        "vkWaitSemaphores");

  std::array<u64, 2> timestamps{};
  check(vkGetQueryPoolResults(ctx.device, ctx.timing_query_pool, 0, 2,
                              sizeof(timestamps), timestamps.data(),
                              sizeof(u64), VK_QUERY_RESULT_64_BIT),
        "vkGetQueryPoolResults");

  return timestamps[1] - timestamps[0];
}

#include "./comp_spv.inc"
} // namespace

#if defined(_DEBUG) || !defined(NDEBUG)
constexpr bool is_debug = true;
#else
constexpr bool is_debug = false;
#endif

inline VKAPI_ATTR VkBool32 VKAPI_CALL default_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
  auto ms = vkb::to_string_message_severity(messageSeverity);
  auto mt = vkb::to_string_message_type(messageType);
  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    printf("[%s: %s] - %s\n%s\n", ms, mt, pCallbackData->pMessageIdName,
           pCallbackData->pMessage);
  } else {
    printf("[%s: %s]\n%s\n", ms, mt, pCallbackData->pMessage);
  }

  return VK_FALSE;
}

auto main() -> int {
  check(volkInitialize(), "volkInitialize");

  vkb::InstanceBuilder instance_builder{vkGetInstanceProcAddr};
  auto instance_ret = instance_builder.set_app_name("compute_to_file")
                          .set_headless()
                          .enable_validation_layers(true)
                          .set_debug_callback(default_debug_callback)
                          .require_api_version(1, 3, 0)
                          .build();

  if (!instance_ret)
    fatal(instance_ret.error().message());

  vkb::Instance vkb_instance = instance_ret.value();
  VkInstance instance = vkb_instance.instance;

  volkLoadInstance(instance);

  vkb::PhysicalDeviceSelector selector{vkb_instance};
  auto physical_device_ret =
      selector.add_required_extension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME)
          .add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
          .set_minimum_version(1, 3)
          .select();

  if (!physical_device_ret)
    fatal(physical_device_ret.error().message());

  vkb::PhysicalDevice vkb_physical_device = physical_device_ret.value();
  VkPhysicalDevice physical_device = vkb_physical_device.physical_device;

  VkPhysicalDeviceDescriptorBufferPropertiesEXT desc_buf_props{
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
  };
  VkPhysicalDeviceProperties2 phys_props2{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &desc_buf_props,
  };
  vkGetPhysicalDeviceProperties2(physical_device, &phys_props2);
  auto const timestamp_period_ns =
      phys_props2.properties.limits.timestampPeriod;

  VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
      .pNext = nullptr,
      .descriptorBuffer = VK_TRUE,
  };

  VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR,
      .pNext = &descriptor_buffer_features,
      .maintenance5 = VK_TRUE,
  };

  VkPhysicalDeviceVulkan13Features vulkan_13_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &maintenance5_features,
      .synchronization2 = VK_TRUE,
  };

  VkPhysicalDeviceVulkan12Features vulkan_12_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &vulkan_13_features,
  };
  vulkan_12_features.timelineSemaphore = VK_TRUE;
  vulkan_12_features.hostQueryReset = VK_TRUE;
  vulkan_12_features.bufferDeviceAddress = VK_TRUE;
  vulkan_12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  vulkan_12_features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
  vulkan_12_features.runtimeDescriptorArray = VK_TRUE;
  vulkan_12_features.descriptorBindingPartiallyBound = VK_TRUE;

  vkb::DeviceBuilder device_builder{vkb_physical_device};
  auto device_ret = device_builder.add_pNext(&vulkan_12_features).build();
  if (!device_ret)
    fatal(device_ret.error().message());

  vkb::Device vkb_device = device_ret.value();
  VkDevice device = vkb_device.device;

  volkLoadDevice(device);

  auto queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
  auto family_ret = vkb_device.get_queue_index(vkb::QueueType::graphics);

  if (!queue_ret || !family_ret)
    fatal("failed to get queue");

  VkQueue queue = queue_ret.value();
  u32 queue_family_index = family_ret.value();

  set_debug_name(device, VK_OBJECT_TYPE_INSTANCE, instance,
                 "compute_to_file.instance");
  set_debug_name(device, VK_OBJECT_TYPE_PHYSICAL_DEVICE, physical_device,
                 "compute_to_file.physical_device");
  set_debug_name(device, VK_OBJECT_TYPE_DEVICE, device,
                 "compute_to_file.device");
  set_debug_name(device, VK_OBJECT_TYPE_QUEUE, queue,
                 "compute_to_file.graphics_queue");

  VkQueryPoolCreateInfo query_pool_info{
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = 2,
  };

  VkQueryPool timing_query_pool{};
  check(
      vkCreateQueryPool(device, &query_pool_info, nullptr, &timing_query_pool),
      "vkCreateQueryPool");
  set_debug_name(device, VK_OBJECT_TYPE_QUERY_POOL, timing_query_pool,
                 "compute_to_file.timing_query_pool");

  VmaVulkanFunctions vma_vulkan_funcs{};
  vma_vulkan_funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vma_vulkan_funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocator_ci{
      .flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
               VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT |
               VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = physical_device,
      .device = device,
      .pVulkanFunctions = &vma_vulkan_funcs,
      .instance = instance,
      .vulkanApiVersion = VK_API_VERSION_1_3,
  };

  VmaAllocator allocator{};
  check(vmaCreateAllocator(&allocator_ci, &allocator), "vmaCreateAllocator");

  struct ImageInfo {
    u32 width;
    u32 height;
    u32 mip_levels;
    u32 array_layers;
    VkFormat format;
  };

  auto create_texture =
      [&](VmaAllocator &alloc, ImageInfo info, VkImageUsageFlags usage,
          std::string_view debug_name) -> std::unique_ptr<Texture> {
    auto tex = Texture::create(alloc, debug_name);
    tex->metadata = {
        .width = info.width,
        .height = info.height,
        .mip_levels = info.mip_levels,
        .array_layers = info.array_layers,
        .format = info.format,
    };

    VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = info.format,
        .extent = {info.width, info.height, 1},
        .mipLevels = info.mip_levels,
        .arrayLayers = info.array_layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_ci{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    check(vmaCreateImage(allocator, &image_ci, &alloc_ci, &tex->image,
                         &tex->allocation, &tex->allocation_info),
          "vmaCreateImage");
    set_vma_allocation_name(allocator, tex->allocation, debug_name);
    return tex;
  };

  auto create_image_view = [&](VkImage image, VkFormat format,
                               std::string_view debug_name) -> VkImageView {
    VkImageViewCreateInfo image_view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkImageView view{};
    check(vkCreateImageView(device, &image_view_info, nullptr, &view),
          "vkCreateImageView");

    set_debug_name(device, VK_OBJECT_TYPE_IMAGE_VIEW, view, debug_name);
    return view;
  };

  ImageInfo input_info{
      .width = image_width,
      .height = image_height,
      .mip_levels = 1,
      .array_layers = 1,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
  };

  auto input_tex = create_texture(allocator, input_info,
                                  VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  "input_texture");

  {
    std::vector<std::uint8_t> data(image_width * image_height * 4);
    for (u32 y = 0; y < image_height; ++y) {
      for (u32 x = 0; x < image_width; ++x) {
        auto idx =
            static_cast<usize>(y) * image_width * 4 + static_cast<usize>(x) * 4;
        data[idx + 0] = static_cast<std::uint8_t>((255u * x) / image_width);
        data[idx + 1] = static_cast<std::uint8_t>((255u * y) / image_height);
        data[idx + 2] = 96;
        data[idx + 3] = 255;
      }
    }
    auto host_buffer = HostBuffer::create(
        allocator, image_width * image_height * 4, "input_upload_host_buffer");

    OneTimeCommand input_upload{
        .device = device,
        .queue = queue,
        .queue_family_index = queue_family_index,
        .debug_name = "input_upload",
    };

    input_upload.submit([&](VkCommandBuffer cmd) {
      input_tex->upload(cmd, allocator, data, *host_buffer, image_width * 4);
    });
  }

  ImageInfo output_info{
      .width = image_width,
      .height = image_height,
      .mip_levels = 1,
      .array_layers = 1,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
  };

  auto output_tex = create_texture(allocator, output_info,
                                   VK_IMAGE_USAGE_STORAGE_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   "output_texture");

  {
    OneTimeCommand output_init{
        .device = device,
        .queue = queue,
        .queue_family_index = queue_family_index,
        .debug_name = "output_texture_init",
    };

    output_init.submit([&](VkCommandBuffer cmd) {
      VkImageMemoryBarrier2 barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
          .srcAccessMask = VK_ACCESS_2_NONE,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = output_tex->image,
          .subresourceRange =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      };

      VkDependencyInfo dep{
          .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
          .imageMemoryBarrierCount = 1,
          .pImageMemoryBarriers = &barrier,
      };

      vkCmdPipelineBarrier2(cmd, &dep);
    });
  }

  VkImageView input_view = create_image_view(
      input_tex->image, input_tex->metadata.format, "input_view");
  VkImageView output_view = create_image_view(
      output_tex->image, output_tex->metadata.format, "output_view");

  VkSamplerCreateInfo sampler_ci{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
  };

  VkSampler sampler{};
  check(vkCreateSampler(device, &sampler_ci, nullptr, &sampler),
        "vkCreateSampler");
  set_debug_name(device, VK_OBJECT_TYPE_SAMPLER, sampler,
                 "compute_to_file.linear_clamp_sampler");

  VkSamplerCreateInfo linear_repeat_sampler_ci{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
  };

  VkSampler linear_repeat_sampler{};
  check(vkCreateSampler(device, &linear_repeat_sampler_ci, nullptr,
                        &linear_repeat_sampler),
        "vkCreateSampler");
  set_debug_name(device, VK_OBJECT_TYPE_SAMPLER, linear_repeat_sampler,
                 "compute_to_file.linear_repeat_sampler");

  auto upload_buffer = HostBuffer::create(
      allocator, image_width * image_height * 4, "upload_buffer");
  auto readback_buffer = HostBuffer::create(
      allocator, image_width * image_height * 4, "readback_buffer");

  {
    auto pixels = std::span<std::uint8_t>(
        static_cast<std::uint8_t *>(upload_buffer->mapped_data()),
        image_width * image_height * 4);
    for (u32 y = 0; y < image_height; ++y) {
      for (u32 x = 0; x < image_width; ++x) {
        auto idx =
            static_cast<usize>(y) * image_width * 4 + static_cast<usize>(x) * 4;
        pixels[idx + 0] = static_cast<std::uint8_t>((255u * x) / image_width);
        pixels[idx + 1] = static_cast<std::uint8_t>((255u * y) / image_height);
        pixels[idx + 2] = 96;
        pixels[idx + 3] = 255;
      }
    }

    flush_allocation(allocator, upload_buffer->allocation,
                     "vmaFlushAllocation(upload_buffer)");
  }

  ImageInfo white_info{
      .width = 1,
      .height = 1,
      .mip_levels = 1,
      .array_layers = 1,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
  };

  auto white_texture = create_texture(allocator, white_info,
                                      VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                      "white_texture");
  {
    std::array<u8, 4> white_data{
        255,
        255,
        255,
        255,
    };

    auto white_upload_buffer =
        HostBuffer::create(allocator, white_data.size(), "white_upload_buffer");
    std::memcpy(white_upload_buffer->mapped_data(), white_data.data(),
                white_data.size());
    flush_allocation(allocator, white_upload_buffer->allocation,
                     "vmaFlushAllocation(white_upload_buffer)");

    OneTimeCommand white_upload{
        .device = device,
        .queue = queue,
        .queue_family_index = queue_family_index,
        .debug_name = "white_texture_upload",
    };

    white_upload.submit([&](VkCommandBuffer cmd) {
      VkImageMemoryBarrier2 barrier{
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
          .srcAccessMask = VK_ACCESS_2_NONE,
          .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
          .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image = white_texture->image,
          .subresourceRange =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
      };

      VkDependencyInfo dep{
          .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
          .imageMemoryBarrierCount = 1,
          .pImageMemoryBarriers = &barrier,
      };

      vkCmdPipelineBarrier2(cmd, &dep);

      VkBufferImageCopy region{
          .bufferOffset = 0,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource =
              {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
          .imageOffset = {0, 0, 0},
          .imageExtent = {1, 1, 1},
      };

      vkCmdCopyBufferToImage(cmd, white_upload_buffer->buffer,
                             white_texture->image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
      barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
      barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      vkCmdPipelineBarrier2(cmd, &dep);
    });
  }

  VkImageView white_view = create_image_view(
      white_texture->image, white_texture->metadata.format, "white_view");

  std::vector<VkImageView> input_image_views{};
  input_image_views.resize(max_input_images);
  std::ranges::for_each(input_image_views,
                        [wv = white_view](auto &view) { view = wv; });
  input_image_views[0] = white_view;
  input_image_views[1] = input_view;

  std::vector<VkSampler> sampler_views{};
  sampler_views.resize(max_samplers);
  std::ranges::for_each(sampler_views,
                        [s = linear_repeat_sampler](auto &smpl) { smpl = s; });
  sampler_views[0] = linear_repeat_sampler;

  std::vector<VkImageView> output_image_views{};
  output_image_views.resize(max_output_images);
  std::ranges::for_each(output_image_views,
                        [ov = output_view](auto &view) { view = ov; });
  output_image_views[0] = output_view;

  auto bindless_set =
      BindlessSet::create(device, allocator, desc_buf_props,
                          std::span<VkImageView const>{input_image_views},
                          std::span<VkSampler const>{sampler_views},
                          std::span<VkImageView const>{output_image_views},
                          "compute_to_file.bindless_set");

  VkPushConstantRange push_range{
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(PushConstants),
  };

  VkDescriptorSetLayout set_layout = bindless_set->layout_handle();

  VkPipelineLayoutCreateInfo layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_range,
  };

  VkPipelineLayout pipeline_layout{};
  check(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout),
        "vkCreatePipelineLayout");
  set_debug_name(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline_layout,
                 "compute_to_file.pipeline_layout");

  VkShaderModuleCreateInfo inline_shader{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = comp_spv_len,
      .pCode = reinterpret_cast<u32 const *>(comp_spv),
  };

  VkPipelineShaderStageCreateInfo stage{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &inline_shader,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = VK_NULL_HANDLE,
      .pName = "main",
  };

  VkComputePipelineCreateInfo pipeline_info{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
      .stage = stage,
      .layout = pipeline_layout,
  };

  VkPipeline pipeline{};
  check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                 nullptr, &pipeline),
        "vkCreateComputePipelines");
  set_debug_name(device, VK_OBJECT_TYPE_PIPELINE, pipeline,
                 "compute_to_file.compute_pipeline");

  VkCommandPoolCreateInfo pool_ci{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queue_family_index,
  };

  VkCommandPool command_pool{};
  check(vkCreateCommandPool(device, &pool_ci, nullptr, &command_pool),
        "vkCreateCommandPool");
  set_debug_name(device, VK_OBJECT_TYPE_COMMAND_POOL, command_pool,
                 "compute_to_file.command_pool");

  VkCommandBufferAllocateInfo cmd_alloc{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };

  VkCommandBuffer cmd{};
  check(vkAllocateCommandBuffers(device, &cmd_alloc, &cmd),
        "vkAllocateCommandBuffers");
  set_debug_name(device, VK_OBJECT_TYPE_COMMAND_BUFFER, cmd,
                 "compute_to_file.command_buffer");

  VkSemaphoreTypeCreateInfo semaphore_type_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0,
  };

  VkSemaphoreCreateInfo semaphore_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
  };

  VkSemaphore timeline{};
  check(vkCreateSemaphore(device, &semaphore_info, nullptr, &timeline),
        "vkCreateSemaphore");
  set_debug_name(device, VK_OBJECT_TYPE_SEMAPHORE, timeline,
                 "compute_to_file.timeline_semaphore");

  u64 timeline_value = 0;

  ComputeDispatchContext dispatch_ctx{
      .device = device,
      .queue = queue,
      .command_pool = command_pool,
      .cmd = cmd,
      .timeline = timeline,
      .timeline_value = timeline_value,
      .pipeline = pipeline,
      .pipeline_layout = pipeline_layout,
      .bindless_set = bindless_set.get(),
      .input_tex = input_tex.get(),
      .output_tex = output_tex.get(),
      .upload_buffer = upload_buffer.get(),
      .readback_buffer = readback_buffer.get(),
      .timing_query_pool = timing_query_pool,
      .pc =
          {
              .width = image_width,
              .height = image_height,
              .input_image_index = 1,
              .sampler_index = 0,
              .output_image_index = 0,
          },
  };

  constexpr u32 warmup_iterations{4};
  constexpr u32 timed_iterations{32};

  for (u32 i = 0; i < warmup_iterations; ++i)
    record_and_submit(dispatch_ctx);

  std::vector<u64> ticks(timed_iterations);
  for (u32 i = 0; i < timed_iterations; ++i)
    ticks[i] = record_and_submit(dispatch_ctx);

  auto ticks_to_us = [&](u64 t) -> double {
    return static_cast<double>(t) * static_cast<double>(timestamp_period_ns) /
           1'000.0;
  };

  auto const min_us = ticks_to_us(*std::ranges::min_element(ticks));
  auto const max_us = ticks_to_us(*std::ranges::max_element(ticks));

  double const sum_us =
      std::accumulate(ticks.begin(), ticks.end(), 0.0,
                      [&](double acc, u64 t) { return acc + ticks_to_us(t); });
  double const mean_us = sum_us / static_cast<double>(timed_iterations);

  double const variance = std::accumulate(ticks.begin(), ticks.end(), 0.0,
                                          [&](double acc, u64 t) {
                                            double const d =
                                                ticks_to_us(t) - mean_us;
                                            return acc + d * d;
                                          }) /
                          static_cast<double>(timed_iterations);
  double const stddev_us = std::sqrt(variance);

  std::vector<u64> sorted_ticks = ticks;
  std::ranges::sort(sorted_ticks);
  double const median_us =
      (timed_iterations % 2 == 0)
          ? ticks_to_us((sorted_ticks[timed_iterations / 2 - 1] +
                         sorted_ticks[timed_iterations / 2]) /
                        2)
          : ticks_to_us(sorted_ticks[timed_iterations / 2]);

  std::cout << std::format(
      "GPU dispatch stats over {} iterations ({} warmup discarded)\n"
      "  min    {:.3f} us\n"
      "  max    {:.3f} us\n"
      "  mean   {:.3f} us\n"
      "  median {:.3f} us\n"
      "  stddev {:.3f} us\n",
      timed_iterations, warmup_iterations, min_us, max_us, mean_us, median_us,
      stddev_us);

  invalidate_allocation(allocator, readback_buffer->allocation,
                        "vmaInvalidateAllocation(readback_buffer)");

  auto *pixels =
      static_cast<std::uint8_t const *>(readback_buffer->mapped_data());
  write_png(
      "out.png",
      std::span{pixels, static_cast<usize>(image_width) * image_height * 4},
      image_width, image_height, image_width * 4);

  vkDestroySemaphore(device, timeline, nullptr);
  vkDestroyCommandPool(device, command_pool, nullptr);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
  vkDestroySampler(device, sampler, nullptr);
  vkDestroySampler(device, linear_repeat_sampler, nullptr);
  vkDestroyImageView(device, white_view, nullptr);
  vkDestroyImageView(device, output_view, nullptr);
  vkDestroyImageView(device, input_view, nullptr);
  vkDestroyQueryPool(device, timing_query_pool, nullptr);

  white_texture.reset();
  output_tex.reset();
  input_tex.reset();
  readback_buffer.reset();
  upload_buffer.reset();
  bindless_set.reset();

  vmaDestroyAllocator(allocator);
  vkb::destroy_device(vkb_device);
  vkb::destroy_instance(vkb_instance);

  return 0;
}