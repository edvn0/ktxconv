#pragma once

#include "image.hxx"

#include <vector>

[[nodiscard]] auto mip_level_count(u32 width, u32 height) -> u32;
[[nodiscard]] auto generate_mip_chain(Image const& base, PixelFormat format) -> std::vector<Image>;