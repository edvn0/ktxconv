#include "ktx2_decode.hxx"

#include <ktx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    
    [[nodiscard]] auto unpack_snorm(u8 v) -> float
    {
        return (static_cast<float>(v) / 255.0f) * 2.0f - 1.0f;
    }

    [[nodiscard]] auto pack_snorm(float v) -> u8
    {
        auto const clamped = std::clamp(v, -1.0f, 1.0f);
        auto const encoded = (clamped * 0.5f + 0.5f) * 255.0f;
        return static_cast<u8>(std::lround(encoded));
    }
}

auto decode_ktx2_to_rgba8(std::filesystem::path const& path) -> DecodedKtxImage
{
    ktxTexture2* raw_texture = nullptr;

    auto rc = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &raw_texture
    );

    if (rc != KTX_SUCCESS)
    {
        throw std::runtime_error("failed to open ktx2 file");
    }

    std::unique_ptr<ktxTexture2, Deleter> texture {
        raw_texture
    };

    if (ktxTexture2_NeedsTranscoding(raw_texture))
    {
        rc = ktxTexture2_TranscodeBasis(raw_texture, KTX_TTF_RGBA32, 0);
        if (rc != KTX_SUCCESS)
        {
            throw std::runtime_error("failed to transcode basis texture");
        }
    }

    ktx_size_t offset = 0;
    rc = ktxTexture2_GetImageOffset(texture.get(), 0, 0, 0, &offset);
    if (rc != KTX_SUCCESS)
    {
        throw std::runtime_error("failed to query image offset");
    }

    DecodedKtxImage decoded {};
    decoded.image.width = raw_texture->baseWidth;
    decoded.image.height = raw_texture->baseHeight;
    decoded.image.pixels.resize(
        static_cast<usize>(decoded.image.width) *
        decoded.image.height * 4
    );

    auto* data = ktxTexture_GetData(std::bit_cast<ktxTexture*>(texture.get()));
    std::memcpy(
        decoded.image.pixels.data(),
        data + offset,
        decoded.image.pixels.size()
    );

    usize similar_rgb_count {0};
    usize pixel_count {0};

    for (usize i = 0; i < decoded.image.pixels.size(); i += 4)
    {
        auto const r = decoded.image.pixels[i + 0];
        auto const g = decoded.image.pixels[i + 1];
        auto const b = decoded.image.pixels[i + 2];

        if (std::abs(static_cast<i32>(r) - static_cast<i32>(g)) <= 1 &&
            std::abs(static_cast<i32>(r) - static_cast<i32>(b)) <= 1)
        {
            ++similar_rgb_count;
        }

        ++pixel_count;
    }

    if (pixel_count > 0 &&
        static_cast<f64>(similar_rgb_count) / static_cast<f64>(pixel_count) > 0.95)
    {
        decoded.semantic = DecodedKtxSemantic::PackedNormalXY;
    }

    return decoded;
}

auto reconstruct_normal_map_from_packed_xy(Image const& packed_xy) -> Image
{
    Image reconstructed {};
    reconstructed.width = packed_xy.width;
    reconstructed.height = packed_xy.height;
    reconstructed.pixels.resize(packed_xy.pixels.size());

    for (usize i = 0; i < packed_xy.pixels.size(); i += 4)
    {
        auto const x = unpack_snorm(packed_xy.pixels[i + 0]);
        auto const y = unpack_snorm(packed_xy.pixels[i + 3]);

        auto const zz = std::max(0.0f, 1.0f - x * x - y * y);
        auto const z = std::sqrt(zz);

        reconstructed.pixels[i + 0] = pack_snorm(x);
        reconstructed.pixels[i + 1] = pack_snorm(y);
        reconstructed.pixels[i + 2] = pack_snorm(z);
        reconstructed.pixels[i + 3] = 255;
    }

    return reconstructed;
}