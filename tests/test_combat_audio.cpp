#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "combat_audio.h"

TEST(CombatAudioTest, SynthesizedCuesAreFiniteBoundedAndDeterministic)
{
    const auto first = synthesizeCombatAudioCue(
        CombatAudioCue::RifleShot, 42U);
    const auto second = synthesizeCombatAudioCue(
        CombatAudioCue::RifleShot, 42U);

    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);
    EXPECT_TRUE(std::all_of(first.begin(), first.end(), [](float sample)
    {
        return std::isfinite(sample) && std::abs(sample) <= 0.85F;
    }));
}

TEST(CombatAudioTest, WeaponAndImpactCuesHaveDistinctEnvelopes)
{
    const auto rifle = synthesizeCombatAudioCue(
        CombatAudioCue::RifleShot, 7U);
    const auto pistol = synthesizeCombatAudioCue(
        CombatAudioCue::PistolShot, 7U);
    const auto impact = synthesizeCombatAudioCue(
        CombatAudioCue::ObstacleImpact, 7U);

    EXPECT_GT(rifle.size(), pistol.size());
    EXPECT_GT(pistol.size(), impact.size());
    EXPECT_NE(rifle, pistol);
}
