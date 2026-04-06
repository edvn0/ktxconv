#pragma once

#include <cstdint>
#include <cstddef>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using usize = std::size_t;
using f32 = float;
using f64 = double;
using i32 = std::int32_t;
using i64 = std::int64_t;


template <typename Iterator>
struct IteratorRange final {
    Iterator first {};
    Iterator last {};

    auto begin() const -> Iterator {
        return first;
    }

    auto end() const -> Iterator {
        return last;
    }
};

struct Deleter {
    template<typename T>
    auto operator()(T* ptr) const -> void;
};
