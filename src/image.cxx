#include "image.hxx"

#include <stb_image.h>
#include <stb_image_write.h>

#include <stdexcept>
#include <string>

auto load_image_rgba8(std::filesystem::path const& path) -> Image
{
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        throw std::runtime_error("stbi_load failed for: " + path.string());
    }

    Image image {};
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.pixels.assign(data, data + static_cast<usize>(width * height * 4));

    stbi_image_free(data);
    return image;
}

void save_png_rgba8(std::filesystem::path const& path, Image const& image)
{
    auto const stride = static_cast<int>(image.width * 4);
    auto const ok = stbi_write_png(
        path.string().c_str(),
        static_cast<int>(image.width),
        static_cast<int>(image.height),
        4,
        image.pixels.data(),
        stride
    );

    if (ok == 0)
    {
        throw std::runtime_error("stbi_write_png failed for: " + path.string());
    }
}