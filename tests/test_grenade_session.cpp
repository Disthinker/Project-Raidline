#include "alpha_content_ids.h"
#include "game_session.h"
#include "grenade_domain.h"
#include "noise_decoy_fixture.h"
#include "raid_corpse_search.h"
#include "raid_grenade_runtime.h"
#include "save_repository.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>

// Keep this small accessor identical in test_grenade_performance.cpp. Tests
// stage legal deployments; production services still own every mutation tested.
struct GrenadeTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }
    static void betweenRaids(GameSession &session) { session.state_ = GameSessionState::BetweenRaids; }
    static void replaceWorld(GameSession &session, RaidWorldConfig config)
    { session.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static EnemyLifecycle &actors(GameplayWorld &world) { return world.activeEnemies(); }
};

namespace
{
const ItemDefinitionId kGrenade{"item.throwable.frag_grenade_basic"};
const auto &content() { return publishedContentRegistry(); }

AssetInstanceId addThrowable(ProfileState &profile, const ItemDefinitionId &definition,
    std::uint32_t quantity = 2, bool inRig = true)
{
    equipDecoyTestRig(profile, content());
    const auto rig = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    if (!rig) return 0;
    ProfileContainerId destination = ProfileContainerId::stash();
    if (inRig)
    {
        const auto &pockets = content().item(profile.assets.find(*rig)->definitionId).containerCompartments;
        for (std::uint32_t index = 0; index < pockets.size(); ++index)
            if (pockets[index].pocketKind == ContainerPocketKind::General)
            { destination = ProfileContainerId::compartment(*rig, index); break; }
    }
    const auto &item = content().item(definition);
    const auto fit = findFirstProfileFit(profile, content(), destination, item, ItemOrientation::Degrees0);
    return fit ? profile.assets.create(item, StoredAssetLocation{destination, *fit}, quantity) : 0;
}

RaidWorldConfig arena()
{
    RaidWorldConfig config;
    config.worldSize = {2000, 1200};
    config.playerSpawn = {100, 100};
    config.playerMaximumHealth = config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.extractionPoint = {{1800, 1000}, {100, 100}};
    config.noiseDecoyEnabled = config.grenadeEnabled = true;
    return config;
}

AssetInstanceId stage(GameSession &session, RaidWorldConfig config = arena(),
    bool inRig = true, const MapDefinitionId &map = MapDefinitionId{"map.v0.test"},
    bool bodyArmor = false)
{
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewPublishedProfile("grenade-session", content());
    profile.tutorial = TutorialProgress::Complete;
    profile.currentHealth = config.playerCurrentHealth;
    const auto id = addThrowable(profile, kGrenade, 2, inRig);
    EXPECT_NE(id, 0U);
    if (bodyArmor)
        for (const auto &[assetId, asset] : profile.assets.records())
            if (asset.definitionId == alpha_content::bodyArmor)
            {
                EXPECT_TRUE(executeInventory(profile, content(),
                    InventoryEquipCommand{assetId, EquipmentSlotKind::BodyArmor},
                    {profile.revision, "grenade-equip-armor"}).succeeded);
                break;
            }
    GrenadeTestAccess::betweenRaids(session);
    EXPECT_TRUE(session.deployAlpha(42, map)) << session.persistenceMessage();
    EXPECT_TRUE(profile.pendingRaid);
    GrenadeTestAccess::replaceWorld(session, std::move(config));
    return id;
}

void makeExplosionDue(GameplayWorld &world)
{
    auto runtime = world.grenade();
    runtime.advance(world.activeRaidSpaceId(), 2.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    world.commitGrenade(std::move(runtime));
}

const Enemy *findEnemy(const GameplayWorld &world, CombatTargetId id)
{
    const auto &enemies = world.enemies();
    const auto found = std::find_if(enemies.begin(), enemies.end(),
        [id](const Enemy &enemy) { return enemy.combatTargetId() == id; });
    return found == enemies.end() ? nullptr : &*found;
}

struct IsolatedSaveDirectory
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-grenade-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    IsolatedSaveDirectory() { std::filesystem::create_directories(path); }
    ~IsolatedSaveDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

std::string fileBytes(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

TEST(GrenadeSessionTest, ExactStackAndReplayCommitOnlyOneRealFlight)
{
    GameSession session;
    const auto id = stage(session);
    ASSERT_TRUE(session.canSelectThrowable());
    ASSERT_TRUE(session.beginThrowableSelection());
    const CommandContext command{session.profile().revision, "grenade-one"};
    const auto thrown = session.throwThrowable(id, {500, 116}, command);
    ASSERT_TRUE(thrown.succeeded) << thrown.message;
    ASSERT_TRUE(session.world().grenade().flight());
    EXPECT_EQ(session.profile().assets.find(id)->quantity, 1U);
    const auto fingerprint = profileStateFingerprint(session.profile());
    const auto replay = session.throwThrowable(id, {900, 500}, command);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_FLOAT_EQ(session.world().grenade().flight()->landing.point.x, 500);
    EXPECT_FALSE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-second"}).succeeded);
    EXPECT_FALSE(session.canSelectThrowable());
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);

    makeExplosionDue(session.world());
    session.update({}, 0);
    ASSERT_FALSE(session.world().grenade().active());
    const auto afterExplosion = profileStateFingerprint(session.profile());
    EXPECT_TRUE(session.throwThrowable(id, {600, 200}, command).alreadyCommitted);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_EQ(profileStateFingerprint(session.profile()), afterExplosion);
}

TEST(GrenadeSessionTest, InvalidAimStaleMissingStashAndDisabledActivityAreZeroModification)
{
    for (int refusal = 0; refusal < 6; ++refusal)
    {
        SCOPED_TRACE(refusal);
        GameSession session;
        auto config = arena();
        if (refusal == 4) config.grenadeEnabled = false;
        if (refusal == 5) config.developerRange = true;
        auto id = stage(session, config, refusal != 3);
        Vec2 aim{500, 116};
        CommandContext command{session.profile().revision, "grenade-reject"};
        if (refusal == 0) aim = {116, 116};
        if (refusal == 1) ++command.expectedRevision;
        if (refusal == 2) id = std::numeric_limits<AssetInstanceId>::max();
        const auto before = profileStateFingerprint(session.profile());
        EXPECT_FALSE(session.throwThrowable(id, aim, command).succeeded);
        EXPECT_EQ(profileStateFingerprint(session.profile()), before);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_FALSE(session.world().lastGrenadeExplosion());
        EXPECT_EQ(session.world().score(), 0);
    }
}

TEST(GrenadeSessionTest, WorldExplosionUsesArmorWallsAndStableIdentitiesWithoutCrossingSpace)
{
    auto config = arena();
    config.ballisticBlockers = {{1, {{540, 20}, {8, 220}}}};
    auto armor = content().enemyCombatDefinition(EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    ASSERT_TRUE(armor);
    config.initialEnemies = {
        EnemySpawn{{470, 106}, {20, 20}, 12},
        EnemySpawn{{490, 106}, {20, 20}, 12},
        EnemySpawn{{560, 106}, {20, 20}, 12},
        EnemySpawn{{490, 136}, {20, 20}, 12},
        EnemySpawn{{1000, 106}, {20, 20}, 12}};
    config.initialEnemies[1].torsoArmor = armor;
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.grenade_test_room"};
    room.displayName = "隔离空间";
    room.worldSize = config.worldSize;
    room.exteriorEntrance = {{80, 80}, {100, 100}};
    room.exteriorReturn = room.interiorSpawn = {100, 100};
    room.interiorExit = {{80, 80}, {100, 100}};
    room.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 12}};
    config.interiors = {room};
    GameSession session;
    const auto id = stage(session, config);
    const auto ordinaryFirst = session.world().enemies()[0].combatTargetId();
    const auto armored = session.world().enemies()[1].combatTargetId();
    const auto blocked = session.world().enemies()[2].combatTargetId();
    const auto ordinaryLast = session.world().enemies()[3].combatTargetId();
    const auto outsideRadius = session.world().enemies()[4].combatTargetId();
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-multiple"}).succeeded);
    makeExplosionDue(session.world());
    const auto prepared = session.world().prepareGrenadeExplosion();
    ASSERT_TRUE(prepared);
    ASSERT_EQ(prepared->enemies.size(), 3U);
    EXPECT_FALSE(prepared->playerDamage);
    session.update({}, 0);
    EXPECT_EQ(session.world().enemies().size(), 3U);
    EXPECT_FALSE(findEnemy(session.world(), ordinaryFirst));
    EXPECT_FALSE(findEnemy(session.world(), ordinaryLast));
    ASSERT_TRUE(findEnemy(session.world(), armored));
    EXPECT_EQ(findEnemy(session.world(), armored)->health(), 1);
    EXPECT_EQ(findEnemy(session.world(), armored)->torsoArmor()->durability, armor->durability - 6U);
    EXPECT_EQ(findEnemy(session.world(), blocked)->health(), 12);
    EXPECT_EQ(findEnemy(session.world(), outsideRadius)->health(), 12);
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_FALSE(session.world().commitPreparedGrenadeExplosion(*prepared));
    session.update({}, 0);
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_EQ(findEnemy(session.world(), armored)->health(), 1);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());

    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0);
    EXPECT_EQ(session.world().activeRaidSpaceId(), room.id);
    ASSERT_EQ(session.world().enemies().size(), 1U);
    EXPECT_EQ(session.world().enemies().front().health(), 12);
}

