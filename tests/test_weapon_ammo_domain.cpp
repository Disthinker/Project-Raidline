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

TEST(WeaponAmmoDomainTest, RifleAndPistolMagazinesNeverInterchange)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-caliber-family",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId pistol = firstAsset(profile, alpha_content::pistol);
    const AssetInstanceId rifleMagazine = firstAsset(
        profile, alpha_content::magazine);
    const AssetInstanceId pistolMagazine = firstAsset(
        profile, alpha_content::pistolMagazine);
    const AssetInstanceId ammunition = firstAsset(
        profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{pistolMagazine, ammunition, 3},
        CommandContext{profile.revision, "load-pistol-magazine"}).succeeded);

    const std::uint64_t before = profileStateFingerprint(profile);
    EXPECT_FALSE(queryWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, pistolMagazine}).canCommit);
    EXPECT_FALSE(queryWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{pistol, rifleMagazine}).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineAndChamberCommand{pistol, pistolMagazine},
        CommandContext{profile.revision, "install-pistol-magazine"})
        .succeeded);
    const WeaponAmmoReceipt fired = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{pistol},
        CommandContext{profile.revision, "fire-pistol"});
    ASSERT_TRUE(fired.succeeded);
    EXPECT_EQ(fired.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(profile.assets.find(pistol)->currentDurability, 9992U);
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

TEST(WeaponAmmoDomainTest, SuccessfulShotWearsWeaponWithoutReliableTierFault)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-condition-wear", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 3},
        CommandContext{profile.revision, "wear-load"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        InstallMagazineAndChamberCommand{rifle, magazine},
        CommandContext{profile.revision, "wear-install"}).succeeded);

    const WeaponAmmoReceipt fired = executeFireWeapon(
        profile, publishedContentRegistry(),
        FireWeaponCommand{rifle, 0, 0},
        CommandContext{profile.revision, "wear-fire"});

    ASSERT_TRUE(fired.succeeded);
    EXPECT_EQ(fired.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, 9990U);
    EXPECT_EQ(profile.assets.find(rifle)->weaponMalfunction,
              WeaponMalfunctionType::None);
}

TEST(WeaponAmmoDomainTest,
     NarrowFireTransactionPreservesAtomicAmmoAndRevisionRules)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-hot-fire", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 3},
        CommandContext{profile.revision, "hot-fire-load"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        InstallMagazineAndChamberCommand{rifle, magazine},
        CommandContext{profile.revision, "hot-fire-install"}).succeeded);

    const std::uint64_t beforeQuery = profileStateFingerprint(profile);
    const WeaponAmmoPlan plan = queryFireWeapon(
        profile, publishedContentRegistry(), FireWeaponCommand{rifle});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    const ProfileRevision beforeFireRevision = profile.revision;
    const WeaponAmmoReceipt fired = executeFireWeapon(
        profile, publishedContentRegistry(), FireWeaponCommand{rifle},
        CommandContext{profile.revision, "hot-fire-shot"});
    ASSERT_TRUE(fired.succeeded) << fired.message;
    EXPECT_EQ(fired.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(profile.revision, beforeFireRevision + 1U);
    EXPECT_TRUE(profile.committedTransactions.contains("hot-fire-shot"));
    EXPECT_EQ(magazineRoundCount(profile, magazine), 1U);
    EXPECT_TRUE(profile.assets.find(rifle)->chamberedRound.has_value());
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    const std::uint64_t beforeStale = profileStateFingerprint(profile);
    const WeaponAmmoReceipt stale = executeFireWeapon(
        profile, publishedContentRegistry(), FireWeaponCommand{rifle},
        CommandContext{beforeFireRevision, "hot-fire-stale"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), beforeStale);
}

