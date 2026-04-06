#include "format.hxx"

#include <stdexcept>
#include <string>

auto parse_format(std::string_view text) -> PixelFormat
{
    if (text == "r8g8b8a8_unorm")
    {
        return PixelFormat::R8G8B8A8_UNORM;
    }

    if (text == "r8g8b8a8_srgb")
    {
        return PixelFormat::R8G8B8A8_SRGB;
    }

    throw std::runtime_error("unsupported format: " + std::string(text));
}

auto to_vk_format(PixelFormat format) -> VkFormat
{
    switch (format)
    {
        case PixelFormat::R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
    }

    throw std::runtime_error("unreachable");
}

auto is_srgb(PixelFormat format) -> bool
{
    return format == PixelFormat::R8G8B8A8_SRGB;
}