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
    EXPECT_EQ(profile.assets.records().size(), 19U);
    EXPECT_EQ(profile.assets.nextAssetId(), 20U);
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
    std::size_t fieldMedical{};
    std::size_t maintenanceKits{};
    std::size_t pistols{};
    std::size_t pistolMagazines{};
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
        if (asset.definitionId == alpha_content::bandage ||
            asset.definitionId == alpha_content::tourniquet ||
            asset.definitionId == alpha_content::painkiller)
        {
            ++fieldMedical;
        }
        if (asset.definitionId == alpha_content::rifle)
        {
            EXPECT_EQ(asset.currentMaximumDurability, 10000U);
            EXPECT_EQ(asset.currentDurability, 10000U);
            EXPECT_EQ(asset.weaponMalfunction, WeaponMalfunctionType::None);
        }
        if (asset.definitionId == alpha_content::pistol)
        {
            ++pistols;
            EXPECT_EQ(asset.currentMaximumDurability, 10000U);
            EXPECT_EQ(asset.currentDurability, 10000U);
            EXPECT_EQ(asset.weaponMalfunction, WeaponMalfunctionType::None);
        }
        if (asset.definitionId == alpha_content::pistolMagazine)
        {
            ++pistolMagazines;
        }
        if (asset.definitionId == alpha_content::weaponMaintenanceKit)
        {
            ++maintenanceKits;
            EXPECT_EQ(asset.remainingCharges, 2500U);
        }
    }
    EXPECT_EQ(ammunition, 90U);
    EXPECT_EQ(magazines, 3U);
    EXPECT_EQ(medkits, 2U);
    EXPECT_EQ(protectiveGear, 2U);
    EXPECT_EQ(fieldMedical, 3U);
    EXPECT_EQ(maintenanceKits, 1U);
    EXPECT_EQ(pistols, 1U);
    EXPECT_EQ(pistolMagazines, 2U);
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

TEST(ProfileStateTest, InvalidMedicalTimerCombinationIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-medical-invalid",
        publishedContentRegistry());
    profile.medicalStatus.bleeding = BleedingSeverity::Heavy;

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("medical"), std::string::npos);
}
