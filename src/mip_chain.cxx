#include "mip_chain.hxx"

#include <stb_image_resize2.h>

#include <algorithm>
#include <stdexcept>

namespace
{
    [[nodiscard]] auto to_stbir_layout(PixelFormat format) -> stbir_pixel_layout
    {
        switch (format)
        {
            case PixelFormat::R8G8B8A8_UNORM:
                return STBIR_RGBA;
            case PixelFormat::R8G8B8A8_SRGB:
                return STBIR_RGBA;
        }

        throw std::runtime_error("unsupported pixel format");
    }

    [[nodiscard]] auto downsample(Image const& src, PixelFormat format) -> Image
    {
        Image dst {};
        dst.width = std::max(1u, src.width / 2);
        dst.height = std::max(1u, src.height / 2);
        dst.pixels.resize(static_cast<usize>(dst.width) * dst.height * 4);

        auto const src_width = static_cast<int>(src.width);
        auto const src_height = static_cast<int>(src.height);
        auto const dst_width = static_cast<int>(dst.width);
        auto const dst_height = static_cast<int>(dst.height);

        auto const src_stride = static_cast<int>(src.width * 4);
        auto const dst_stride = static_cast<int>(dst.width * 4);
        auto const layout = to_stbir_layout(format);

        auto const ok = [&]() -> bool
        {
            switch (format)
            {
                case PixelFormat::R8G8B8A8_UNORM:
                    return stbir_resize_uint8_linear(
                        src.pixels.data(),
                        src_width,
                        src_height,
                        src_stride,
                        dst.pixels.data(),
                        dst_width,
                        dst_height,
                        dst_stride,
                        layout
                    );

                case PixelFormat::R8G8B8A8_SRGB:
                    return stbir_resize_uint8_srgb(
                        src.pixels.data(),
                        src_width,
                        src_height,
                        src_stride,
                        dst.pixels.data(),
                        dst_width,
                        dst_height,
                        dst_stride,
                        layout
                    );
            }

            throw std::runtime_error("unsupported pixel format");
        }();

        if (!ok)
        {
            throw std::runtime_error("stbir resize failed");
        }

        return dst;
    }
}

auto mip_level_count(u32 width, u32 height) -> u32
{
    if (width == 0 || height == 0)
    {
        throw std::runtime_error("invalid image dimensions");
    }

    u32 levels {1};

    while (width > 1 || height > 1)
    {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++levels;
    }

    return levels;
}

auto generate_mip_chain(Image const& base, PixelFormat format) -> std::vector<Image>
{
    if (base.empty())
    {
        throw std::runtime_error("cannot generate mip chain from empty image");
    }

    std::vector<Image> mip_chain {};
    mip_chain.reserve(mip_level_count(base.width, base.height));
    mip_chain.push_back(base);

    while (mip_chain.back().width > 1 || mip_chain.back().height > 1)
    {
        mip_chain.push_back(downsample(mip_chain.back(), format));
    }

    return mip_chain;
}