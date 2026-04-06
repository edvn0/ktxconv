#include "app_decode.hxx"

#include "image.hxx"
#include "ktx2_decode.hxx"

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

auto parse_decode_args(
    std::span<char const* const> args,
    DecodeAppConfig& config
) -> CliParseResult
{
    config = DecodeAppConfig {};

    auto normal_mode_text = std::string {"as-stored"};
    bool help = false;

    auto cli = lyra::cli()
        | lyra::help(help)
        | lyra::opt(config.input, "path")("Input KTX2 file")
            ["--input"]
            .required()
        | lyra::opt(config.output, "path")("Output PNG file")
            ["--output"]
            .required()
        | lyra::opt(normal_mode_text, "mode")("Normal decode mode: as-stored, reconstruct-xyz")
            ["--normal-mode"];

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

    if (normal_mode_text == "as-stored")
    {
        config.normal_mode = DecodeNormalMode::AsStored;
    }
    else if (normal_mode_text == "reconstruct-xyz")
    {
        config.normal_mode = DecodeNormalMode::ReconstructXYZ;
    }
    else
    {
        std::cerr
            << "error: unsupported value for --normal-mode: " << normal_mode_text << '\n'
            << cli << '\n';
        return CliParseResult::Error;
    }

    return CliParseResult::Ok;
}

auto run_decode_app(DecodeAppConfig const& config) -> int
{
    DecodedKtxImage decoded = decode_ktx2_to_rgba8(config.input);

    if (decoded.semantic == DecodedKtxSemantic::PackedNormalXY &&
        config.normal_mode == DecodeNormalMode::ReconstructXYZ)
    {
        decoded.image = reconstruct_normal_map_from_packed_xy(decoded.image);
    }

    save_png_rgba8(config.output, decoded.image);

    std::cout
        << "decoded " << config.input.string()
        << " -> " << config.output.string() << '\n';

    std::cout
        << "semantic: "
        << (decoded.semantic == DecodedKtxSemantic::PackedNormalXY
            ? "packed-normal-xy"
            : "color")
        << '\n';

    if (decoded.semantic == DecodedKtxSemantic::PackedNormalXY)
    {
        std::cout
            << "normal_mode="
            << (config.normal_mode == DecodeNormalMode::ReconstructXYZ
                ? "reconstruct-xyz"
                : "as-stored")
            << '\n';
    }

    return 0;
}