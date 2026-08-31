#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "alpha_content_ids.h"
#include "base_service_domain.h"

namespace
{
AssetInstanceId firstStashAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (asset.definitionId == definitionId && stored != nullptr &&
            stored->container == ProfileContainerId::stash())
        {
            return id;
        }
    }
    return 0;
}

ProfileState damagedRifleProfile()
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("gunsmith-domain", content);
    profile.currency = 1000;
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    AssetRecord *rifle = profile.assets.findMutable(rifleId);
    rifle->currentMaximumDurability = 8000;
    rifle->currentDurability = 5000;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    rifle->chamberedRound = MagazineRoundRecord{alpha_content::ammunition};

    const AssetInstanceId magazineId = firstStashAsset(
        profile, alpha_content::magazine);
    AssetRecord *magazine = profile.assets.findMutable(magazineId);
    magazine->location = InstalledMagazineLocation{rifleId};
    magazine->magazineRounds.push_back(
        MagazineRoundRecord{alpha_content::ammunition});
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    return profile;
}

void moveWeaponIntoLegacyJob(
    ProfileState &profile,
    AssetInstanceId weaponAssetId)
{
    AssetRecord *weapon = profile.assets.findMutable(weaponAssetId);
    ASSERT_NE(weapon, nullptr);
    const StoredAssetLocation returnLocation =
        std::get<StoredAssetLocation>(weapon->location);
    const BaseServiceJobId jobId = profile.nextBaseServiceJobId++;
    weapon->location = BaseServiceAssetLocation{jobId};
    profile.gunsmithMaintenanceJob = GunsmithMaintenanceJob{
        jobId,
        weaponAssetId,
        returnLocation.origin,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 240U,
        130U,
        10000U};
}
}

TEST(BaseServiceDomainTest, QuoteAndMaintenanceCommitImmediately)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    const AssetInstanceId magazineId = installedMagazine(
        profile, rifleId).value();
    const AssetLocation originalLocation =
        profile.assets.find(rifleId)->location;
    const BaseServiceJobId nextJobId = profile.nextBaseServiceJobId;
    const std::uint64_t worldMinute =
        profile.worldClock.elapsedWorldMinutes;
    const ProfileRevision revisionBefore = profile.revision;

    const GunsmithMaintenancePlan plan = queryGunsmithMaintenance(
        profile, content, StartGunsmithMaintenanceCommand{rifleId});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.quotedCurrency, 130U);
    EXPECT_EQ(plan.currentDurabilityBeforeCenti, 5000U);
    EXPECT_EQ(plan.currentMaximumBeforeCenti, 8000U);
    EXPECT_EQ(plan.targetFactoryDurabilityCenti, 10000U);

    const GunsmithMaintenanceReceipt receipt = executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{revisionBefore, "gunsmith-instant-1"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.idempotent);
    EXPECT_EQ(receipt.currencyPaid, 130U);
    EXPECT_EQ(receipt.restoredCurrentDurabilityCenti, 5000U);
    EXPECT_EQ(receipt.restoredMaximumDurabilityCenti, 2000U);
    EXPECT_TRUE(receipt.clearedMalfunction);
    EXPECT_EQ(profile.currency, 870U);
    EXPECT_EQ(profile.revision, revisionBefore + 1U);
    EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, worldMinute);
    EXPECT_EQ(profile.nextBaseServiceJobId, nextJobId);
    EXPECT_FALSE(profile.gunsmithMaintenanceJob.has_value());

    const AssetRecord *rifle = profile.assets.find(rifleId);
    ASSERT_NE(rifle, nullptr);
    EXPECT_EQ(rifle->location, originalLocation);
    EXPECT_EQ(rifle->currentDurability, 10000U);
    EXPECT_EQ(rifle->currentMaximumDurability, 10000U);
    EXPECT_EQ(rifle->weaponMalfunction, WeaponMalfunctionType::None);
    EXPECT_TRUE(rifle->chamberedRound.has_value());
    EXPECT_EQ(installedMagazine(profile, rifleId), magazineId);
    EXPECT_EQ(profile.assets.find(magazineId)->magazineRounds.size(), 1U);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseServiceDomainTest, OperationalReadinessDoesNotChangeQuoteOrTime)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState critical = damagedRifleProfile();
    critical.baseResources.pool = BaseResourceBundle{0, 100, 100, 100};
    const AssetInstanceId criticalRifle = firstStashAsset(
        critical, alpha_content::rifle);
    const GunsmithMaintenancePlan criticalPlan = queryGunsmithMaintenance(
        critical,
        content,
        StartGunsmithMaintenanceCommand{criticalRifle});
    ASSERT_TRUE(criticalPlan.canCommit) << criticalPlan.message;

    ProfileState supported = damagedRifleProfile();
    supported.baseResources.pool = BaseResourceBundle{100, 100, 100, 100};
    const AssetInstanceId supportedRifle = firstStashAsset(
        supported, alpha_content::rifle);
    const GunsmithMaintenancePlan supportedPlan = queryGunsmithMaintenance(
        supported,
        content,
        StartGunsmithMaintenanceCommand{supportedRifle});
    ASSERT_TRUE(supportedPlan.canCommit) << supportedPlan.message;
    EXPECT_EQ(criticalPlan.quotedCurrency, supportedPlan.quotedCurrency);

    const std::uint64_t beforeMinute =
        critical.worldClock.elapsedWorldMinutes;
    ASSERT_TRUE(executeGunsmithMaintenance(
        critical,
        content,
        StartGunsmithMaintenanceCommand{criticalRifle},
        CommandContext{critical.revision, "critical-instant-service"})
                    .succeeded);
    EXPECT_EQ(critical.worldClock.elapsedWorldMinutes, beforeMinute);
    EXPECT_FALSE(critical.gunsmithMaintenanceJob.has_value());
}

