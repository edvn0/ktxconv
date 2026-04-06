#include "compression.hxx"

#include <cmath>
#include <filesystem>

auto check_if_is_normal_map(const std::filesystem::path& filename_with_extension, Image const& image) -> bool
{
    if (image.empty())
    {
        return false;
    }

    auto const has_normal_suffix = [](std::string_view name) {
        return name.ends_with("_Normal") || name.ends_with("_DDN") || name.ends_with("_ddn") || name.ends_with("_normal") || name.ends_with("_NormalMap") || name.ends_with("_normalmap") || name.ends_with("_n");
    };

    const auto name_without_extension = filename_with_extension.stem().string();
    if (has_normal_suffix(  name_without_extension))
    {
        return true;
    }

    f64 avg_r = 0.0;
    f64 avg_g = 0.0;
    f64 avg_b = 0.0;
    usize count = 0;

    for (auto&&  pixels : image.four_channel_pixels())
    {
        avg_r += static_cast<f32>(pixels[0]);
        avg_g += static_cast<f32>(pixels[1]);
        avg_b += static_cast<f32>(pixels[2]);
        ++count;
    }

    avg_r /= static_cast<f64>(count);
    avg_g /= static_cast<f64>(count);
    avg_b /= static_cast<f64>(count);

    auto const near_128 = [](f64 x) { return std::abs(x - 128.0) < 35.0; };
    auto const high_blue = avg_b > 180.0;

    return near_128(avg_r) && near_128(avg_g) && high_blue;
}