TEST(WeaponAmmoDomainTest, StovepipeConsumesShotBlocksFireAndClearsByCommand)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-condition-stovepipe", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(profile, alpha_content::ammunition);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 3},
        CommandContext{profile.revision, "fault-load"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, publishedContentRegistry(),
        InstallMagazineAndChamberCommand{rifle, magazine},
        CommandContext{profile.revision, "fault-install"}).succeeded);
    profile.assets.findMutable(rifle)->currentDurability = 3000;

    const WeaponAmmoReceipt fired = executeFireWeapon(
        profile, publishedContentRegistry(),
        FireWeaponCommand{rifle, 0, 0},
        CommandContext{profile.revision, "fault-fire"});
    ASSERT_TRUE(fired.succeeded);
    EXPECT_EQ(fired.result, WeaponAmmoResult::FiredAndMalfunctioned);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, 2990U);
    EXPECT_FALSE(profile.assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(magazineRoundCount(profile, magazine), 2U);
    EXPECT_EQ(profile.assets.find(rifle)->weaponMalfunction,
              WeaponMalfunctionType::Stovepipe);

    const std::uint64_t blockedFingerprint = profileStateFingerprint(profile);
    const ProfileRevision blockedRevision = profile.revision;
    const WeaponAmmoReceipt blocked = executeFireWeapon(
        profile, publishedContentRegistry(), FireWeaponCommand{rifle},
        CommandContext{profile.revision, "blocked-fire"});
    ASSERT_TRUE(blocked.succeeded);
    EXPECT_EQ(blocked.result, WeaponAmmoResult::BlockedByMalfunction);
    EXPECT_EQ(profileStateFingerprint(profile), blockedFingerprint);
    EXPECT_EQ(profile.revision, blockedRevision);

    const WeaponAmmoReceipt cleared = executeWeaponAmmo(
        profile, publishedContentRegistry(),
        ClearWeaponMalfunctionCommand{rifle},
        CommandContext{profile.revision, "clear-fault"});
    ASSERT_TRUE(cleared.succeeded);
    EXPECT_EQ(cleared.result, WeaponAmmoResult::MalfunctionCleared);
    EXPECT_EQ(profile.assets.find(rifle)->weaponMalfunction,
              WeaponMalfunctionType::None);
    EXPECT_TRUE(profile.assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(magazineRoundCount(profile, magazine), 1U);
}

TEST(WeaponAmmoDomainTest, BrokenWeaponCannotConsumeChamberedRound)
{
    ProfileState profile = makeNewAlphaProfile(
        "weapon-condition-broken", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    profile.assets.findMutable(rifle)->currentDurability = 0;
    const std::uint64_t before = profileStateFingerprint(profile);

    const WeaponAmmoReceipt result = executeFireWeapon(
        profile, publishedContentRegistry(), FireWeaponCommand{rifle},
        CommandContext{profile.revision, "broken-fire"});

    ASSERT_TRUE(result.succeeded);
    EXPECT_EQ(result.result, WeaponAmmoResult::Broken);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(WeaponAmmoDomainTest, MixedCaliberFamilyRoundsPreserveOrderAndUnloadIdentity)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-mixed-tier", content);
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const AssetInstanceId standard = firstAsset(
        profile, alpha_content::ammunition);
    const ItemDefinitionId enhancedId{"item.ammunition.9mm_enhanced"};
    const ItemDefinition &enhancedDefinition = content.item(enhancedId);
    const auto enhancedOrigin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        enhancedDefinition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(enhancedOrigin.has_value());
    const AssetInstanceId enhanced = profile.assets.create(
        enhancedDefinition,
        StoredAssetLocation{
            ProfileContainerId::stash(), *enhancedOrigin},
        7);

    ASSERT_TRUE(executeWeaponAmmo(
        profile, content,
        LoadMagazineCommand{magazine, standard, 1},
        CommandContext{profile.revision, "mixed-load-standard"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, content,
        LoadMagazineCommand{magazine, enhanced, 7},
        CommandContext{profile.revision, "mixed-load-enhanced"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile, content,
        InstallMagazineAndChamberCommand{rifle, magazine},
        CommandContext{profile.revision, "mixed-install"}).succeeded);

    const WeaponAmmoReceipt standardShot = executeFireWeapon(
        profile, content, FireWeaponCommand{rifle},
        CommandContext{profile.revision, "mixed-fire-standard"});
    ASSERT_TRUE(standardShot.succeeded);
    EXPECT_EQ(
        standardShot.firedAmmunitionDefinitionId,
        alpha_content::ammunition);
    const WeaponAmmoReceipt enhancedShot = executeFireWeapon(
        profile, content, FireWeaponCommand{rifle},
        CommandContext{profile.revision, "mixed-fire-enhanced"});
    ASSERT_TRUE(enhancedShot.succeeded);
    EXPECT_EQ(enhancedShot.firedAmmunitionDefinitionId, enhancedId);

    ASSERT_TRUE(executeWeaponAmmo(
        profile, content,
        UnloadMagazineCommand{magazine, ProfileContainerId::stash()},
        CommandContext{profile.revision, "mixed-unload"}).succeeded);
    std::uint32_t unloadedEnhanced{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == enhancedId)
        {
            unloadedEnhanced += asset.quantity;
        }
    }
    EXPECT_EQ(unloadedEnhanced, 5U);
    EXPECT_EQ(magazineRoundCount(profile, magazine), 0U);
}

TEST(WeaponAmmoDomainTest, WrongCaliberLoadRejectsWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "weapon-ammo-wrong-caliber", content);
    const AssetInstanceId magazine = firstAsset(profile, alpha_content::magazine);
    const ItemDefinition &ammunition = content.item(
        ItemDefinitionId{"item.ammunition.5_45x39_standard"});
    const auto origin = findFirstProfileFit(
        profile, content, ProfileContainerId::stash(), ammunition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId ammunitionId = profile.assets.create(
        ammunition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        10);
    const std::uint64_t before = profileStateFingerprint(profile);

    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
        profile, content,
        LoadMagazineCommand{magazine, ammunitionId, 1},
        CommandContext{profile.revision, "wrong-caliber"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(WeaponAmmoDomainTest, EveryNewWeaponCompletesMagazineFedFireChain)
{
    const ContentRegistry &content = publishedContentRegistry();
    const std::array<ItemDefinitionId, 4> weapons{
        ItemDefinitionId{"item.weapon.carbine_5_45_compact"},
        ItemDefinitionId{"item.weapon.rifle_5_45_service"},
        ItemDefinitionId{"item.weapon.dmr_7_62x51_service"},
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}};

    for (const ItemDefinitionId &weaponId : weapons)
    {
        ProfileState profile = makeNewAlphaProfile(
            std::string{"weapon-chain-"} + std::string{weaponId.value()},
            content);
        const ItemDefinition &weaponDefinition = content.item(weaponId);
        ASSERT_TRUE(weaponDefinition.compatibleAmmunitionDefinitionId.has_value());
        ASSERT_FALSE(weaponDefinition.compatibleMagazineDefinitionIds.empty());
        const ItemDefinition &magazineDefinition = content.item(
            weaponDefinition.compatibleMagazineDefinitionIds.front());
        const ItemDefinition &ammunitionDefinition = content.item(
            *weaponDefinition.compatibleAmmunitionDefinitionId);
        const auto createStored =
            [&profile, &content](
                const ItemDefinition &definition,
                std::uint32_t quantity)
            {
                const auto origin = findFirstProfileFit(
                    profile, content, ProfileContainerId::stash(),
                    definition, ItemOrientation::Degrees0);
                EXPECT_TRUE(origin.has_value());
                return profile.assets.create(
                    definition,
                    StoredAssetLocation{
                        ProfileContainerId::stash(), *origin},
                    quantity);
            };
        const AssetInstanceId weapon = createStored(weaponDefinition, 1);
        const AssetInstanceId magazine = createStored(magazineDefinition, 1);
        const AssetInstanceId ammunition = createStored(ammunitionDefinition, 3);

        ASSERT_TRUE(executeWeaponAmmo(
            profile, content,
            LoadMagazineCommand{magazine, ammunition, 3},
            CommandContext{profile.revision, "chain-load"}).succeeded)
            << weaponId.value();
        ASSERT_TRUE(executeWeaponAmmo(
            profile, content,
            InstallMagazineAndChamberCommand{weapon, magazine},
            CommandContext{profile.revision, "chain-install"}).succeeded)
            << weaponId.value();
        const WeaponAmmoReceipt fired = executeFireWeapon(
            profile, content, FireWeaponCommand{weapon},
            CommandContext{profile.revision, "chain-fire"});
        ASSERT_TRUE(fired.succeeded) << weaponId.value();
        EXPECT_TRUE(
            fired.result == WeaponAmmoResult::Fired ||
            fired.result == WeaponAmmoResult::FiredAndMalfunctioned)
            << weaponId.value();
        EXPECT_EQ(
            fired.firedAmmunitionDefinitionId,
            weaponDefinition.compatibleAmmunitionDefinitionId)
            << weaponId.value();
    }
}
