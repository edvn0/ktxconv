#pragma once

#include "types.hxx"

#include <cassert>
#include <filesystem>
#include <vector>

enum class PixelFormat
{
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB
};

struct Image final
{
    u32 width {0};
    u32 height {0};
    std::vector<u8> pixels {};

    [[nodiscard]] auto empty() const -> bool
    {
        return width == 0 || height == 0 || pixels.empty();
    }

    [[nodiscard]] auto pixel_offset(u32 x, u32 y) const -> usize
    {
        assert(x < width && y < height && "pixel_offset: x or y out of bounds");
        return static_cast<usize>((y * width + x) * 4);
    }

    auto operator[](std::integral auto index) const -> decltype(auto)
    {
        return pixels[index];
    }

    auto operator[](std::integral auto x, std::integral auto y) const -> decltype(auto)
    {
        return pixels[pixel_offset(x, y)];
    }
};

[[nodiscard]] auto load_image_rgba8(std::filesystem::path const& path) -> Image;
void save_png_rgba8(std::filesystem::path const& path, Image const& image);