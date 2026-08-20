#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "weapon_ammo_domain.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const auto found = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&definitionId](const auto &entry)
        {
            return entry.second.definitionId == definitionId;
        });
    return found == profile.assets.records().end() ? 0 : found->first;
}
}

TEST(WeaponAmmoDomainTest, LoadInstallChamberAndFirePreserveRealAmmunition)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-test",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);

    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 30},
        CommandContext{profile.revision, "load-magazine"}).succeeded);
    EXPECT_EQ(magazineRoundCount(profile, magazine), 30U);

    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "install-magazine"}).succeeded);
    EXPECT_EQ(installedMagazine(profile, rifle), magazine);

    const WeaponAmmoReceipt chambered = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{profile.revision, "chamber-round"});
    ASSERT_TRUE(chambered.succeeded);
    EXPECT_EQ(chambered.result, WeaponAmmoResult::Chambered);
    EXPECT_EQ(magazineRoundCount(profile, magazine), 29U);

    const WeaponAmmoReceipt fired = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{profile.revision, "fire-round"});
    ASSERT_TRUE(fired.succeeded);
    EXPECT_EQ(fired.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(fired.firedAmmunitionDefinitionId, alpha_content::ammunition);
    EXPECT_EQ(magazineRoundCount(profile, magazine), 28U);
    ASSERT_NE(profile.assets.find(rifle), nullptr);
    EXPECT_TRUE(profile.assets.find(rifle)->chamberedRound.has_value());
}

TEST(WeaponAmmoDomainTest, UnloadPreservesReliefBatchProvenance)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-relief",
        publishedContentRegistry());
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    profile.assets.findMutable(ammunition)->reliefBatchId = "relief-1";

    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 12},
        CommandContext{profile.revision, "load-relief"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        UnloadMagazineCommand{magazine, ProfileContainerId::stash()},
        CommandContext{profile.revision, "unload-relief"}).succeeded);

    EXPECT_EQ(magazineRoundCount(profile, magazine), 0U);
    std::uint32_t reliefRounds{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == alpha_content::ammunition &&
            asset.reliefBatchId == "relief-1")
        {
            reliefRounds += asset.quantity;
        }
    }
    EXPECT_EQ(reliefRounds, 60U);
}

TEST(WeaponAmmoDomainTest, RejectedCommandDoesNotMutateProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-reject",
        publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);

    const WeaponAmmoReceipt result = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{9999, 8888, 1},
        CommandContext{profile.revision, "invalid-load"});

    EXPECT_FALSE(result.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(WeaponAmmoDomainTest, InstallAndChamberIsOneAtomicCommand)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-prepare",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 12},
        CommandContext{profile.revision, "prepare-load"}).succeeded);

    const WeaponAmmoPlan plan = queryWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineAndChamberCommand{rifle, magazine});
    EXPECT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.result, WeaponAmmoResult::InstalledAndChambered);
    const std::uint64_t before = profileStateFingerprint(profile);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineAndChamberCommand{rifle, magazine},
        CommandContext{profile.revision, "prepare-weapon"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.result, WeaponAmmoResult::InstalledAndChambered);
    EXPECT_EQ(installedMagazine(profile, rifle), magazine);
    EXPECT_TRUE(profile.assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(magazineRoundCount(profile, magazine), 11U);
}

TEST(WeaponAmmoDomainTest, ChamberCommandNeverFiresAnExistingRound)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-explicit-chamber",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 3},
        CommandContext{profile.revision, "explicit-load"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "explicit-install"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        ChamberWeaponCommand{rifle},
        CommandContext{profile.revision, "explicit-chamber"}).succeeded);

    const std::uint64_t before = profileStateFingerprint(profile);
    const WeaponAmmoReceipt repeated = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        ChamberWeaponCommand{rifle},
        CommandContext{profile.revision, "do-not-fire"});
    EXPECT_FALSE(repeated.succeeded);
    EXPECT_EQ(repeated.error, DomainErrorCode::InvalidQuantity);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(magazineRoundCount(profile, magazine), 2U);
}

TEST(WeaponAmmoDomainTest, InstalledMagazineCanUninstallToExactCell)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-uninstall",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "uninstall-setup"}).succeeded);
    const StoredAssetLocation destination{
        ProfileContainerId::stash(), GridPosition{18, 10}};

    const WeaponAmmoPlan plan = queryWeaponAmmo(
        profile,
        publishedContentRegistry(),
        UninstallMagazineCommand{
            rifle, destination, ItemOrientation::Degrees0});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        UninstallMagazineCommand{
            rifle, destination, ItemOrientation::Degrees0},
        CommandContext{profile.revision, "uninstall-exact"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(installedMagazine(profile, rifle).has_value());
    EXPECT_EQ(
        std::get<StoredAssetLocation>(profile.assets.find(magazine)->location),
        destination);
}
