#include <gtest/gtest.h>
#include "hit_effect.h"

namespace
{
    constexpr Vec2 kPosition{20.0f, 30.0f};
    constexpr float kLifetime{0.15f};
    constexpr float kSize{16.0f};
    constexpr float kDeltaTime{0.05f};
    constexpr float kEpsilon{0.0001f};
}

// 初始状态能保存 position、lifetime 和 size
TEST(HitEffectTest, StoresInitialState)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    EXPECT_FLOAT_EQ(effect.position().x, 20.0f);
    EXPECT_FLOAT_EQ(effect.position().y, 30.0f);
    EXPECT_FLOAT_EQ(effect.lifetimeRemaining(), 0.15f);
    EXPECT_FLOAT_EQ(effect.size(), 16.0f);
}

// update 会按 deltaTime 减少 lifetimeRemaining
TEST(HitEffectTest, UpdateReducesLifetime)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    effect.update(kDeltaTime);

    EXPECT_NEAR(effect.lifetimeRemaining(), 0.10f, kEpsilon);
}

// lifetimeRemaining 大于 0 时未过期
TEST(HitEffectTest, PositiveLifetimeIsNotExpired)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    EXPECT_FALSE(effect.isExpired());
}

// lifetimeRemaining 等于 0 时过期
TEST(HitEffectTest, ZeroLifetimeIsExpired)
{
    HitEffect effect(kPosition, 0.0f, kSize);

    EXPECT_TRUE(effect.isExpired());
}

// lifetimeRemaining 小于 0 时过期
TEST(HitEffectTest, NegativeLifetimeIsExpired)
{
    HitEffect effect(kPosition, -0.01f, kSize);

    EXPECT_TRUE(effect.isExpired());
}

// update 不应该改变 position
TEST(HitEffectTest, UpdateDoesNotChangePosition)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    effect.update(kDeltaTime);

    EXPECT_FLOAT_EQ(effect.position().x, 20.0f);
    EXPECT_FLOAT_EQ(effect.position().y, 30.0f);
}

// update 不应该改变 size
TEST(HitEffectTest, UpdateDoesNotChangeSize)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    effect.update(kDeltaTime);

    EXPECT_FLOAT_EQ(effect.size(), 16.0f);
}

// 多次 update 后，lifetimeRemaining 小于等于 0 时过期
TEST(HitEffectTest, MultipleUpdatesEventuallyExpire)
{
    HitEffect effect(kPosition, kLifetime, kSize);

    effect.update(0.05f);
    effect.update(0.05f);
    effect.update(0.05f);

    EXPECT_TRUE(effect.isExpired());
    EXPECT_NEAR(effect.lifetimeRemaining(), 0.0f, kEpsilon);
}