TEST(BaseServiceDomainTest, RejectionsPreserveEveryParticipant)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState insufficient = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        insufficient, alpha_content::rifle);
    insufficient.currency = 1;
    const std::uint64_t fingerprint = profileStateFingerprint(insufficient);
    const AssetInstanceId nextAssetId = insufficient.assets.nextAssetId();
    const BaseServiceJobId nextJobId = insufficient.nextBaseServiceJobId;
    EXPECT_FALSE(executeGunsmithMaintenance(
        insufficient,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{insufficient.revision, "gunsmith-insufficient"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(insufficient), fingerprint);
    EXPECT_EQ(insufficient.assets.nextAssetId(), nextAssetId);
    EXPECT_EQ(insufficient.nextBaseServiceJobId, nextJobId);

    ProfileState equipped = damagedRifleProfile();
    const AssetInstanceId equippedRifle = firstStashAsset(
        equipped, alpha_content::rifle);
    ASSERT_TRUE(executeInventory(
        equipped,
        content,
        InventoryEquipCommand{
            equippedRifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{equipped.revision, "equip-before-service"})
                    .succeeded);
    const std::uint64_t equippedFingerprint =
        profileStateFingerprint(equipped);
    EXPECT_FALSE(executeGunsmithMaintenance(
        equipped,
        content,
        StartGunsmithMaintenanceCommand{equippedRifle},
        CommandContext{equipped.revision, "reject-equipped-service"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(equipped), equippedFingerprint);
}

TEST(BaseServiceDomainTest, RevisionOverflowAndTransactionReplayAreSafe)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState overflow = damagedRifleProfile();
    const AssetInstanceId overflowRifle = firstStashAsset(
        overflow, alpha_content::rifle);
    overflow.revision = std::numeric_limits<ProfileRevision>::max();
    const std::uint64_t overflowFingerprint =
        profileStateFingerprint(overflow);
    EXPECT_FALSE(executeGunsmithMaintenance(
        overflow,
        content,
        StartGunsmithMaintenanceCommand{overflowRifle},
        CommandContext{overflow.revision, "overflow-instant-service"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(overflow), overflowFingerprint);

    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifle = firstStashAsset(
        profile, alpha_content::rifle);
    const CommandContext context{
        profile.revision, "idempotent-gunsmith-service"};
    ASSERT_TRUE(executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifle},
        context).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const GunsmithMaintenanceReceipt replay = executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifle},
        context);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseServiceDomainTest, LegacyTimedJobIsCollectibleImmediately)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    const AssetInstanceId magazineId = installedMagazine(
        profile, rifleId).value();
    const GridPosition original = std::get<StoredAssetLocation>(
        profile.assets.find(rifleId)->location).origin;
    moveWeaponIntoLegacyJob(profile, rifleId);
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const GunsmithCollectionPlan ready = queryGunsmithCollection(
        profile, content);
    ASSERT_TRUE(ready.canCommit) << ready.message;
    EXPECT_EQ(ready.destination.origin, original);
    const ProfileRevision revisionBefore = profile.revision;
    const GunsmithCollectionReceipt receipt = executeGunsmithCollection(
        profile,
        content,
        CommandContext{revisionBefore, "collect-legacy-gunsmith"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.weaponAssetId, rifleId);
    EXPECT_FALSE(profile.gunsmithMaintenanceJob.has_value());
    const AssetRecord *rifle = profile.assets.find(rifleId);
    ASSERT_NE(rifle, nullptr);
    EXPECT_EQ(rifle->currentDurability, 10000U);
    EXPECT_EQ(rifle->currentMaximumDurability, 10000U);
    EXPECT_EQ(rifle->weaponMalfunction, WeaponMalfunctionType::None);
    EXPECT_EQ(installedMagazine(profile, rifleId), magazineId);
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const std::uint64_t collectedFingerprint =
        profileStateFingerprint(profile);
    const GunsmithCollectionReceipt replay = executeGunsmithCollection(
        profile,
        content,
        CommandContext{revisionBefore, "collect-legacy-gunsmith"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), collectedFingerprint);
}

TEST(BaseServiceDomainTest, LegacyHeldJobBlocksAnotherMaintenance)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    moveWeaponIntoLegacyJob(profile, rifleId);

    const AssetInstanceId pistolId = firstStashAsset(
        profile, alpha_content::pistol);
    ASSERT_NE(pistolId, 0U);
    AssetRecord *pistol = profile.assets.findMutable(pistolId);
    ASSERT_GT(pistol->currentDurability, 100U);
    pistol->currentDurability -= 100U;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const GunsmithMaintenanceReceipt receipt = executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{pistolId},
        CommandContext{profile.revision, "reject-while-legacy-held"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseServiceDomainTest, LegacyCollectionUsesAnotherLegalStashPosition)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    const GridPosition original = std::get<StoredAssetLocation>(
        profile.assets.find(rifleId)->location).origin;
    moveWeaponIntoLegacyJob(profile, rifleId);

    const ItemDefinition &ammo = content.item(alpha_content::ammunition);
    static_cast<void>(profile.assets.create(
        ammo,
        StoredAssetLocation{ProfileContainerId::stash(), original},
        1));
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const GunsmithCollectionPlan plan = queryGunsmithCollection(
        profile, content);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_NE(plan.destination.origin, original);
    const GunsmithCollectionReceipt receipt = executeGunsmithCollection(
        profile,
        content,
        CommandContext{profile.revision, "collect-legacy-fallback"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto *stored = std::get_if<StoredAssetLocation>(
        &profile.assets.find(rifleId)->location);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->origin, plan.destination.origin);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseServiceDomainTest, FullStashPreservesLegacyHeldWeapon)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(
        profile, alpha_content::rifle);
    moveWeaponIntoLegacyJob(profile, rifleId);

    std::vector<AssetInstanceId> storedIds;
    for (const AssetRecord *asset : assetsInContainer(
             profile, ProfileContainerId::stash()))
    {
        storedIds.push_back(asset->instanceId);
    }
    for (AssetInstanceId id : storedIds)
    {
        ASSERT_TRUE(profile.assets.erase(id));
    }
    const ItemDefinition &ammo = content.item(alpha_content::ammunition);
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 24; ++x)
        {
            static_cast<void>(profile.assets.create(
                ammo,
                StoredAssetLocation{
                    ProfileContainerId::stash(), GridPosition{x, y}},
                1));
        }
    }
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const GunsmithCollectionReceipt receipt = executeGunsmithCollection(
        profile,
        content,
        CommandContext{profile.revision, "collect-full-legacy"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_TRUE(profile.gunsmithMaintenanceJob.has_value());
    EXPECT_TRUE(std::holds_alternative<BaseServiceAssetLocation>(
        profile.assets.find(rifleId)->location));
}
