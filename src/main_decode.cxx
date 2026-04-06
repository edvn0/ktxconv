#include "app_decode.hxx"
#include "types.hxx"

#include <span>

auto main(int argc, char** argv) -> int
{
    DecodeAppConfig config {};

    auto const args = std::span<char const* const> {argv, static_cast<usize>(argc)};
    auto const parse_result = parse_decode_args(args, config);

    switch (parse_result)
    {
        case CliParseResult::Ok:
            return run_decode_app(config);

        case CliParseResult::Help:
            return 0;

        case CliParseResult::Error:
            return 1;
    }

    return 1;
}