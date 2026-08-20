#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "profile_state.h"

TEST(ProfileStateTest, NewAlphaProfileCreatesContractAssets)
{
    const ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        publishedContentRegistry());

    EXPECT_EQ(profile.currency, 200U);
    EXPECT_EQ(profile.revision, 1U);
    EXPECT_EQ(profile.assets.records().size(), 12U);
    EXPECT_EQ(profile.assets.nextAssetId(), 13U);
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    std::uint64_t ammunition{};
    std::size_t magazines{};
    std::size_t medkits{};
    std::size_t protectiveGear{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == alpha_content::ammunition)
        {
            ammunition += asset.quantity;
        }
        if (asset.definitionId == alpha_content::magazine)
        {
            ++magazines;
        }
        if (asset.definitionId == alpha_content::medkit)
        {
            ++medkits;
            EXPECT_EQ(asset.remainingCharges, 3U);
        }
        if (asset.definitionId == alpha_content::helmet ||
            asset.definitionId == alpha_content::bodyArmor)
        {
            ++protectiveGear;
            EXPECT_GT(asset.currentMaximumDurability, 0U);
            EXPECT_EQ(asset.currentDurability, asset.currentMaximumDurability);
        }
    }
    EXPECT_EQ(ammunition, 90U);
    EXPECT_EQ(magazines, 3U);
    EXPECT_EQ(medkits, 2U);
    EXPECT_EQ(protectiveGear, 2U);
}

TEST(ProfileStateTest, BackwardHighWaterMarkIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        publishedContentRegistry());
    profile.assets.setNextAssetIdForLoad(2);

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("high-water"), std::string::npos);
}

TEST(ProfileStateTest, FingerprintChangesWithAuthoritativeState)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);
    ++profile.currency;
    EXPECT_NE(profileStateFingerprint(profile), before);
}
