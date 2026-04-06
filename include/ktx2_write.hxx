#pragma once

#include "compression.hxx"
#include "format.hxx"
#include "image.hxx"

#include <filesystem>
#include <vector>

void write_ktx2(
    std::filesystem::path const& path,
    PixelFormat format,
    std::vector<Image> const& mip_chain
);

struct Ktx2Info
{
    u32 width {0};
    u32 height {0};
    u32 levels {0};
    u32 vk_format {0};
};

[[nodiscard]] auto write_ktx2(
    std::filesystem::path const& path,
    PixelFormat format,
    std::vector<Image> const& mip_chain,
    CompressionConfig const& compression
) -> bool;

[[nodiscard]] auto read_ktx2_info(std::filesystem::path const& path) -> Ktx2Info;