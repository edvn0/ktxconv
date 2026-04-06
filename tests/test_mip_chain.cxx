#include "mip_chain.hxx"

#include <doctest/doctest.h>

TEST_CASE("mip_level_count basic cases")
{
    CHECK(mip_level_count(1, 1) == 1);
    CHECK(mip_level_count(2, 2) == 2);
    CHECK(mip_level_count(4, 4) == 3);
    CHECK(mip_level_count(3, 5) == 3);
    CHECK(mip_level_count(1024, 1) == 11);

    
    CHECK(mip_level_count(5, 3) == 3);
    CHECK(mip_level_count(1, 1024) == 11);
}

TEST_CASE("generate_mip_chain dimensions")
{
    Image image {};
    image.width = 4;
    image.height = 4;
    image.pixels.resize(4 * 4 * 4, 255);

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);

    REQUIRE(chain.size() == 3);
    CHECK(chain[0].width == 4);
    CHECK(chain[0].height == 4);
    CHECK(chain[1].width == 2);
    CHECK(chain[1].height == 2);
    CHECK(chain[2].width == 1);
    CHECK(chain[2].height == 1);
}

TEST_CASE("generate_mip_chain dimensions (4k)")
{
    Image image {};
    image.width = 4096;
    image.height = 4096;
    image.pixels.resize(4096 * 4096 * 4, 255);

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);

    REQUIRE(chain.size() == 13);
    CHECK(chain[0].width == 4096);
    CHECK(chain[0].height == 4096);
    CHECK(chain[1].width == 2048);
    CHECK(chain[1].height == 2048);
    CHECK(chain[12].width == 1);
    CHECK(chain[12].height == 1);
}

TEST_CASE("generate_mip_chain averages texels")
{
    Image image {};
    image.width = 2;
    image.height = 2;
    image.pixels = {
        0,   0,   0,   255,
        255, 0,   0,   255,
        0,   255, 0,   255,
        255, 255, 0,   255
    };

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);

    REQUIRE(chain.size() == 2);
    REQUIRE(chain[1].pixels.size() == 4);

    CHECK(chain[1].pixels[0] == 128);
    CHECK(chain[1].pixels[1] == 128);
    CHECK(chain[1].pixels[2] == 0);
    CHECK(chain[1].pixels[3] == 255);
}

TEST_CASE("generate_mip_chain preserves constant color")
{
    Image image {};
    image.width = 4;
    image.height = 4;
    image.pixels.resize(4 * 4 * 4);

    for (u32 i = 0; i < 16; ++i)
    {
        image.pixels[i * 4 + 0] = 64;
        image.pixels[i * 4 + 1] = 128;
        image.pixels[i * 4 + 2] = 192;
        image.pixels[i * 4 + 3] = 255;
    }

    auto const chain = generate_mip_chain(image, PixelFormat::R8G8B8A8_UNORM);

    REQUIRE(chain.size() == 3);

    for (auto const& mip : chain)
    {
        for (usize i = 0; i < mip.pixels.size(); i += 4)
        {
            CHECK(mip.pixels[i + 0] == 64);
            CHECK(mip.pixels[i + 1] == 128);
            CHECK(mip.pixels[i + 2] == 192);
            CHECK(mip.pixels[i + 3] == 255);
        }
    }
}

