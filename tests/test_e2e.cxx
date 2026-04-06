#include "app_encode.hxx"
#include "cli_parse_result.hxx"
#include "format.hxx"
#include "image.hxx"
#include "ktx2_decode.hxx"
#include "ktx2_write.hxx"
#include "mip_chain.hxx"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] auto make_checkerboard() -> Image
    {
        Image image {};
        image.width = 4;
        image.height = 4;
        image.pixels.resize(4 * 4 * 4);

        for (u32 y = 0; y < image.height; ++y)
        {
            for (u32 x = 0; x < image.width; ++x)
            {
                auto const offset = image.pixel_offset(x, y);
                auto const white = ((x + y) & 1u) == 0u ? 255u : 0u;

                image.pixels[offset + 0] = static_cast<u8>(white);
                image.pixels[offset + 1] = static_cast<u8>(white);
                image.pixels[offset + 2] = static_cast<u8>(white);
                image.pixels[offset + 3] = 255;
            }
        }

        return image;
    }

    [[nodiscard]] auto decode_normal(u8 r, u8 g, u8 b) -> std::array<float, 3>
    {
        auto const x = (static_cast<float>(r) / 255.0f) * 2.0f - 1.0f;
        auto const y = (static_cast<float>(g) / 255.0f) * 2.0f - 1.0f;
        auto const z = (static_cast<float>(b) / 255.0f) * 2.0f - 1.0f;

        auto const length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.0f)
        {
            return {0.0f, 0.0f, 1.0f};
        }

        return {x / length, y / length, z / length};
    }

    [[nodiscard]] auto angular_error_degrees(
        std::array<float, 3> const& a,
        std::array<float, 3> const& b
    ) -> float
    {
        auto const dot =
            std::clamp(
                a[0] * b[0] + a[1] * b[1] + a[2] * b[2],
                -1.0f,
                1.0f
            );

        return std::acos(dot) * 57.2957795f;
    }

    [[nodiscard]] auto percentile(std::vector<float> values, double p) -> float
    {
        REQUIRE_FALSE(values.empty());
        REQUIRE(p >= 0.0);
        REQUIRE(p <= 1.0);

        std::sort(values.begin(), values.end());

        auto const index = static_cast<usize>(
            std::clamp(
                static_cast<usize>(std::lround((values.size() - 1) * p)),
                static_cast<usize>(0),
                values.size() - 1
            )
        );

        return values[index];
    }
}

TEST_CASE("png to ktx2 e2e")
{
    auto const temp_dir = std::filesystem::temp_directory_path() / "texture_tool_tests";
    std::filesystem::create_directories(temp_dir);

    auto const input = temp_dir / "checker.png";
    auto const output = temp_dir / "checker.ktx2";

    save_png_rgba8(input, make_checkerboard());

    auto const input_as_string = input.string();
    auto const output_as_string = output.string();

    std::vector<char const*> args {
        "texture_tool",
        "--input", input_as_string.c_str(),
        "--output", output_as_string.c_str(),
        "--format", "r8g8b8a8_unorm"
    };

    EncodeAppConfig app_config {};
    REQUIRE_EQ(parse_encode_args(std::span<char const* const> {args.data(), args.size()}, app_config), CliParseResult::Ok);
    CHECK(run_encode_app(app_config) == 0);

    CHECK(std::filesystem::exists(output));

    auto const info = read_ktx2_info(output);
    CHECK(info.width == 4);
    CHECK(info.height == 4);
    CHECK(info.levels == 3);
    CHECK(info.vk_format == static_cast<u32>(VK_FORMAT_R8G8B8A8_UNORM));
}

TEST_CASE("normal texture generates full mip chain")
{
    auto const path = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";

    auto image = load_image_rgba8(path);
    REQUIRE_FALSE(image.empty());

    CHECK(image.width == 1024);
    CHECK(image.height == 1024);

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);
    REQUIRE_FALSE(chain.empty());

    REQUIRE(chain.size() == 11);

    CHECK(chain[0].width == 1024);
    CHECK(chain[1].width == 512);
    CHECK(chain[2].width == 256);
    CHECK(chain[10].width == 1);

    CHECK(chain[0].height == 1024);
    CHECK(chain[1].height == 512);
    CHECK(chain[2].height == 256);
    CHECK(chain[10].height == 1);
}

TEST_CASE("each mip has correct byte size")
{
    auto const path = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";

    auto const image = load_image_rgba8(path);
    REQUIRE_FALSE(image.empty());

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);
    REQUIRE_FALSE(chain.empty());

    for (auto const& mip : chain)
    {
        CHECK(mip.pixels.size() == static_cast<usize>(mip.width) * mip.height * 4);
    }
}

TEST_CASE("normal texture mip chain retains positive average z")
{
    auto const path = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";

    auto const image = load_image_rgba8(path);
    REQUIRE_FALSE(image.empty());

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);
    REQUIRE_FALSE(chain.empty());

    for (auto const& mip : chain)
    {
        double z_sum = 0.0;
        usize count = 0;

        for (usize i = 0; i < mip.pixels.size(); i += 4)
        {
            auto const n = decode_normal(
                mip.pixels[i + 0],
                mip.pixels[i + 1],
                mip.pixels[i + 2]
            );

            z_sum += n[2];
            ++count;
        }

        auto const avg_z = z_sum / static_cast<double>(count);
        CHECK(avg_z > 0.0);
    }
}

