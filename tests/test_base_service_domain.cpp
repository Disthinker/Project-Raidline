#include <gtest/gtest.h>

#include <limits>

#include "alpha_content_ids.h"
#include "base_service_domain.h"
#include "world_clock.h"

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
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
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
}

TEST(BaseServiceDomainTest, QuotesAndStartsOneAtomicGunsmithJob)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
    const ProfileRevision revisionBefore = profile.revision;

    const GunsmithMaintenancePlan plan = queryGunsmithMaintenance(
        profile, content, StartGunsmithMaintenanceCommand{rifleId});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.quotedCurrency, 130U);
    EXPECT_EQ(plan.durationMinutes, 240U);
    EXPECT_EQ(plan.currentDurabilityBeforeCenti, 5000U);
    EXPECT_EQ(plan.currentMaximumBeforeCenti, 8000U);
    EXPECT_EQ(plan.targetFactoryDurabilityCenti, 10000U);

    const GunsmithMaintenanceReceipt receipt = executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{revisionBefore, "gunsmith-start-1"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.idempotent);
    EXPECT_EQ(profile.currency, 870U);
    EXPECT_EQ(profile.revision, revisionBefore + 1U);
    ASSERT_TRUE(profile.gunsmithMaintenanceJob.has_value());
    EXPECT_EQ(receipt.jobId, profile.gunsmithMaintenanceJob->jobId);
    EXPECT_EQ(profile.nextBaseServiceJobId, receipt.jobId + 1U);
    EXPECT_EQ(
        profile.assets.find(rifleId)->location,
        AssetLocation{BaseServiceAssetLocation{receipt.jobId}});
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseServiceDomainTest, RejectionsPreserveFingerprintAndHighWaterMarks)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
    profile.currency = 1;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const AssetInstanceId nextAssetId = profile.assets.nextAssetId();
    const BaseServiceJobId nextJobId = profile.nextBaseServiceJobId;

    const GunsmithMaintenanceReceipt receipt = executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{profile.revision, "gunsmith-insufficient"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), nextAssetId);
    EXPECT_EQ(profile.nextBaseServiceJobId, nextJobId);
    EXPECT_FALSE(profile.gunsmithMaintenanceJob.has_value());
}

TEST(BaseServiceDomainTest, ServiceRequiresStashRootAndOneActiveJob)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState equipped = damagedRifleProfile();
    const AssetInstanceId equippedRifle = firstStashAsset(
        equipped, alpha_content::rifle);
    ASSERT_TRUE(executeInventory(
        equipped,
        content,
        InventoryEquipCommand{
            equippedRifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{equipped.revision, "equip-before-service"}).succeeded);
    const std::uint64_t equippedFingerprint =
        profileStateFingerprint(equipped);
    const GunsmithMaintenanceReceipt equippedReceipt =
        executeGunsmithMaintenance(
            equipped,
            content,
            StartGunsmithMaintenanceCommand{equippedRifle},
            CommandContext{equipped.revision, "reject-equipped-service"});
    EXPECT_FALSE(equippedReceipt.succeeded);
    EXPECT_EQ(
        profileStateFingerprint(equipped), equippedFingerprint);

    ProfileState active = damagedRifleProfile();
    const AssetInstanceId rifle = firstStashAsset(
        active, alpha_content::rifle);
    ASSERT_TRUE(executeGunsmithMaintenance(
        active,
        content,
        StartGunsmithMaintenanceCommand{rifle},
        CommandContext{active.revision, "start-only-service-slot"}).succeeded);
    const AssetInstanceId pistol = firstStashAsset(
        active, alpha_content::pistol);
    ASSERT_NE(pistol, 0U);
    AssetRecord *damagedPistol = active.assets.findMutable(pistol);
    damagedPistol->currentDurability -= 100U;
    const std::uint64_t activeFingerprint = profileStateFingerprint(active);
    const GunsmithMaintenanceReceipt secondReceipt =
        executeGunsmithMaintenance(
            active,
            content,
            StartGunsmithMaintenanceCommand{pistol},
            CommandContext{active.revision, "reject-second-service-slot"});
    EXPECT_FALSE(secondReceipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(active), activeFingerprint);
}

TEST(BaseServiceDomainTest, TimelineOverflowAndTransactionReplayAreSafe)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState overflow = damagedRifleProfile();
    const AssetInstanceId overflowRifle = firstStashAsset(
        overflow, alpha_content::rifle);
    overflow.worldClock.elapsedWorldMinutes =
        std::numeric_limits<std::uint64_t>::max() - 100U;
    const GunsmithMaintenancePlan overflowPlan = queryGunsmithMaintenance(
        overflow,
        content,
        StartGunsmithMaintenanceCommand{overflowRifle});
    EXPECT_FALSE(overflowPlan.canCommit);
    EXPECT_EQ(overflowPlan.error, DomainErrorCode::RevisionOverflow);

    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifle = firstStashAsset(
        profile, alpha_content::rifle);
    const CommandContext context{
        profile.revision, "idempotent-gunsmith-start"};
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

