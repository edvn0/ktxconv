#pragma once

#include "cli_parse_result.hxx"
#include "compression.hxx"

#include <filesystem>
#include <span>
#include <string>

struct EncodeAppConfig
{
    std::filesystem::path input {};
    std::filesystem::path output {};
    std::string format_text {"r8g8b8a8_unorm"};
    CompressionConfig compression {};
};

[[nodiscard]] auto parse_encode_args(std::span<char const* const> args, EncodeAppConfig& config) -> CliParseResult;
[[nodiscard]] auto run_encode_app(EncodeAppConfig const& config) -> int;