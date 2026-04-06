#include "compression.hxx"

#include <cmath>
#include <filesystem>

auto check_if_is_normal_map(const std::filesystem::path& filename_with_extension, Image const& image) -> bool
{
    if (image.empty())
    {
        return false;
    }

    // Check for _Normal, _DDN, _ddn, _normal, _NormalMap, _normalmap, _n suffixes in the filename
    auto const has_normal_suffix = [](std::string_view name) {
        return name.ends_with("_Normal") || name.ends_with("_DDN") || name.ends_with("_ddn") || name.ends_with("_normal") || name.ends_with("_NormalMap") || name.ends_with("_normalmap") || name.ends_with("_n");
    };

    const auto name_without_extension = filename_with_extension.stem().string();
    if (has_normal_suffix(  name_without_extension))
    {
        return true;
    }

    double avg_r = 0.0;
    double avg_g = 0.0;
    double avg_b = 0.0;
    usize count = 0;

    for (usize i = 0; i < image.pixels.size(); i += 4)
    {
        avg_r += image.pixels[i + 0];
        avg_g += image.pixels[i + 1];
        avg_b += image.pixels[i + 2];
        ++count;
    }

    avg_r /= static_cast<double>(count);
    avg_g /= static_cast<double>(count);
    avg_b /= static_cast<double>(count);

    auto const near_128 = [](double x) { return std::abs(x - 128.0) < 35.0; };
    auto const high_blue = avg_b > 180.0;

    return near_128(avg_r) && near_128(avg_g) && high_blue;
}