TEST(GrenadeSessionTest, MutatedTargetRejectsPreparedExplosionBeforeAnyOtherTargetChanges)
{
    auto config = arena();
    config.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 100},
        EnemySpawn{{490, 136}, {20, 20}, 100}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-stale-plan"}).succeeded);
    makeExplosionDue(session.world());
    const auto plan = session.world().prepareGrenadeExplosion();
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->enemies.size(), 2U);
    auto &actors = GrenadeTestAccess::actors(session.world());
    ASSERT_FALSE(actors.back().takeDamage(1));
    EXPECT_FALSE(session.world().validatePreparedGrenadeExplosion(*plan));
    EXPECT_FALSE(session.world().commitPreparedGrenadeExplosion(*plan));
    EXPECT_EQ(actors.front().health(), 100);
    EXPECT_EQ(actors.back().health(), 99);
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(session.world().grenade().pendingExplosion());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
}

TEST(GrenadeSessionTest, ThreeFrozenCorpsesOpenOnceWhileOtherDeathsGrantNoNewLoot)
{
    GameSession session;
    const auto id = stage(session, arena(), true, MapDefinitionId{"map.raid.frontier_exchange"});
    std::vector<std::string> sources;
    for (const auto &[assetId, asset] : session.profile().assets.records())
    {
        static_cast<void>(assetId);
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
            sources.push_back(corpse->sourceEnemyId);
    }
    ASSERT_EQ(sources.size(), 3U);
    auto config = arena();
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        EnemySpawn enemy{{470 + 20.0F * static_cast<float>(index), 106}, {12, 12}, 12};
        enemy.sourceEnemyId = sources[index];
        config.initialEnemies.push_back(enemy);
    }
    config.initialEnemies.push_back(EnemySpawn{{490, 130}, {12, 12}, 12});
    GrenadeTestAccess::replaceWorld(session, config);
    const auto frozenEnemies = session.profile().pendingRaid->enemies;
    const auto frozenLoot = session.profile().pendingRaid->loot;
    const auto highWater = session.profile().assets.nextAssetId();
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-corpse"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 1.0F / 60.0F);
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 400);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    std::size_t exposed = 0;
    for (const auto &[assetId, asset] : session.profile().assets.records())
    {
        static_cast<void>(assetId);
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
            if (corpse->deathPosition) ++exposed;
    }
    EXPECT_EQ(exposed, 3U);
    EXPECT_EQ(session.profile().assets.nextAssetId(), highWater);
    EXPECT_EQ(session.profile().pendingRaid->enemies, frozenEnemies);
    EXPECT_EQ(session.profile().pendingRaid->loot, frozenLoot);
    const auto after = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(session.world().score(), 400);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    EXPECT_EQ(profileStateFingerprint(session.profile()), after);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
}

