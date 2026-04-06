#include "app_encode.hxx"

#include <span>

auto main(int argc, char** argv) -> int
{
    EncodeAppConfig config {};

    auto const args = std::span<char const* const> {argv, static_cast<usize>(argc)};
    auto const parse_result = parse_encode_args(args, config);

    switch (parse_result)
    {
        case CliParseResult::Ok:
            return run_encode_app(config);

        case CliParseResult::Help:
            return 0;

        case CliParseResult::Error:
            return 1;
    }

    return 1;
}