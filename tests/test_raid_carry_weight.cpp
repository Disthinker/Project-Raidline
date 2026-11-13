#include "base_world.h"
#include "game_session.h"
#include "test_temp_directory.h"
#include "weapon_handling_fixture.h"

#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>

struct RaidCarryWeightTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }

    static void useRaid(GameSession &session, bool rulesEnabled, bool training = false)
    {
        session.alphaRaidActive_ = true;
        session.developerRange_ = training;
        session.profile_.pendingRaid.emplace().backpackRecoveryEnabled = rulesEnabled;
    }

    static GameplayInput prepareMove(GameSession &session, bool sprint)
    {
        GameplayInput input;
        input.moveRight = true;
        input.sprint = sprint;
        GameplayInput prepared = input;
        session.preparePlayerCombatInput(prepared, input, 0.016F, std::nullopt);
        return prepared;
    }
};

namespace
{
using Access = RaidCarryWeightTestAccess;

const auto &content() { return publishedContentRegistry(); }

AssetInstanceId equip(ProfileState &profile, const char *definition, EquipmentSlotKind slot)
{
    return profile.assets.create(content().item(ItemDefinitionId{definition}), EquippedAssetLocation{slot});
}

AssetInstanceId storeItem(ProfileState &profile, ProfileContainerId container,
                          const char *id, std::uint32_t quantity = 1)
{
    const auto &definition = content().item(ItemDefinitionId{id});
    const auto origin = findFirstProfileFit(profile, content(), container, definition, ItemOrientation::Degrees0);
    if (!origin)
        throw std::runtime_error{"Test equipment must fit in its compartment"};
    return profile.assets.create(definition, StoredAssetLocation{container, *origin}, quantity);
}

void equipWeightedPack(ProfileState &profile, bool heavy, bool extraGram)
{
    const auto backpack = equip(profile, "item.container.backpack_expedition", EquipmentSlotKind::Backpack);
    const auto vest = equip(profile, "item.container.chest_rig_assault", EquipmentSlotKind::ChestRig);
    static_cast<void>(equip(profile, "item.protective_gear.body_armor_heavy", EquipmentSlotKind::BodyArmor));
    const auto packGrid = ProfileContainerId::compartment(backpack, 0);
    const auto vestGrid = ProfileContainerId::compartment(vest, 0);
    static_cast<void>(storeItem(profile, packGrid, "item.loot.sealed_water", 2));
    if (heavy)
    {
        static_cast<void>(storeItem(profile, packGrid, "item.loot.extraction_fuel_can"));
        static_cast<void>(storeItem(profile, packGrid, "item.loot.extraction_fuel_can"));
        static_cast<void>(storeItem(profile, packGrid, "item.medical.medkit_basic"));
    }
    if (extraGram)
    {
        static_cast<void>(storeItem(profile, vestGrid, "item.ammunition.5_45x39_standard", 7));
        static_cast<void>(storeItem(profile, vestGrid, "item.ammunition.9mm_basic", 2));
    }
    else
        static_cast<void>(storeItem(profile, vestGrid, "item.medical.painkiller_basic"));
}
}

TEST(RaidCarryWeightTest, SprintThresholdsAreInclusiveAndWalkKeepsItsSpeed)
{
    struct Case { bool heavy; bool extraGram; std::uint64_t weight; float multiplier; };
    for (const Case sample : {Case{false, false, 22000U, 1.0F}, Case{false, true, 22001U, 0.9F},
                              Case{true, false, 30000U, 0.9F}, Case{true, true, 30001U, 0.8F}})
    {
        SCOPED_TRACE(sample.weight);
        GameSession session;
        ASSERT_TRUE(session.startNewProfile("carry-threshold"));
        equipWeightedPack(Access::profile(session), sample.heavy, sample.extraGram);
        ASSERT_TRUE(validateProfileState(session.profile(), content()).valid);
        Access::useRaid(session, true);

        EXPECT_EQ(session.currentRaidCarriedWeightGrams(), sample.weight);
        const auto sprint = Access::prepareMove(session, true);
        EXPECT_TRUE(sprint.sprint);
        EXPECT_FLOAT_EQ(sprint.movementSpeedMultiplier, sample.multiplier);
        EXPECT_FLOAT_EQ(Access::prepareMove(session, false).movementSpeedMultiplier, 1.0F);
    }
}

TEST(RaidCarryWeightTest, OldFrozenRaidAndTrainingKeepFullSprint)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("carry-old-and-training"));
    equipWeightedPack(Access::profile(session), false, true);
    ASSERT_GT(session.currentRaidCarriedWeightGrams(), 22000U);
    Access::useRaid(session, false);
    EXPECT_FLOAT_EQ(Access::prepareMove(session, true).movementSpeedMultiplier, 1.0F);
    Access::useRaid(session, true, true);
    EXPECT_FLOAT_EQ(Access::prepareMove(session, true).movementSpeedMultiplier, 1.0F);
}