TEST(GrenadeSessionTest, SelfDamageUsesProfileArmorAndDoesNotRollNewBleeding)
{
    for (bool armored : {false, true})
    {
        SCOPED_TRACE(armored);
        GameSession session;
        const auto id = stage(session, arena(), true, MapDefinitionId{"map.v0.test"}, armored);
        const auto armorId = equippedAsset(session.profile(), EquipmentSlotKind::BodyArmor);
        const auto durability = armorId ? session.profile().assets.find(*armorId)->currentDurability : 0U;
        const auto medical = session.profile().medicalStatus;
        const auto expected = queryIncomingDamage(session.profile(), content(),
            IncomingDamageCommand{24, HitRegion::Torso, 3, 6, false, {}});
        ASSERT_TRUE(expected.canCommit);
        if (!armored) EXPECT_EQ(expected.resolution.damageApplied, 24);
        ASSERT_TRUE(session.throwThrowable(id, {160, 116},
            {session.profile().revision, "grenade-self"}).succeeded);
        makeExplosionDue(session.world());
        session.update({}, 0);
        EXPECT_EQ(session.profile().currentHealth, 100 - expected.resolution.damageApplied);
        EXPECT_EQ(session.world().player().health(), session.profile().currentHealth);
        EXPECT_EQ(session.profile().medicalStatus, medical);
        ASSERT_TRUE(session.lastIncomingDamage());
        EXPECT_EQ(session.lastIncomingDamage()->semantic, HitSemantic::Normal);
        if (armorId)
            EXPECT_EQ(session.profile().assets.find(*armorId)->currentDurability,
                durability - expected.resolution.armorDurabilityLoss);
        EXPECT_FALSE(session.world().grenade().active());
    }
}

