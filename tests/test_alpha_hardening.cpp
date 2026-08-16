#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "game_session.h"
#include "raid_lifecycle.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"

namespace
{
class TemporarySaveDirectory
{
public:
    TemporarySaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-alpha-hardening-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))}
    {
    }

    ~TemporarySaveDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<AssetInstanceId> assets(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            result.push_back(id);
        }
    }
    return result;
}

std::uint64_t ammunitionCount(const ProfileState &profile)
{
    std::uint64_t result{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == alpha_content::ammunition)
        {
            result += asset.quantity;
        }
        result += asset.magazineRounds.size();
        result += asset.chamberedRound.has_value() ? 1U : 0U;
    }
    return result;
}

void equipIfMissing(
    ProfileState &profile,
    const ItemDefinitionId &definitionId,
    EquipmentSlotKind slot,
    std::string transactionId)
{
    if (equippedAsset(profile, slot).has_value())
    {
        return;
    }
    const auto candidates = assets(profile, definitionId);
    ASSERT_FALSE(candidates.empty());
    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{candidates.front(), slot},
        CommandContext{profile.revision, std::move(transactionId)});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}

void prepareRaid(ProfileState &profile, int cycle)
{
    const std::string suffix = std::to_string(cycle);
    if (!hasMinimumRaidCapability(profile, publishedContentRegistry()))
    {
        ASSERT_TRUE(isReliefEligible(profile, publishedContentRegistry()));
        const EconomyReceipt relief = executeEconomy(
            profile,
            publishedContentRegistry(),
            ClaimReliefCommand{"hardening-relief-" + suffix},
            CommandContext{
                profile.revision,
                "hardening-claim-relief-" + suffix});
        ASSERT_TRUE(relief.succeeded) << relief.message;
    }

    ASSERT_TRUE(hasMinimumRaidCapability(
        profile,
        publishedContentRegistry()));
    equipIfMissing(
        profile,
        alpha_content::rifle,
        EquipmentSlotKind::PrimaryWeapon,
        "hardening-equip-rifle-" + suffix);
    equipIfMissing(
        profile,
        alpha_content::chestRig,
        EquipmentSlotKind::ChestRig,
        "hardening-equip-chest-" + suffix);
    if (!equippedAsset(profile, EquipmentSlotKind::Backpack).has_value())
    {
        const auto backpacks = assets(profile, alpha_content::backpack);
        if (!backpacks.empty())
        {
            equipIfMissing(
                profile,
                alpha_content::backpack,
                EquipmentSlotKind::Backpack,
                "hardening-equip-backpack-" + suffix);
        }
    }

    const AssetInstanceId rifle = *equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon);
    if (!installedMagazine(profile, rifle).has_value())
    {
        const auto magazines = assets(profile, alpha_content::magazine);
        const auto found = std::find_if(
            magazines.begin(),
            magazines.end(),
            [&profile](AssetInstanceId id)
            {
                return !std::holds_alternative<InstalledMagazineLocation>(
                    profile.assets.find(id)->location);
            });
        ASSERT_NE(found, magazines.end());
        const AssetInstanceId magazine = *found;
        if (magazineRoundCount(profile, magazine) == 0)
        {
            const auto ammunition = assets(profile, alpha_content::ammunition);
            ASSERT_FALSE(ammunition.empty());
            const WeaponAmmoReceipt loaded = executeWeaponAmmo(
                profile,
                publishedContentRegistry(),
                LoadMagazineCommand{magazine, ammunition.front(), 30},
                CommandContext{
                    profile.revision,
                    "hardening-load-magazine-" + suffix});
            ASSERT_TRUE(loaded.succeeded) << loaded.message;
        }
        const WeaponAmmoReceipt installed = executeWeaponAmmo(
            profile,
            publishedContentRegistry(),
            InstallMagazineCommand{rifle, magazine},
            CommandContext{
                profile.revision,
                "hardening-install-magazine-" + suffix});
        ASSERT_TRUE(installed.succeeded) << installed.message;
    }
}

