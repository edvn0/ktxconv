#pragma once

#include "image.hxx"
#include "types.hxx"


enum class CompressionMode
{
    None,
    BasisLz,
    Uastc
};

struct CompressionConfig
{
    CompressionMode mode {CompressionMode::None};
    bool normal_map {false};
    bool normal_map_was_forced {false};

    u32 quality_level {255};
    u32 compression_level {2};
    u32 thread_count {0};

    bool uastc_rdo {false};
    f32 uastc_rdo_quality_scalar {1.0f};
    u32 uastc_rdo_dict_size {4096};
    f32 uastc_rdo_max_smooth_block_error_scale {10.0f};
    f32 uastc_rdo_max_smooth_block_std_dev {18.0f};
};
[[nodiscard]] auto check_if_is_normal_map(const std::filesystem::path& filename_with_extension, Image const& image) -> bool;