TEST(GrenadeSessionTest, PlayerCandidateFailureLeavesEnemiesArmorScoreAndExplosionUncommitted)
{
    auto config = arena();
    EnemySpawn enemy{{150, 106}, {12, 12}, 12};
    enemy.torsoArmor = content().enemyCombatDefinition(EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    config.initialEnemies = {enemy, EnemySpawn{{180, 106}, {12, 12}, 12}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-failure"}).succeeded);
    makeExplosionDue(session.world());
    ASSERT_TRUE(session.world().prepareGrenadeExplosion()->playerDamage);
    std::vector<EnemyRuntimeCheckpoint> checkpoints;
    for (const auto &actor : session.world().enemies()) checkpoints.push_back(actor.checkpoint());
    GrenadeTestAccess::profile(session).revision = std::numeric_limits<ProfileRevision>::max();
    const auto fingerprint = profileStateFingerprint(session.profile());
    session.update({}, 1.0F / 60.0F);
    EXPECT_EQ(session.state(), GameSessionState::SettlementBlocked);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    ASSERT_EQ(session.world().enemies().size(), checkpoints.size());
    // The earlier whole-checkpoint assertion also rejected ordinary perception
    // and facing updates at zero time. Compare only blast-owned participants.
    for (std::size_t index = 0; index < checkpoints.size(); ++index)
    {
        const auto actual = session.world().enemies()[index].checkpoint();
        const auto &before = checkpoints[index];
        EXPECT_EQ(actual.id, before.id);
        EXPECT_EQ(actual.position, before.position);
        EXPECT_EQ(actual.health, before.health);
        EXPECT_EQ(actual.maximumHealth, before.maximumHealth);
        EXPECT_EQ(actual.torsoArmor, before.torsoArmor);
    }
    EXPECT_EQ(session.world().player().health(), 100);
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    EXPECT_TRUE(session.world().grenade().pendingExplosion());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
}

TEST(GrenadeSessionTest, FlightAndFuseBlockPortalsAndExtractionUntilTheExplosionCommits)
{
    auto config = arena();
    config.extractionPoint = {{80, 80}, {100, 100}};
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.grenade_gate_room"};
    room.displayName = "门禁测试";
    room.worldSize = config.worldSize;
    room.exteriorEntrance = room.interiorExit = {{80, 80}, {100, 100}};
    room.exteriorReturn = room.interiorSpawn = {100, 100};
    config.interiors = {room};
    GameSession session;
    const auto id = stage(session, config);
    session.update({}, 0.25F);
    ASSERT_EQ(session.world().raidSession().state(), RaidSessionState::Extracting);
    ASSERT_GT(session.world().raidSession().extractionProgress(), 0);
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-gates"}).succeeded);
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0.5F);
    EXPECT_EQ(session.world().activeRaidSpaceId(), outdoorRaidSpaceId());
    EXPECT_TRUE(session.world().grenade().fuse());
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    session.update(enter, 1.49F);
    EXPECT_TRUE(session.world().grenade().active());
    EXPECT_EQ(session.world().activeRaidSpaceId(), outdoorRaidSpaceId());
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    session.update({}, 0.02F);
    ASSERT_FALSE(session.world().grenade().active());
    session.update(enter, 0);
    EXPECT_EQ(session.world().activeRaidSpaceId(), room.id);
    EXPECT_TRUE(session.profile().pendingRaid);
}

TEST(GrenadeSessionTest, InventoryMapAndWheelInputsDoNotFreezeTheRealFuse)
{
    for (int mode = 0; mode < 3; ++mode)
    {
        SCOPED_TRACE(mode);
        GameSession session;
        const auto id = stage(session);
        ASSERT_TRUE(session.throwThrowable(id, {500, 116},
            {session.profile().revision, "grenade-clock"}).succeeded);
        session.update({}, 0.5F);
        ASSERT_TRUE(session.world().grenade().fuse());
        GameplayInput interfaceInput;
        interfaceInput.inventoryOpen = mode == 0;
        interfaceInput.freezeObservationDirection = mode == 1;
        interfaceInput.throwableModal = mode == 2;
        session.update(interfaceInput, 1.0F);
        ASSERT_TRUE(session.world().grenade().fuse());
        EXPECT_NEAR(session.world().grenade().fuse()->elapsed, 1.0, 0.0001);
        session.update(interfaceInput, 0); // The client pause path supplies no advancing time.
        EXPECT_NEAR(session.world().grenade().fuse()->elapsed, 1.0, 0.0001);
        session.update(interfaceInput, 0.51F);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_TRUE(session.world().lastGrenadeExplosion());
    }
}

TEST(GrenadeSessionTest, LethalSelfDamageSettlesOnceAfterTheSameBlastKillsEnemies)
{
    auto config = arena();
    config.playerCurrentHealth = 20;
    config.initialEnemies = {EnemySpawn{{150, 106}, {12, 12}, 12},
        EnemySpawn{{180, 106}, {12, 12}, 12}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-lethal"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 1.0F / 60.0F);
    EXPECT_TRUE(session.world().player().isDead());
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
    const auto fingerprint = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_EQ(session.world().score(), 200);
}

TEST(GrenadeSessionTest, ExistingTerminalStateAndActiveQuitClearPendingGrenadeWithoutDetonation)
{
    for (bool alreadyDead : {false, true})
    {
        SCOPED_TRACE(alreadyDead);
        auto config = arena();
        config.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 12}};
        GameSession session;
        const auto id = stage(session, config);
        ASSERT_TRUE(session.throwThrowable(id, {500, 116},
            {session.profile().revision, "grenade-end"}).succeeded);
        makeExplosionDue(session.world());
        if (alreadyDead)
        {
            ASSERT_TRUE(session.world().markPlayerDead());
            session.update({}, 1.0F / 60.0F);
        }
        else ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
        EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
        EXPECT_FALSE(session.profile().pendingRaid);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_FALSE(session.world().lastGrenadeExplosion());
        EXPECT_EQ(session.world().score(), 0);
        ASSERT_EQ(session.world().enemies().size(), 1U);
        EXPECT_EQ(session.world().enemies().front().health(), 12);
    }
}