TEST(BaseServiceDomainTest, CollectionWaitsForWorldTimeAndRestoresSameWeapon)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
    const AssetInstanceId magazineId = installedMagazine(profile, rifleId).value();
    const GridPosition original = std::get<StoredAssetLocation>(
        profile.assets.find(rifleId)->location).origin;
    ASSERT_TRUE(executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{profile.revision, "gunsmith-start-collect"}).succeeded);

    const GunsmithCollectionPlan waiting = queryGunsmithCollection(
        profile, content);
    EXPECT_FALSE(waiting.canCommit);
    EXPECT_EQ(waiting.minutesRemaining, 240U);
    static_cast<void>(advanceWorldClock(profile.worldClock, 240U));

    const GunsmithCollectionPlan ready = queryGunsmithCollection(
        profile, content);
    ASSERT_TRUE(ready.canCommit) << ready.message;
    EXPECT_EQ(ready.destination.origin, original);
    const ProfileRevision revisionBefore = profile.revision;
    const GunsmithCollectionReceipt receipt = executeGunsmithCollection(
        profile,
        content,
        CommandContext{revisionBefore, "gunsmith-collect-1"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.weaponAssetId, rifleId);
    EXPECT_EQ(receipt.restoredCurrentDurabilityCenti, 5000U);
    EXPECT_EQ(receipt.restoredMaximumDurabilityCenti, 2000U);
    EXPECT_TRUE(receipt.clearedMalfunction);
    EXPECT_FALSE(profile.gunsmithMaintenanceJob.has_value());
    const AssetRecord *rifle = profile.assets.find(rifleId);
    ASSERT_NE(rifle, nullptr);
    EXPECT_EQ(rifle->currentDurability, 10000U);
    EXPECT_EQ(rifle->currentMaximumDurability, 10000U);
    EXPECT_EQ(rifle->weaponMalfunction, WeaponMalfunctionType::None);
    EXPECT_TRUE(rifle->chamberedRound.has_value());
    EXPECT_EQ(installedMagazine(profile, rifleId), magazineId);
    EXPECT_EQ(profile.assets.find(magazineId)->magazineRounds.size(), 1U);
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const std::uint64_t collectedFingerprint =
        profileStateFingerprint(profile);
    const GunsmithCollectionReceipt replay = executeGunsmithCollection(
        profile,
        content,
        CommandContext{revisionBefore, "gunsmith-collect-1"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), collectedFingerprint);
}

TEST(BaseServiceDomainTest, CollectionUsesAnotherLegalStashPosition)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
    const GridPosition original = std::get<StoredAssetLocation>(
        profile.assets.find(rifleId)->location).origin;
    ASSERT_TRUE(executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{profile.revision, "gunsmith-start-fallback"}).succeeded);
    static_cast<void>(advanceWorldClock(profile.worldClock, 240U));

    const ItemDefinition &ammo = content.item(alpha_content::ammunition);
    static_cast<void>(profile.assets.create(
        ammo,
        StoredAssetLocation{ProfileContainerId::stash(), original},
        1));
    const GunsmithCollectionPlan ready = queryGunsmithCollection(
        profile, content);
    ASSERT_TRUE(ready.canCommit) << ready.message;
    EXPECT_NE(ready.destination.origin, original);
    EXPECT_TRUE(executeGunsmithCollection(
        profile,
        content,
        CommandContext{profile.revision, "gunsmith-collect-fallback"}).succeeded);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseServiceDomainTest, FullStashKeepsReadyWeaponInService)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = damagedRifleProfile();
    const AssetInstanceId rifleId = firstStashAsset(profile, alpha_content::rifle);
    ASSERT_TRUE(executeGunsmithMaintenance(
        profile,
        content,
        StartGunsmithMaintenanceCommand{rifleId},
        CommandContext{profile.revision, "gunsmith-start-blocked"}).succeeded);
    static_cast<void>(advanceWorldClock(profile.worldClock, 240U));

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
    for (int y = 0; y < 12; ++y)
    {
        for (int x = 0; x < 20; ++x)
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
        CommandContext{profile.revision, "gunsmith-collect-blocked"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_TRUE(profile.gunsmithMaintenanceJob.has_value());
    EXPECT_TRUE(std::holds_alternative<BaseServiceAssetLocation>(
        profile.assets.find(rifleId)->location));
}
