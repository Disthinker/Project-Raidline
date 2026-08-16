#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <tuple>

#include "alpha_content_ids.h"
#include "game_session.h"

namespace
{
class TemporarySaveDirectory
{
public:
    TemporarySaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-alpha-session-" + std::to_string(
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

void equip(GameSession &session, AssetInstanceId id, EquipmentSlotKind slot,
           std::string transaction)
{
    const InventoryReceipt receipt = session.executeProfileInventory(
        InventoryEquipCommand{id, slot},
        std::move(transaction));
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}

void prepareArmedLoadout(GameSession &session)
{
    const auto rifles = assets(session.profile(), alpha_content::rifle);
    const auto chests = assets(session.profile(), alpha_content::chestRig);
    const auto backpacks = assets(session.profile(), alpha_content::backpack);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto ammunition = assets(session.profile(), alpha_content::ammunition);
    ASSERT_EQ(rifles.size(), 1U);
    ASSERT_EQ(chests.size(), 1U);
    ASSERT_EQ(backpacks.size(), 1U);
    ASSERT_GE(magazines.size(), 2U);
    ASSERT_GE(ammunition.size(), 2U);

    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazines[0], ammunition[0], 10},
        "alpha-load-ten").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazines[1], ammunition[1], 20},
        "alpha-load-twenty").succeeded);
    equip(session, rifles[0], EquipmentSlotKind::PrimaryWeapon,
          "alpha-equip-rifle");
    equip(session, chests[0], EquipmentSlotKind::ChestRig,
          "alpha-equip-chest");
    equip(session, backpacks[0], EquipmentSlotKind::Backpack,
          "alpha-equip-backpack");
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            magazines[0], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests[0], 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "alpha-pocket-mag-0").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            magazines[1], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests[0], 1),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "alpha-pocket-mag-1").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        InstallMagazineCommand{rifles[0], magazines[0]},
        "alpha-install-mag").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        FireWeaponCommand{rifles[0]},
        "alpha-chamber-round").succeeded);
}

std::uint32_t carriedLooseAmmunition(const ProfileState &profile)
{
    std::uint32_t total{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::ammunition &&
            assetIsCarried(profile, id))
        {
            total += asset.quantity;
        }
    }
    return total;
}
}

TEST(AlphaExtractionSessionTest, DeployUsesSnapshotAndRealShotConsumption)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-fire"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId magazine = *installedMagazine(
        session.profile(), rifle);
    const std::size_t roundsBefore = magazineRoundCount(
        session.profile(), magazine);

    ASSERT_TRUE(session.deployAlpha(90817));
    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(session.world().player().maxHealth(), 100);
    EXPECT_EQ(session.world().player().health(), 100);
    EXPECT_FLOAT_EQ(session.world().raidSession().raidTimeRemaining(), 0.0F);
    EXPECT_GE(session.profile().pendingRaid->loot.size(), 6U);
    EXPECT_LE(session.profile().pendingRaid->loot.size(), 9U);

    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    session.update(fire, 0.0F);

    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(magazineRoundCount(session.profile(), magazine), roundsBefore - 1U);
    EXPECT_TRUE(session.profile().assets.find(rifle)->chamberedRound.has_value());
}

TEST(AlphaExtractionSessionTest, ReloadCommitsSelectedChestMagazineAfterTwoSeconds)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-reload"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId original = *installedMagazine(session.profile(), rifle);
    ASSERT_TRUE(session.deployAlpha(3319));

    GameplayInput reload{};
    reload.reloadJustPressed = true;
    session.update(reload, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    session.update(GameplayInput{}, 2.0F);

    const auto installed = installedMagazine(session.profile(), rifle);
    ASSERT_TRUE(installed.has_value());
    EXPECT_NE(*installed, original);
    EXPECT_EQ(magazineRoundCount(session.profile(), *installed), 20U);
}

TEST(AlphaExtractionSessionTest, TargetedReloadIsAtomicAndChambersAfterTwoSeconds)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-targeted-reload"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId original = *installedMagazine(session.profile(), rifle);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto target = std::find_if(
        magazines.begin(),
        magazines.end(),
        [original, &session](AssetInstanceId id)
        {
            return id != original &&
                   assetIsCarried(session.profile(), id) &&
                   magazineRoundCount(session.profile(), id) == 20U;
        });
    ASSERT_NE(target, magazines.end());
    ASSERT_TRUE(session.deployAlpha(3320));

    for (int shot = 0; shot < 10; ++shot)
    {
        ASSERT_TRUE(session.executeProfileWeaponAmmo(
            FireWeaponCommand{rifle},
            "targeted-setup-fire-" + std::to_string(shot)).succeeded);
    }
    ASSERT_FALSE(session.profile().assets.find(rifle)->chamberedRound.has_value());
    const std::uint64_t beforeInterrupted =
        profileStateFingerprint(session.profile());

    ASSERT_TRUE(session.startAlphaReload(rifle, *target));
    GameplayInput inventoryOpened{};
    inventoryOpened.inventoryOpen = true;
    session.update(inventoryOpened, 0.5F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeInterrupted);
    EXPECT_EQ(installedMagazine(session.profile(), rifle), original);

    ASSERT_TRUE(session.startAlphaReload(rifle, *target));
    session.update(GameplayInput{}, 2.0F);
    EXPECT_EQ(installedMagazine(session.profile(), rifle), *target);
    EXPECT_TRUE(session.profile().assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 19U);
}