TEST_CASE("normal texture png to ktx2 e2e")
{
    auto const input = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";
    auto const output = std::filesystem::temp_directory_path() / "normal.ktx2";

    EncodeAppConfig config {};
    config.input = input;
    config.output = output;
    config.format_text = "r8g8b8a8_unorm";

    REQUIRE_EQ(run_encode_app(config), 0);

    auto const info = read_ktx2_info(output);
    CHECK(info.width == 1024);
    CHECK(info.height == 1024);
    CHECK(info.levels == 11);
    CHECK(info.vk_format == static_cast<u32>(VK_FORMAT_R8G8B8A8_UNORM));
}

TEST_CASE("normal texture conversion is deterministic")
{
    auto const input = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";
    auto const temp = std::filesystem::temp_directory_path();

    auto const out_a = temp / "normal_a.ktx2";
    auto const out_b = temp / "normal_b.ktx2";

    EncodeAppConfig config_a {};
    config_a.input = input;
    config_a.output = out_a;
    config_a.format_text = "r8g8b8a8_unorm";

    EncodeAppConfig config_b = config_a;
    config_b.output = out_b;

    REQUIRE_EQ(run_encode_app(config_a), 0);
    REQUIRE_EQ(run_encode_app(config_b), 0);

    auto const size_a = std::filesystem::file_size(out_a);
    auto const size_b = std::filesystem::file_size(out_b);
    CHECK(size_a == size_b);

    std::ifstream fa(out_a, std::ios::binary);
    std::ifstream fb(out_b, std::ios::binary);

    std::vector<char> a((std::istreambuf_iterator<char>(fa)), {});
    std::vector<char> b((std::istreambuf_iterator<char>(fb)), {});
    CHECK(a == b);
}

TEST_CASE("normal texture uncompressed round trips byte exactly")
{
    auto const input = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";
    auto const temp_dir = std::filesystem::temp_directory_path() / "texture_tool_tests";
    std::filesystem::create_directories(temp_dir);

    auto const output = temp_dir / "normal_uncompressed.ktx2";
    auto const decoded_png = temp_dir / "normal_uncompressed_decoded.png";

    auto const original = load_image_rgba8(input);
    REQUIRE_FALSE(original.empty());

    EncodeAppConfig config {};
    config.input = input;
    config.output = output;
    config.format_text = "r8g8b8a8_unorm";
    config.compression.mode = CompressionMode::None;
    config.compression.normal_map = true;
    config.compression.normal_map_was_forced = true;

    REQUIRE_EQ(run_encode_app(config), 0);

    auto decoded = decode_ktx2_to_rgba8(output);
    REQUIRE_FALSE(decoded.image.empty());

    if (decoded.semantic == DecodedKtxSemantic::PackedNormalXY)
    {
        decoded.image = reconstruct_normal_map_from_packed_xy(decoded.image);
    }

    save_png_rgba8(decoded_png, decoded.image);

    CHECK(decoded.image.width == original.width);
    CHECK(decoded.image.height == original.height);
    CHECK(decoded.image.pixels == original.pixels);
}

TEST_CASE("normal texture uastc round trips within angular tolerance")
{
    auto const input = std::filesystem::path(TEST_ASSETS_DIR) / "images" / "normal.png";
    auto const temp_dir = std::filesystem::temp_directory_path() / "texture_tool_tests";
    std::filesystem::create_directories(temp_dir);

    auto const output = temp_dir / "normal_uastc.ktx2";
    auto const decoded_png = temp_dir / "normal_uastc_decoded.png";

    auto const original = load_image_rgba8(input);
    REQUIRE_FALSE(original.empty());

    EncodeAppConfig config {};
    config.input = input;
    config.output = output;
    config.format_text = "r8g8b8a8_unorm";
    config.compression.mode = CompressionMode::Uastc;
    config.compression.normal_map = true;
    config.compression.normal_map_was_forced = true;

    REQUIRE_EQ(run_encode_app(config), 0);

    auto decoded = decode_ktx2_to_rgba8(output);
    REQUIRE_FALSE(decoded.image.empty());

    if (decoded.semantic == DecodedKtxSemantic::PackedNormalXY)
    {
        decoded.image = reconstruct_normal_map_from_packed_xy(decoded.image);
    }

    save_png_rgba8(decoded_png, decoded.image);

    CHECK(decoded.image.width == original.width);
    CHECK(decoded.image.height == original.height);

    std::vector<float> angular_errors {};
    angular_errors.reserve(original.pixels.size() / 4);

    double avg_error_deg = 0.0;

    for (usize i = 0; i < original.pixels.size(); i += 4)
    {
        auto const a = decode_normal(
            original.pixels[i + 0],
            original.pixels[i + 1],
            original.pixels[i + 2]
        );

        auto const b = decode_normal(
            decoded.image.pixels[i + 0],
            decoded.image.pixels[i + 1],
            decoded.image.pixels[i + 2]
        );

        auto const error_deg = angular_error_degrees(a, b);
        angular_errors.push_back(error_deg);
        avg_error_deg += error_deg;
    }

    avg_error_deg /= static_cast<double>(angular_errors.size());

    auto const p95_error_deg = percentile(angular_errors, 0.95);
    auto const p99_error_deg = percentile(angular_errors, 0.99);

    CHECK(avg_error_deg < 10.0);
    CHECK(p95_error_deg < 20.0f);
    CHECK(p99_error_deg < 45.0f);
}