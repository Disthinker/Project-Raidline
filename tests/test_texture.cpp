#include "texture.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Texture>);
static_assert(!std::is_copy_assignable_v<Texture>);
static_assert(std::is_move_constructible_v<Texture>);
static_assert(std::is_move_assignable_v<Texture>);

TEST(TextureTest, DefaultConstructedTextureIsInvalid)
{
    Texture texture;

    EXPECT_EQ(texture.get(), nullptr);
    EXPECT_FALSE(texture.valid());
}

TEST(TextureTest, ResetEmptyTextureIsSafe)
{
    Texture texture;

    texture.reset();

    EXPECT_EQ(texture.get(), nullptr);
    EXPECT_FALSE(texture.valid());
}

TEST(TextureTest, MoveConstructedEmptyTextureIsInvalid)
{
    Texture source;
    Texture target{std::move(source)};

    EXPECT_EQ(target.get(), nullptr);
    EXPECT_FALSE(target.valid());
    EXPECT_EQ(source.get(), nullptr);
    EXPECT_FALSE(source.valid());
}

TEST(TextureTest, MoveAssignedEmptyTextureIsInvalid)
{
    Texture source;
    Texture target;

    target = std::move(source);

    EXPECT_EQ(target.get(), nullptr);
    EXPECT_FALSE(target.valid());
    EXPECT_EQ(source.get(), nullptr);
    EXPECT_FALSE(source.valid());
}