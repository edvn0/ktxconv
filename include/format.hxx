#pragma once

#include "image.hxx"

#include <string_view>
#include <vulkan/vulkan_core.h>

[[nodiscard]] auto parse_format(std::string_view text) -> PixelFormat;
[[nodiscard]] auto to_vk_format(PixelFormat format) -> VkFormat;
[[nodiscard]] auto is_srgb(PixelFormat format) -> bool;