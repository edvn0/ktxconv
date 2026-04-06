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



TEST_CASE("Image bytes iterates one byte at a time") {
    Image image {};
    image.width = 1;
    image.height = 2;
    image.pixels = {1, 2, 3, 4, 5, 6, 7, 8};

    std::vector<u8> collected {};
    for (u8 byte : image.bytes()) {
        collected.push_back(byte);
    }

    REQUIRE(collected.size() == 8);
    CHECK(collected[0] == 1);
    CHECK(collected[1] == 2);
    CHECK(collected[2] == 3);
    CHECK(collected[3] == 4);
    CHECK(collected[4] == 5);
    CHECK(collected[5] == 6);
    CHECK(collected[6] == 7);
    CHECK(collected[7] == 8);
}

TEST_CASE("Image four_channel_pixels iterates RGBA groups") {
    Image image {};
    image.width = 2;
    image.height = 1;
    image.pixels = {
        10, 20, 30, 40,
        50, 60, 70, 80
    };

    std::vector<std::array<u8, 4>> collected {};
    for (auto pixel : image.four_channel_pixels()) {
        collected.push_back({pixel[0], pixel[1], pixel[2], pixel[3]});
    }

    REQUIRE(collected.size() == 2);
    CHECK(collected[0] == std::array<u8, 4> {10, 20, 30, 40});
    CHECK(collected[1] == std::array<u8, 4> {50, 60, 70, 80});
}