TEST(GrenadeSessionTest, SettlementSaveFailurePreservesCheckpointAndRetryDoesNotRefundOrExplode)
{
    IsolatedSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path);
    const auto id = stage(session);
    const auto checkpoint = fileBytes(temporary.path / "profile.json");
    ASSERT_FALSE(checkpoint.empty());
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-save-failure"}).succeeded);
    const auto afterThrow = profileStateFingerprint(session.profile());
    std::filesystem::create_directory(temporary.path / "profile.tmp.json");
    EXPECT_FALSE(session.activeQuitAlphaRaid());
    EXPECT_EQ(session.state(), GameSessionState::SettlementBlocked);
    EXPECT_EQ(profileStateFingerprint(session.profile()), afterThrow);
    EXPECT_EQ(fileBytes(temporary.path / "profile.json"), checkpoint);
    EXPECT_EQ(session.profile().assets.find(id)->quantity, 1U);
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
    std::filesystem::remove(temporary.path / "profile.tmp.json");
    ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
}

TEST(GrenadeSessionTest, ExplosionInvestigatesItsPointWithoutExpandingPlayerAcquisitionDistance)
{
    auto config = arena();
    // Player center (116,116) is 400 units away: beyond acquisition (360),
    // inside pursuit retention (460), with an unobstructed view. The blast is
    // deliberately below the enemy, while the player is to its left.
    config.initialEnemies = {EnemySpawn{{506, 106}, {20, 20}, 100}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_EQ(session.world().enemies().front().awarenessState(), EnemyAwarenessState::Unaware);
    ASSERT_FALSE(session.world().enemies().front().lastKnownTargetPosition());
    const Vec2 blast{500, 200};
    ASSERT_TRUE(session.throwThrowable(id, blast,
        {session.profile().revision, "grenade-investigate-only"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 0);
    const auto &investigator = session.world().enemies().front();
    ASSERT_GT(investigator.health(), 0);
    EXPECT_LT(investigator.health(), 100);
    EXPECT_EQ(investigator.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(investigator.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(investigator.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(investigator.lastKnownTargetPosition()->y, blast.y);
    const auto before = investigator.position();

    GameplayInput moveAway;
    moveAway.moveLeft = true;
    session.update(moveAway, 0.1F);
    const auto &after = session.world().enemies().front();
    EXPECT_EQ(after.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(after.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(after.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(after.lastKnownTargetPosition()->y, blast.y);
    // Verify the real navigation consumer, not just its memory label: following
    // the player would move left, whereas investigating the blast moves down.
    EXPECT_GT(after.position().y, before.y + 4.0F);
    EXPECT_GT(after.position().x, before.x - 4.0F);
    const auto player = session.world().player().position();
    const float half = session.world().player().size() / 2;
    EXPECT_NE(after.lastKnownTargetPosition()->x, player.x + half);
    EXPECT_NE(after.lastKnownTargetPosition()->y, player.y + half);
}

TEST(GrenadeSessionTest, AlertedListenerPropagatesToItsGroupWithoutReplacingItsExistingPursuit)
{
    auto config = arena();
    config.worldSize = {2600, 1200};
    const std::string group = "encounter.grenade-propagation";
    EnemySpawn pursuing{{506, 106}, {20, 20}, 100};
    EnemySpawn remote{{1790, 190}, {20, 20}, 100};
    EnemySpawn unrelated{{1790, 290}, {20, 20}, 100};
    pursuing.encounterGroupInstanceId = remote.encounterGroupInstanceId = group;
    unrelated.encounterGroupInstanceId = "encounter.unrelated";
    config.initialEnemies = {pursuing, remote, unrelated};
    GameSession session;
    const auto id = stage(session, config);
    const Vec2 blast{500, 200};
    ASSERT_TRUE(session.throwThrowable(id, blast,
        {session.profile().revision, "grenade-group-relay"}).succeeded);
    makeExplosionDue(session.world());
    const Vec2 knownPlayer{116, 116};
    GrenadeTestAccess::actors(session.world()).front().hearTarget(knownPlayer);
    session.update({}, 0);
    ASSERT_EQ(session.world().enemies().size(), 3U);
    const auto &near = session.world().enemies()[0];
    const auto &far = session.world().enemies()[1];
    const auto &other = session.world().enemies()[2];
    EXPECT_EQ(near.awarenessState(), EnemyAwarenessState::Alerted);
    ASSERT_TRUE(near.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(near.lastKnownTargetPosition()->x, knownPlayer.x);
    EXPECT_FLOAT_EQ(near.lastKnownTargetPosition()->y, knownPlayer.y);
    EXPECT_LT(near.health(), 100); // Preserving pursuit does not grant blast immunity.
    EXPECT_EQ(far.health(), 100);
    EXPECT_EQ(far.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(far.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(far.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(far.lastKnownTargetPosition()->y, blast.y);
    EXPECT_EQ(other.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(other.lastKnownTargetPosition());
    EXPECT_EQ(session.world().score(), 0);
}

TEST(GrenadeSessionTest, DueExplosionRetiresAnActiveScratchBeforeContactAndNeverReplaysItsHit)
{
    auto config = arena();
    config.initialEnemies = {EnemySpawn{{150, 100}, {32, 32}, 12}};
    const auto readyScratch = [](GameSession &session)
    {
        auto &enemy = GrenadeTestAccess::actors(session.world()).front();
        if (!enemy.tryStartAttack(EnemyAttackType::Scratch, {-1, 0})) return false;
        const auto attack = enemy.attackConfig();
        if (!attack) return false;
        static_cast<void>(enemy.update(attack->windupDuration + 0.00001F, 2000, 1200));
        return enemy.attackPhase() == EnemyAttackPhase::Active && enemy.hasAttackHitOpportunity();
    };

    // This identical live contact is a real counterfactual, not an out-of-range
    // attacker. Its accepted scratch reaches the Profile damage consumer.
    GameSession control;
    ASSERT_NE(stage(control, config), 0U);
    ASSERT_TRUE(readyScratch(control));
    control.update({}, 0.02F);
    ASSERT_TRUE(control.lastIncomingDamage());
    EXPECT_EQ(control.lastIncomingDamage()->damageBeforeArmor, 12);
    EXPECT_EQ(control.profile().currentHealth, 88);
    EXPECT_EQ(control.world().player().health(), 88);
    EXPECT_EQ(control.world().score(), 0);

    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-before-contact"}).succeeded);
    auto runtime = session.world().grenade();
    runtime.advance(session.world().activeRaidSpaceId(), 1.99F);
    ASSERT_TRUE(runtime.fuse());
    ASSERT_FALSE(runtime.pendingExplosion());
    session.world().commitGrenade(std::move(runtime));
    ASSERT_TRUE(readyScratch(session));
    const auto medical = session.profile().medicalStatus;
    session.update({}, 0.02F); // Fuse expires before this prepared contact can run.
    ASSERT_TRUE(session.world().lastGrenadeExplosion());
    EXPECT_TRUE(session.world().grenadeExplodedLastUpdate());
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 100);
    EXPECT_EQ(session.profile().currentHealth, 76);
    EXPECT_EQ(session.world().player().health(), 76);
    EXPECT_EQ(session.profile().medicalStatus, medical);
    ASSERT_TRUE(session.lastIncomingDamage());
    EXPECT_EQ(session.lastIncomingDamage()->damageBeforeArmor, 24);
    EXPECT_TRUE(session.world().takePlayerDamageObservations().empty());
    EXPECT_FALSE(session.world().grenade().active());

    session.update({}, 0.05F);
    EXPECT_EQ(session.profile().currentHealth, 76);
    EXPECT_EQ(session.world().player().health(), 76);
    EXPECT_EQ(session.profile().medicalStatus, medical);
    EXPECT_FALSE(session.lastIncomingDamage());
    EXPECT_TRUE(session.world().takePlayerDamageObservations().empty());
    EXPECT_EQ(session.world().score(), 100);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
}