void consumeOneRound(ProfileState &profile, int cycle)
{
    const AssetInstanceId rifle = *equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon);
    const std::uint64_t before = ammunitionCount(profile);
    WeaponAmmoReceipt fired = executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{
            profile.revision,
            "hardening-fire-a-" + std::to_string(cycle)});
    ASSERT_TRUE(fired.succeeded) << fired.message;
    if (fired.result == WeaponAmmoResult::Chambered)
    {
        fired = executeWeaponAmmo(
            profile,
            publishedContentRegistry(),
            FireWeaponCommand{rifle},
            CommandContext{
                profile.revision,
                "hardening-fire-b-" + std::to_string(cycle)});
        ASSERT_TRUE(fired.succeeded) << fired.message;
    }
    ASSERT_EQ(fired.result, WeaponAmmoResult::Fired);
    EXPECT_EQ(ammunitionCount(profile) + 1U, before);
}

void expectValid(const ProfileState &profile)
{
    const ProfileValidationResult validation = validateProfileState(
        profile,
        publishedContentRegistry());
    ASSERT_TRUE(validation.valid) << validation.message;
}
}

TEST(AlphaHardeningTest, TenMixedRaidsSurviveRepeatedProcessReloads)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "alpha-hardening-sequence",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    std::size_t reloadCount{};
    AssetInstanceId previousHighWater = profile.assets.nextAssetId();
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        prepareRaid(profile, cycle);
        if (cycle % 2 == 0)
        {
            consumeOneRound(profile, cycle);
        }
        const std::uint64_t preRaidFingerprint =
            profileStateFingerprint(profile);
        ASSERT_TRUE(repository.save(
            profile,
            publishedContentRegistry().contentVersion()).succeeded);

        const std::string suffix = std::to_string(cycle);
        const std::string settlementId =
            "hardening-settlement-" + suffix;
        const DeployReceipt deployed = executeDeploy(
            profile,
            publishedContentRegistry(),
            DeployCommand{
                "hardening-raid-" + suffix,
                settlementId,
                static_cast<std::uint64_t>(1001 + cycle),
                MapDefinitionId{"map.v0.test"}},
            CommandContext{
                profile.revision,
                "hardening-deploy-" + suffix});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        expectValid(profile);

        RaidResultOutcome outcome = RaidResultOutcome::Extracted;
        if (cycle % 2 != 0)
        {
            switch (cycle % 6)
            {
            case 1:
                outcome = RaidResultOutcome::PlayerDead;
                break;
            case 3:
                outcome = RaidResultOutcome::ActiveQuit;
                break;
            default:
                outcome = RaidResultOutcome::AbnormalQuit;
                break;
            }
        }

        if (outcome == RaidResultOutcome::AbnormalQuit)
        {
            const SaveLoadResult reopened = repository.load(
                publishedContentRegistry());
            ASSERT_TRUE(reopened.profile.has_value()) << reopened.message;
            profile = *reopened.profile;
            EXPECT_EQ(profileStateFingerprint(profile), preRaidFingerprint);
            EXPECT_FALSE(profile.pendingRaid.has_value());
            expectValid(profile);
            ++reloadCount;
            continue;
        }
        else if (outcome == RaidResultOutcome::Extracted)
        {
            for (const RaidLootSnapshot &loot : profile.pendingRaid->loot)
            {
                const InventoryReceipt pickedUp = pickupRaidLoot(
                    profile,
                    publishedContentRegistry(),
                    loot.assetId,
                    CommandContext{
                        profile.revision,
                        "hardening-pickup-" + suffix + "-" +
                            std::to_string(loot.slotIndex)});
                if (pickedUp.succeeded)
                {
                    break;
                }
            }
        }

        const RaidSettlementReceipt settled = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            settlementId,
            outcome);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        const std::uint64_t settledFingerprint =
            profileStateFingerprint(profile);
        const RaidSettlementReceipt repeated = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            settlementId,
            outcome);
        ASSERT_TRUE(repeated.succeeded);
        EXPECT_TRUE(repeated.alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(profile), settledFingerprint);
        expectValid(profile);
        EXPECT_EQ(profile.currency, 200U);
        EXPECT_GE(profile.assets.nextAssetId(), previousHighWater);
        previousHighWater = profile.assets.nextAssetId();

        ASSERT_TRUE(repository.save(
            profile,
            publishedContentRegistry().contentVersion()).succeeded);
        if (cycle == 2 || cycle == 5 || cycle == 8)
        {
            const SaveLoadResult reopened = repository.load(
                publishedContentRegistry());
            ASSERT_TRUE(reopened.profile.has_value()) << reopened.message;
            EXPECT_EQ(
                profileStateFingerprint(*reopened.profile),
                profileStateFingerprint(profile));
            profile = *reopened.profile;
            ++reloadCount;
        }
    }

    EXPECT_GE(reloadCount, 3U);
    EXPECT_EQ(profile.committedSettlements.size(), 9U);
    expectValid(profile);
}

