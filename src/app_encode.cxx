#include "app_encode.hxx"

#include "compression.hxx"
#include "format.hxx"
#include "image.hxx"
#include "ktx2_write.hxx"
#include "mip_chain.hxx"

#include <lyra/lyra.hpp>

#include <iostream>
#include <string>

namespace
{
    auto print_error(std::string const& message) -> int
    {
        std::cerr << "error: " << message << '\n';
        return 1;
    }
}

auto parse_encode_args(
    std::span<char const* const> args,
    EncodeAppConfig& config
) -> CliParseResult
{
    config = EncodeAppConfig {};

    auto encode_text = std::string {"none"};
    bool help = false;

    auto cli = lyra::cli()
        | lyra::help(help)
        | lyra::opt(config.input, "path")("Input PNG or JPG file")
            ["--input"]
            .required()
        | lyra::opt(config.output, "path")("Output KTX2 file")
            ["--output"]
            .required()
        | lyra::opt(config.format_text, "format")("Texture format: r8g8b8a8_unorm or r8g8b8a8_srgb")
            ["--format"]
        | lyra::opt(encode_text, "mode")("Compression mode: none, basis-lz, uastc")
            ["--encode"]
        | lyra::opt(config.compression.normal_map)("Treat input as a normal map")
            ["--normal-map"];

    auto const result = cli.parse({static_cast<int>(args.size()), args.data()});

    if (help)
    {
        std::cout << cli << '\n';
        return CliParseResult::Help;
    }

    if (!result)
    {
        std::cerr << result.message() << '\n' << cli << '\n';
        return CliParseResult::Error;
    }

    config.compression.normal_map_was_forced = config.compression.normal_map;

    if (encode_text == "none")
    {
        config.compression.mode = CompressionMode::None;
    }
    else if (encode_text == "basis-lz")
    {
        config.compression.mode = CompressionMode::BasisLz;
    }
    else if (encode_text == "uastc")
    {
        config.compression.mode = CompressionMode::Uastc;
    }
    else
    {
        std::cerr
            << "error: unsupported value for --encode: " << encode_text << '\n'
            << cli << '\n';
        return CliParseResult::Error;
    }

    return CliParseResult::Ok;
}

auto run_encode_app(EncodeAppConfig const& config) -> int
{
    PixelFormat const format = parse_format(config.format_text);
    Image const image = load_image_rgba8(config.input);

    auto compression = config.compression;
    if (compression.mode != CompressionMode::None && !compression.normal_map_was_forced)
    {
        compression.normal_map = check_if_is_normal_map(config.input, image);
    }

    std::vector<Image> const mip_chain = generate_mip_chain(image, format);

    if (!write_ktx2(config.output, format, mip_chain, compression))
    {
        return print_error("failed to write KTX2 file");
    }

    std::cout
        << "wrote " << config.output.string()
        << " with " << mip_chain.size() << " mip levels\n";

    if (compression.mode != CompressionMode::None)
    {
        std::cout
            << "compression: "
            << (compression.mode == CompressionMode::BasisLz ? "basis-lz" :
                compression.mode == CompressionMode::Uastc ? "uastc" : "none")
            << ", normal_map=" << (compression.normal_map ? "true" : "false")
            << '\n';
    }

    return 0;
}