TEST(AlphaExtractionSessionTest, RaidMagazineUnloadIsInterruptibleAndAtomic)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-unload-magazine"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId installed = *installedMagazine(
        session.profile(), rifle);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto target = std::find_if(
        magazines.begin(),
        magazines.end(),
        [installed, &session](AssetInstanceId id)
        {
            return id != installed &&
                   assetIsCarried(session.profile(), id) &&
                   magazineRoundCount(session.profile(), id) == 20U;
        });
    ASSERT_NE(target, magazines.end());
    ASSERT_TRUE(session.deployAlpha(3321));
    ASSERT_EQ(carriedLooseAmmunition(session.profile()), 0U);

    const std::uint64_t beforeInterrupted =
        profileStateFingerprint(session.profile());
    ASSERT_TRUE(session.startAlphaUnloadMagazine(*target));
    GameplayInput inventoryOpened{};
    inventoryOpened.inventoryOpen = true;
    session.update(inventoryOpened, 1.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeInterrupted);
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 20U);

    ASSERT_TRUE(session.startAlphaUnloadMagazine(*target));
    session.update(GameplayInput{}, 3.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 0U);
    EXPECT_EQ(carriedLooseAmmunition(session.profile()), 20U);
}

TEST(AlphaExtractionSessionTest, DeathSettlesFullLossAndIsIdempotent)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-death"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(7711));
    const std::string settlementId = session.profile().pendingRaid->settlementId;

    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);

    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_FALSE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(session.profile().currentHealth, 100);
    EXPECT_TRUE(session.profile().committedSettlements.contains(settlementId));
    EXPECT_EQ(session.profile().lastRaidResult->outcome,
              RaidResultOutcome::PlayerDead);
    EXPECT_FALSE(equippedAsset(
        session.profile(), EquipmentSlotKind::PrimaryWeapon).has_value());
}

TEST(AlphaExtractionSessionTest, ReopeningPendingRaidCommitsAbnormalFailure)
{
    TemporarySaveDirectory temporary;
    {
        GameSession first;
        first.configurePersistence(temporary.path());
        ASSERT_TRUE(first.startNewProfile("alpha-session-abnormal"));
        prepareArmedLoadout(first);
        ASSERT_TRUE(first.deployAlpha(44771));
        ASSERT_TRUE(first.profile().pendingRaid.has_value());
    }

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_TRUE(reopened.recoveredAbandonedRaid());
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    ASSERT_TRUE(reopened.profile().lastRaidResult.has_value());
    EXPECT_EQ(reopened.profile().lastRaidResult->outcome,
              RaidResultOutcome::AbnormalQuit);
    EXPECT_FALSE(equippedAsset(
        reopened.profile(), EquipmentSlotKind::PrimaryWeapon).has_value());
}

TEST(AlphaExtractionSessionTest, CorruptPrimaryRecoversPendingRaidAsFailure)
{
    TemporarySaveDirectory temporary;
    {
        GameSession first;
        first.configurePersistence(temporary.path());
        ASSERT_TRUE(first.startNewProfile("alpha-session-corrupt-pending"));
        prepareArmedLoadout(first);
        ASSERT_TRUE(first.deployAlpha(55771));
        ASSERT_TRUE(first.profile().pendingRaid.has_value());
    }

    std::ofstream corrupt(
        temporary.path() / "profile.json",
        std::ios::trunc);
    corrupt << "{corrupt";
    corrupt.close();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_EQ(reopened.lastSaveLoadStatus(), SaveLoadStatus::RecoveredBackup);
    EXPECT_TRUE(reopened.recoveredAbandonedRaid());
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    ASSERT_TRUE(reopened.profile().lastRaidResult.has_value());
    EXPECT_EQ(
        reopened.profile().lastRaidResult->outcome,
        RaidResultOutcome::AbnormalQuit);
    EXPECT_FALSE(equippedAsset(
        reopened.profile(),
        EquipmentSlotKind::PrimaryWeapon).has_value());
}