TEST(AlphaHardeningTest, StableSeedsReachAllConfigurationsAndEveryRoute)
{
    const MapDefinition &map = defaultV0MapDefinition();
    std::set<std::string> pairIds;
    std::set<EnemyDeploymentDefinitionId> deploymentIds;

    for (std::uint64_t seed = 1;
         seed <= 128 && (pairIds.size() < 3 || deploymentIds.size() < 3);
         ++seed)
    {
        ProfileState profile = makeNewAlphaProfile(
            "alpha-configuration-" + std::to_string(seed),
            publishedContentRegistry());
        const DeployReceipt deployed = executeDeploy(
            profile,
            publishedContentRegistry(),
            DeployCommand{
                "configuration-raid-" + std::to_string(seed),
                "configuration-settlement-" + std::to_string(seed),
                seed,
                map.id},
            CommandContext{
                profile.revision,
                "configuration-deploy-" + std::to_string(seed)});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(profile.pendingRaid.has_value());
        pairIds.insert(profile.pendingRaid->spawnExtractionPairId);
        deploymentIds.insert(profile.pendingRaid->enemyDeploymentId);

        std::set<std::string> routes;
        for (const RaidLootSnapshot &loot : profile.pendingRaid->loot)
        {
            ASSERT_LT(loot.slotIndex, map.raidLootSlots.size());
            routes.insert(map.raidLootSlots[loot.slotIndex].route);
        }
        EXPECT_TRUE(routes.contains("central"));
        EXPECT_TRUE(routes.contains("perimeter"));
        EXPECT_TRUE(routes.contains("resource"));

        const std::uint64_t fingerprint = profileStateFingerprint(profile);
        const SaveLoadResult roundTrip = deserializeProfileEnvelope(
            serializeProfileEnvelope(
                profile,
                publishedContentRegistry().contentVersion()),
            publishedContentRegistry());
        ASSERT_TRUE(roundTrip.profile.has_value()) << roundTrip.message;
        EXPECT_EQ(profileStateFingerprint(*roundTrip.profile), fingerprint);
    }

    EXPECT_EQ(pairIds.size(), 3U);
    EXPECT_EQ(deploymentIds.size(), 3U);
}

TEST(AlphaHardeningTest, DeploySaveFailureDoesNotEnterRaidOrMutateProfile)
{
    TemporarySaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("alpha-save-failure"));
    const std::uint64_t before = profileStateFingerprint(session.profile());

    std::error_code error;
    std::filesystem::remove_all(temporary.path(), error);
    ASSERT_FALSE(error);
    std::ofstream blockingFile(temporary.path());
    blockingFile << "not a directory";
    blockingFile.close();

    EXPECT_FALSE(session.deployAlpha(99173));
    EXPECT_FALSE(session.alphaRaidActive());
    EXPECT_FALSE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}
