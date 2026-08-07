#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "extraction_point.h"

TEST(ExtractionPointTest, RejectsInvalidGeometry)
{
    EXPECT_THROW(
        (void)ExtractionPoint(Vec2{}, Vec2{0.0F, 10.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)ExtractionPoint(Vec2{}, Vec2{10.0F, -1.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)ExtractionPoint(
            Vec2{std::numeric_limits<float>::infinity(), 0.0F},
            Vec2{10.0F, 10.0F}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)ExtractionPoint(
            Vec2{},
            Vec2{10.0F, std::numeric_limits<float>::quiet_NaN()}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)ExtractionPoint(
            Vec2{std::numeric_limits<float>::max(), 0.0F},
            Vec2{std::numeric_limits<float>::max(), 10.0F}),
        std::invalid_argument);
}

TEST(ExtractionPointTest, PreservesBounds)
{
    const ExtractionPoint point{
        Vec2{10.0F, 20.0F},
        Vec2{30.0F, 40.0F}};

    EXPECT_FLOAT_EQ(point.bounds().position.x, 10.0F);
    EXPECT_FLOAT_EQ(point.bounds().position.y, 20.0F);
    EXPECT_FLOAT_EQ(point.bounds().size.x, 30.0F);
    EXPECT_FLOAT_EQ(point.bounds().size.y, 40.0F);
}

TEST(ExtractionPointTest, ContainsInteriorAndLeftTopEdges)
{
    const ExtractionPoint point{
        Vec2{10.0F, 20.0F},
        Vec2{30.0F, 40.0F}};

    EXPECT_TRUE(point.contains(Vec2{20.0F, 30.0F}));
    EXPECT_TRUE(point.contains(Vec2{10.0F, 20.0F}));
    EXPECT_TRUE(point.contains(Vec2{10.0F, 59.0F}));
    EXPECT_TRUE(point.contains(Vec2{39.0F, 20.0F}));
}

TEST(ExtractionPointTest, ExcludesRightBottomEdgesAndOutsidePoints)
{
    const ExtractionPoint point{
        Vec2{10.0F, 20.0F},
        Vec2{30.0F, 40.0F}};

    EXPECT_FALSE(point.contains(Vec2{40.0F, 30.0F}));
    EXPECT_FALSE(point.contains(Vec2{20.0F, 60.0F}));
    EXPECT_FALSE(point.contains(Vec2{9.0F, 30.0F}));
    EXPECT_FALSE(point.contains(Vec2{20.0F, 19.0F}));
}

TEST(ExtractionPointTest, ExcludesNonFinitePoints)
{
    const ExtractionPoint point{
        Vec2{10.0F, 20.0F},
        Vec2{30.0F, 40.0F}};

    EXPECT_FALSE(point.contains(Vec2{
        std::numeric_limits<float>::quiet_NaN(),
        30.0F}));
    EXPECT_FALSE(point.contains(Vec2{
        20.0F,
        std::numeric_limits<float>::infinity()}));
}