TEST(RaidCarryWeightTest, BaseSprintUsesCurrentSafetyZone)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("carry-base-zone"));
    equipWeightedPack(Access::profile(session), false, true);
    BaseWorld world;
    session.bindBasePlayerWorld(&world);

    ASSERT_EQ(world.playerSafetyZone(), HomeRegionSafetyZone::SafeCore);
    EXPECT_FLOAT_EQ(Access::prepareMove(session, true).movementSpeedMultiplier, 1.0F);

    const auto core = world.baseParcel();
    const Vec2 exterior{core.position.x - 100.0F, core.position.y + core.size.y * 0.5F};
    ASSERT_TRUE(world.restorePlayerBody(exterior, {1.0F, 0.0F}, 0.0F, 0.0F));
    ASSERT_NE(world.playerSafetyZone(), HomeRegionSafetyZone::SafeCore);
    EXPECT_FLOAT_EQ(Access::prepareMove(session, true).movementSpeedMultiplier, 0.9F);
    EXPECT_FLOAT_EQ(Access::prepareMove(session, false).movementSpeedMultiplier, 1.0F);
}

TEST(RaidCarryWeightTest, CommittedPlacementChangesWeightWithoutChangingAssetCount)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("carry-placement"));
    auto &profile = Access::profile(session);
    const auto bag = equip(profile, "item.container.backpack_expedition", EquipmentSlotKind::Backpack);
    const auto asset = storeItem(profile, ProfileContainerId::stash(), "item.loot.sealed_water", 2);
    const auto count = profile.assets.records().size();
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), 5000U);
    const auto pack = ProfileContainerId::compartment(bag, 0);
    const auto rejected = session.executeProfileInventory(
        InventoryMoveCommand{asset, 0U, {pack, {100, 100}}}, "carry-rejected");
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), 5000U);
    const auto committed = session.executeProfileInventory(
        InventoryMoveCommand{asset, 0U, {pack, {0, 0}}}, "carry-accepted");
    ASSERT_TRUE(committed.succeeded) << committed.message;
    EXPECT_EQ(profile.assets.records().size(), count);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), 7000U);
}

TEST(RaidCarryWeightTest, LiveShotSubtractsTheFiredCartridge)
{
    GameSession session;
    const auto loadout = prepareHandlingSession(session);
    auto &profile = Access::profile(session);
    const auto *weapon = profile.assets.find(loadout.weapon);
    ASSERT_TRUE(weapon && weapon->chamberedRound);
    const auto grams = content().item(weapon->chamberedRound->definitionId).unitWeightGrams;
    const auto before = session.currentRaidCarriedWeightGrams();
    const auto count = profile.assets.records().size();

    const auto shot = executeFireWeapon(profile, content(), FireWeaponCommand{loadout.weapon, 9999U, 0U},
        {profile.revision, "carry-shot"});
    ASSERT_TRUE(shot.succeeded) << shot.message;
    ASSERT_TRUE(shot.firedAmmunitionDefinitionId);
    EXPECT_EQ(profile.assets.records().size(), count);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), before - grams);
}

