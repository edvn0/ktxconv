#pragma once

#include "image.hxx"
#include "types.hxx"

#include <filesystem>
#include <concepts>

enum class DecodedKtxSemantic : u8
{
    Color,
    PackedNormalXY
};

struct DecodedKtxImage
{
    Image image {};
    DecodedKtxSemantic semantic {DecodedKtxSemantic::Color};

    auto operator[](std::integral auto index) const -> decltype(auto)
    {
        return image[index];
    }

    auto operator[](std::integral auto x, std::integral auto y) const -> decltype(auto)
    {
        return image[x, y];
    }
};

[[nodiscard]] auto decode_ktx2_to_rgba8(std::filesystem::path const& path) -> DecodedKtxImage;
[[nodiscard]] auto reconstruct_normal_map_from_packed_xy(Image const& packed_xy) -> Image;