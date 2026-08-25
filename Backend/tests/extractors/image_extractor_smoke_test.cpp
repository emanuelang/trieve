#include "semantic_fs/extractors/image_extractor.h"

#include <catch2/catch_test_macros.hpp>

using semantic_fs::extractors::ImageExtractor;

TEST_CASE("ImageExtractor recognizes mixed-case supported extensions")
{
    const ImageExtractor extractor;

    REQUIRE(extractor.supports("fixture.JpEg"));
}

TEST_CASE("ImageExtractor rejects unsupported extensions")
{
    const ImageExtractor extractor;

    REQUIRE_FALSE(extractor.supports("fixture.txt"));
}