TEST(RaidCarryWeightTest, StaleAmmoCommandsCannotReadOrWriteDroppedPack)
{
    GameSession session;
    const auto loadout = prepareHandlingSession(session);
    const auto bag = equippedAsset(session.profile(), EquipmentSlotKind::Backpack);
    ASSERT_TRUE(bag);
    const auto pack = ProfileContainerId::compartment(*bag, 0);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{loadout.spareMagazine, 0U, {pack, {0, 0}}}, "carry-spare-to-pack").succeeded);
    const auto rig = ProfileContainerId::compartment(loadout.rig, 0);
    const WeaponAmmoCommand fromPack = UnloadMagazineCommand{loadout.spareMagazine, rig};
    const WeaponAmmoCommand intoPack = UnloadMagazineCommand{loadout.oldMagazine, pack};
    ASSERT_TRUE(queryWeaponAmmo(session.profile(), content(), fromPack).canCommit);
    ASSERT_TRUE(queryWeaponAmmo(session.profile(), content(), intoPack).canCommit);
    const auto weight = session.currentRaidCarriedWeightGrams();

    const auto dropped = session.dropRaidBackpack(*bag, {session.profile().revision, "carry-drop-for-ammo"});
    ASSERT_TRUE(dropped.succeeded) << dropped.message;
    EXPECT_LT(session.currentRaidCarriedWeightGrams(), weight);
    const auto revision = session.profile().revision;
    const auto fingerprint = profileStateFingerprint(session.profile());

    EXPECT_EQ(queryWeaponAmmo(session.profile(), content(), fromPack).error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(queryWeaponAmmo(session.profile(), content(), intoPack).error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(session.executeProfileWeaponAmmo(fromPack, "carry-stale-source").error,
              DomainErrorCode::IllegalDestination);
    EXPECT_EQ(session.executeProfileWeaponAmmo(intoPack, "carry-stale-destination").error,
              DomainErrorCode::IllegalDestination);
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
}

TEST(RaidCarryWeightTest, BatteryDrainUpdatesWeightOnlyWhenTheCellDisappears)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("carry-battery"));
    auto &profile = Access::profile(session);
    const auto rifle = equip(profile, "item.weapon.rifle_5_45_service", EquipmentSlotKind::PrimaryWeapon);
    const auto &rail = content().item(ItemDefinitionId{"item.component.rail_handguard"});
    static_cast<void>(profile.assets.create(rail, InstalledWeaponComponentLocation{rifle}));
    const auto &light = content().item(ItemDefinitionId{"item.component.weapon_flashlight_basic"});
    const auto lamp = profile.assets.create(light, InstalledWeaponComponentLocation{rifle});
    const auto &cell = content().item(ItemDefinitionId{"item.battery.universal"});
    const auto battery = profile.assets.create(cell, InstalledDeviceBatteryLocation{lamp});
    profile.assets.findMutable(lamp)->deviceState->switchOn = true;
    profile.assets.findMutable(battery)->remainingCharges = 20U;
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto revision = profile.revision;
    const auto before = session.currentRaidCarriedWeightGrams();

    advanceWeaponLight(profile, content(), {rifle, lamp, battery}, true, 0.001F);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), before);
    advanceWeaponLight(profile, content(), {rifle, lamp, battery}, true, 0.03F);
    ASSERT_EQ(profile.assets.find(battery), nullptr);
    EXPECT_EQ(profile.revision, revision);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), before - cell.unitWeightGrams);
}

TEST(RaidCarryWeightTest, SameRevisionReplacementRecomputesWeight)
{
    raidline::test::TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claim("raidline-carry-weight-", "replacement"));
    auto profile = makeNewAlphaProfile("carry-replacement", content());
    const auto rifle = handlingAssets(profile, alpha_content::rifle).front();
    SaveRepository repository{directory.path()};
    ASSERT_TRUE(repository.save(profile, content().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), 0U);
    const auto revision = session.profile().revision;
    const auto count = session.profile().assets.records().size();

    profile.assets.findMutable(rifle)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(repository.save(profile, content().contentVersion()).succeeded);
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(session.profile().assets.records().size(), count);
    EXPECT_EQ(session.currentRaidCarriedWeightGrams(), content().item(alpha_content::rifle).unitWeightGrams);
}

TEST(RaidCarryWeightTest, CachedReadsStayCheapWithLargeWarehouse)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("carry-large-warehouse"));
    auto &profile = Access::profile(session);
    equipWeightedPack(profile, false, true);
    const auto stash = ProfileContainerId::stash();
    const auto size = profileContainerSize(profile, content(), stash);
    const auto &ammo = content().item(alpha_content::ammunition);
    for (int y = 0; y < size.height && profile.assets.records().size() < 250U; ++y)
        for (int x = 0; x < size.width && profile.assets.records().size() < 250U; ++x)
            if (profilePlacementFits(profile, content(), stash, {x, y}, ammo, ItemOrientation::Degrees0))
                static_cast<void>(profile.assets.create(ammo, StoredAssetLocation{stash, {x, y}}));
    ASSERT_EQ(profile.assets.records().size(), 250U);
    ASSERT_TRUE(validateProfileState(profile, content()).valid);

    using Clock = std::chrono::steady_clock;
    const auto expected = session.currentRaidCarriedWeightGrams();
    constexpr unsigned scans = 40, cachedReads = 512;
    std::uint64_t scanTotal{}, cacheTotal{};
    const auto coldStart = Clock::now();
    for (unsigned sample = 0; sample < scans; ++sample)
        scanTotal += carriedWeightGrams(profile, content());
    const auto cold = Clock::now() - coldStart;

    const auto hotStart = Clock::now();
    for (unsigned sample = 0; sample < cachedReads; ++sample)
        cacheTotal += session.currentRaidCarriedWeightGrams();
    const auto hot = Clock::now() - hotStart;

    EXPECT_EQ(scanTotal, expected * scans);
    EXPECT_EQ(cacheTotal, expected * cachedReads);
    EXPECT_LT(hot, cold);
}
