#pragma once

#include "types.hxx"

#include <cassert>
#include <filesystem>
#include <span>
#include <vector>

enum class PixelFormat { R8G8B8A8_UNORM, R8G8B8A8_SRGB };

class Image final {
private:
  struct ByteIterator final {
    using value_type = u8;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    u8 const *ptr{};

    auto operator*() const -> u8 const & { return *ptr; }

    auto operator++() -> ByteIterator & {
      ++ptr;
      return *this;
    }

    auto operator==(ByteIterator const &other) const -> bool = default;
  };

  struct FourChannelReadOnlyIterator final {
    using value_type = std::span<const u8, 4>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    u8 const *ptr{};

    auto operator*() const -> value_type {
        assert(ptr != nullptr && "FourChannelReadOnlyIterator: dereferencing null pointer");
      return std::span<const u8, 4>{ptr, 4};
    }

    auto operator++() -> FourChannelReadOnlyIterator & {
      ptr += 4;
      return *this;
    }

    auto operator==(FourChannelReadOnlyIterator const &other) const -> bool = default;
  };

public:
  u32 width{0};
  u32 height{0};
  std::vector<u8> pixels{};

  [[nodiscard]] auto empty() const -> bool {
    return width == 0 || height == 0 || pixels.empty();
  }

  [[nodiscard]] auto pixel_offset(u32 x, u32 y) const -> usize {
    assert(x < width && y < height && "pixel_offset: x or y out of bounds");
    return static_cast<usize>((y * width + x) * 4);
  }

  auto operator[](std::integral auto index) const -> decltype(auto) {
    return pixels[index];
  }

  auto operator[](std::integral auto x, std::integral auto y) const
      -> decltype(auto) {
    return pixels[pixel_offset(x, y)];
  }

  [[nodiscard]] auto bytes() const -> IteratorRange<ByteIterator> {
    return {.first = ByteIterator{pixels.data()},
            .last = ByteIterator{pixels.data() + pixels.size()}};
  }

  [[nodiscard]] auto four_channel_pixels() const
      -> IteratorRange<FourChannelReadOnlyIterator> {
    assert(pixels.size() % 4 == 0 &&
           "four_channel_pixels: pixel buffer size must be divisible by 4");

    return {.first = FourChannelReadOnlyIterator{pixels.data()},
            .last = FourChannelReadOnlyIterator{pixels.data() + pixels.size()}};
  }
};

[[nodiscard]] auto load_image_rgba8(std::filesystem::path const &path) -> Image;
void save_png_rgba8(std::filesystem::path const &path, Image const &image);