#pragma once

#include "cli_parse_result.hxx"
#include <filesystem>
#include <span>

enum class DecodeNormalMode
{
    AsStored,
    ReconstructXYZ
};

struct DecodeAppConfig
{
    std::filesystem::path input {};
    std::filesystem::path output {};
    DecodeNormalMode normal_mode {DecodeNormalMode::AsStored};
};

[[nodiscard]] auto parse_decode_args(std::span<char const* const> args, DecodeAppConfig& config) -> CliParseResult;
[[nodiscard]] auto run_decode_app(DecodeAppConfig const& config) -> int;