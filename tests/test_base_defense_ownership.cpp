#include "base_defense_ownership.h"
#include "base_defense_checkpoint_writer.h"
#include "base_defense_preparation.h"
#include "base_defense_positions.h"
#include "base_fortification_domain.h"
#include "base_siege_domain.h"
#include "game_flow.h"
#include "save_repository.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
const auto &content = publishedContentRegistry();
ProfileState warning(bool withStructure = true)
{
    auto p = makeNewAlphaProfile("frozen-fortification-warning", content);
    if (withStructure)
    {
        p.baseConstruction.materialUnits = 30;
        EXPECT_TRUE(executeFortificationCommand(p, content, BuildFortificationCommand{kWoodBarricadeDefinition},
            {p.revision, "build"}).succeeded);
        BaseWorld world;
        const auto positions = baseDefensePositionCandidates(world.layout(), "",
            *content.findFortification(kWoodBarricadeDefinition));
        for (const auto &position : positions)
            if (position.available)
            {
                p.baseFortifications.instances.at({1}).slot = position.key;
                break;
            }
        EXPECT_TRUE(p.baseFortifications.instances.at({1}).slot);
        p.baseFortifications.instances.at({1}).durability = 60;
    }
    p.baseSiege.raidThreatUnits = 100;
    p.baseSiege.safeUntilWorldMinute = p.worldClock.elapsedWorldMinutes;
    EXPECT_TRUE(activateBaseSiegeWarningIfEligible(p));
    p.baseDefenseWarning = prepareBaseDefenseWarning(p, content);
    EXPECT_TRUE(p.baseDefenseWarning->layout);
    EXPECT_TRUE(validateProfileState(p, content).valid);
    return p;
}
BaseDefenseSnapshot checkout(const ProfileState &p)
{
    auto s = *p.baseDefenseWarning->layout;
    for (auto &f : s.fortifications)
        f.initialDurability = f.durability = p.baseFortifications.instances.at(f.id).durability;
    s.layoutHash = baseDefenseLayoutHash(s);
    return s;
}
void start(ProfileState &p)
{
    auto receipt = executeBaseRealtimeDefenseStart(p, content, checkout(p), {p.revision, "start"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}
struct SaveFixture
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-defense-ownership-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~SaveFixture() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}

TEST(BaseDefenseOwnershipTest, WarningRandomnessIgnoresCurrencyRevisionAndRepair)
{
    auto p = warning();
    const auto original = *p.baseDefenseWarning->layout;
    const auto before = profileStateFingerprint(p);
    const auto repeat = prepareBaseDefenseWarning(p, content);
    EXPECT_EQ(profileStateFingerprint(p), before);
    ASSERT_TRUE(repeat.layout);
    EXPECT_EQ(repeat.layout->layoutHash, original.layoutHash);
    p.currency += 17;
    ++p.revision;
    p.baseFortifications.instances.at({1}).durability = 0;
    const auto altered = prepareBaseDefenseWarning(p, content);
    ASSERT_TRUE(altered.layout);
    EXPECT_EQ(altered.layout->seed, original.seed);
    EXPECT_EQ(altered.layout->layoutHash, original.layoutHash);
}

TEST(BaseDefenseOwnershipTest, WarningRepairUpdatesCheckoutNotFrozenRoutes)
{
    auto p = warning();
    const auto frozenHash = p.baseDefenseWarning->layout->layoutHash;
    ASSERT_TRUE(executeFortificationCommand(p, content, RepairFortificationCommand{{1}},
        {p.revision, "repair"}).succeeded);
    EXPECT_EQ(p.baseDefenseWarning->layout->layoutHash, frozenHash);
    start(p);
    ASSERT_TRUE(p.activeBaseDefense);
    EXPECT_EQ(p.activeBaseDefense->fortifications.front().initialDurability, 120U);
    EXPECT_FALSE(queryFortificationCommand(p, content, StoreFortificationCommand{{1}}).canCommit);
    EXPECT_FALSE(queryFortificationCommand(p, content, RepairFortificationCommand{{1}}).canCommit);
    EXPECT_TRUE(validateProfileState(p, content).valid);
}

TEST(BaseDefenseOwnershipTest, StartRejectsRerolledGeometryWrongOwnerAndMissingFrozenWarning)
{
    auto p = warning();
    const auto before = profileStateFingerprint(p);
    auto s = checkout(p);
    s.wavePlans.front().entry.x += 1;
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(executeBaseRealtimeDefenseStart(p, content, s, {p.revision, "changed"}).succeeded);
    s = checkout(p);
    --s.fortifications.front().durability;
    --s.fortifications.front().initialDurability;
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, content, s).canCommit);
    EXPECT_EQ(profileStateFingerprint(p), before);
    s = checkout(p);
    p.baseDefenseWarning.reset();
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, content, s).canCommit);
}

TEST(BaseDefenseOwnershipTest, DamageWritesSameOwnerAndDisabledStateWithoutTouchingOtherAssets)
{
    auto p = warning();
    start(p);
    ASSERT_TRUE(p.activeBaseDefense);
    const auto before = p;
    auto next = *p.activeBaseDefense;
    next.fortifications.front().durability = 0;
    next.fortificationGeometryRevision = 1;
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    EXPECT_EQ(p.baseFortifications.instances.at({1}).durability, 0U);
    EXPECT_EQ(p.baseFortifications.nextInstanceId, before.baseFortifications.nextInstanceId);
    EXPECT_EQ(p.revision, before.revision);
    EXPECT_EQ(p.currency, before.currency);
    EXPECT_EQ(p.assets.nextAssetId(), before.assets.nextAssetId());
    EXPECT_EQ(p.activeBaseDefense->layoutHash, before.activeBaseDefense->layoutHash);
    EXPECT_TRUE(validateProfileState(p, content).valid);
    const auto fingerprint = profileStateFingerprint(p);
    EXPECT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    EXPECT_EQ(profileStateFingerprint(p), fingerprint);
}

TEST(BaseDefenseOwnershipTest, InvalidOrOldCheckpointRejectsEveryOwnerBeforeMutation)
{
    auto p = warning();
    start(p);
    auto stale = *p.activeBaseDefense;
    auto next = stale;
    next.fortifications.front().durability = 12;
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(captureOwnedDefenseCheckpoint(p, stale, content));
    next.eventId += "-other";
    EXPECT_FALSE(captureOwnedDefenseCheckpoint(p, next, content));
    next = *p.activeBaseDefense;
    next.fortifications.front().id = {999};
    EXPECT_FALSE(captureOwnedDefenseCheckpoint(p, next, content));
    next = *p.activeBaseDefense;
    next.fortifications.front().durability = 0; // no matching disabled revision
    EXPECT_FALSE(captureOwnedDefenseCheckpoint(p, next, content));
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseDefenseOwnershipTest, Schema48WarningAndActiveRoundTripRejectLegacyWrite)
{
    auto p = warning();
    for (int phase = 0; phase < 2; ++phase)
    {
        const auto text = serializeProfileEnvelope(p, content.contentVersion());
        const auto loaded = deserializeProfileEnvelope(text, content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(p));
        EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(p, content.contentVersion(), 47)),
                     std::invalid_argument);
        if (phase == 0) start(p);
    }
    p.baseFortifications.instances.at({1}).durability = 1;
    EXPECT_FALSE(validateProfileState(p, content).valid);
}

TEST(BaseDefenseOwnershipTest, Legacy47WarningRemainsLegacyAndDoesNotPrepareOnLoad)
{
    auto p = warning(false);
    p.baseDefenseWarning.reset();
    SaveFixture save;
    // Explicit legacy envelope exercises migration without synthesizing a warning.
    const auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(p, content.contentVersion(), 47), content);
    ASSERT_TRUE(loaded.profile);
    EXPECT_FALSE(loaded.profile->baseDefenseWarning);
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(*loaded.profile, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.continueGame());
    ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    EXPECT_EQ(flow.gameSession().profile().activeBaseDefense->rulesVersion, kBaseDefenseRulesVersion);
    EXPECT_FALSE(flow.gameSession().profile().baseDefenseWarning);
}

TEST(BaseDefenseOwnershipTest, UnavailableFrozenPreparationNeverFallsBackToRerolledRealtime)
{
    auto p = warning(false);
    p.baseDefenseWarning->layout.reset();
    EXPECT_TRUE(validateProfileState(p, content).valid);
    EXPECT_TRUE(queryBaseAutoDefense(p, content).canCommit);
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.continueGame());
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    EXPECT_FALSE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    EXPECT_FALSE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
}

TEST(BaseDefenseOwnershipTest, DisabledRuntimeRestoresAndSettlementDoesNotRepairOrDuplicate)
{
    auto p = warning();
    start(p);
    auto next = *p.activeBaseDefense;
    next.fortifications.front().durability = 0;
    next.fortificationGeometryRevision = 1;
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.continueGame()) << flow.gameSession().persistenceMessage();
    ASSERT_TRUE(flow.baseWorld().baseDefenseCheckpoint());
    EXPECT_EQ(flow.baseWorld().baseDefenseCheckpoint()->fortifications.front().durability, 0U);
    ASSERT_TRUE(flow.gameSession().checkpointWorldClock());
    const auto saved = repository.load(content);
    ASSERT_TRUE(saved.profile);
    EXPECT_EQ(saved.profile->baseFortifications.instances.at({1}).durability, 0U);
    ASSERT_TRUE(flow.gameSession().abandonBaseRealtimeDefense());
    EXPECT_EQ(flow.gameSession().profile().baseFortifications.instances.at({1}).durability, 0U);
    EXPECT_FALSE(flow.gameSession().profile().baseDefenseWarning);
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    EXPECT_FALSE(flow.gameSession().abandonBaseRealtimeDefense());
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
}

TEST(BaseDefenseOwnershipTest, FailedStartSavePreservesWarningOwnersAndIdleWorld)
{
    auto p = warning();
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.continueGame());
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    const auto obstruction = save.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    EXPECT_FALSE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    EXPECT_EQ(flow.gameSession().profile().activeBaseDefense->fortifications.front().initialDurability, 60U);
}

TEST(BaseDefenseOwnershipTest, AsyncWriteFailureRetryKeepsDurabilityAndCheckpointTogether)
{
    auto p = warning();
    start(p);
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    const auto durableHash = profileStateFingerprint(p);
    auto next = *p.activeBaseDefense;
    next.fortifications.front().durability = 12;
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    const auto obstruction = save.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    BaseDefenseCheckpointWriter writer{repository};
    static_cast<void>(writer.request(p, content.contentVersion()));
    EXPECT_FALSE(writer.flush().succeeded);
    auto disk = repository.load(content);
    ASSERT_TRUE(disk.profile);
    EXPECT_EQ(profileStateFingerprint(*disk.profile), durableHash);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    ASSERT_TRUE(writer.retry());
    ASSERT_TRUE(writer.flush().succeeded);
    disk = repository.load(content);
    ASSERT_TRUE(disk.profile);
    EXPECT_EQ(profileStateFingerprint(*disk.profile), profileStateFingerprint(p));
    EXPECT_EQ(disk.profile->activeBaseDefense->fortifications.front().durability, 12U);
    EXPECT_EQ(disk.profile->baseFortifications.instances.at({1}).durability, 12U);
}

TEST(BaseDefenseOwnershipTest, CorruptPrimaryRecoversOneWholeOlderDefenseNotMixedOwners)
{
    auto p = warning();
    start(p);
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    const auto previous = profileStateFingerprint(p);
    auto next = *p.activeBaseDefense;
    next.fortifications.front().durability = 0;
    next.fortificationGeometryRevision = 1;
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, next, content));
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    { std::ofstream corrupt{repository.primaryPath(), std::ios::trunc}; corrupt << "{truncated"; }
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.status, SaveLoadStatus::RecoveredBackup);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), previous);
    EXPECT_EQ(loaded.profile->baseFortifications.instances.at({1}).durability,
              loaded.profile->activeBaseDefense->fortifications.front().durability);
}

TEST(BaseDefenseOwnershipTest, NaturalWarningPersistsOnceAndReloadCannotReroll)
{
    auto p = makeNewAlphaProfile("natural-frozen-warning", content);
    p.baseSiege.raidThreatUnits = 100;
    p.baseSiege.safeUntilWorldMinute = p.worldClock.elapsedWorldMinutes;
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.continueGame());
    flow.gameSession().advanceBaseWorldClock(0.02F);
    const auto &warningState = flow.gameSession().profile().baseDefenseWarning;
    ASSERT_TRUE(warningState);
    ASSERT_TRUE(warningState->layout);
    const auto hash = warningState->layout->layoutHash;
    auto disk = repository.load(content);
    ASSERT_TRUE(disk.profile && disk.profile->baseDefenseWarning);
    EXPECT_EQ(disk.profile->baseDefenseWarning->layout->layoutHash, hash);
    for (int i = 0; i < 12; ++i) flow.gameSession().advanceBaseWorldClock(0.1F);
    EXPECT_EQ(flow.gameSession().profile().baseDefenseWarning->layout->layoutHash, hash);
    ASSERT_TRUE(flow.returnToMainMenu());
    ASSERT_TRUE(flow.continueGame());
    EXPECT_EQ(flow.gameSession().profile().baseDefenseWarning->layout->layoutHash, hash);
}

TEST(BaseDefenseOwnershipTest, LegacyRulesCannotBeSmuggledIntoPreparedActivity)
{
    auto p = warning(false);
    start(p);
    p.activeBaseDefense->rulesVersion = kBaseDefenseRulesVersion;
    p.activeBaseDefense->layoutHash = baseDefenseLayoutHash(*p.activeBaseDefense);
    EXPECT_FALSE(validateProfileState(p, content).valid);
}

TEST(BaseDefenseOwnershipTest, FrozenFixedLanesConsumeRealScratchAndPersistTheSameRemnant)
{
    auto p = warning(false);
    BaseWorld world;
    const auto positions = baseDefensePositionCandidates(world.layout(), "",
        *content.findFortification(kWoodBarricadeDefinition));
    for (const auto &position : positions)
        if (position.available)
        {
            const FortificationInstanceId id{p.baseFortifications.nextInstanceId++};
            p.baseFortifications.instances.emplace(id,
                FortificationRecord{kWoodBarricadeDefinition, 12U, position.key});
        }
    p.baseDefenseWarning = prepareBaseDefenseWarning(p, content);
    ASSERT_TRUE(p.baseDefenseWarning->layout);
    start(p);
    ASSERT_TRUE(p.activeBaseDefense);
    const auto &s = *p.activeBaseDefense;
    std::vector<BallisticBlocker> blockers;
    for (const auto &bounds : s.movementBlockers)
        blockers.push_back({static_cast<BallisticBlockerId>(blockers.size() + 1), bounds});
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(s, blockers));
    WorldShootingRuntime shooting;
    ASSERT_TRUE(shooting.restoreCheckpoint(s.shooting));
    unsigned damageFacts = 0;
    for (unsigned frame = 0; frame < 9000 && !runtime.breached() && !runtime.completed(); ++frame)
    {
        runtime.advance({}, 0.02F, s.playerPosition, world.playerSize(), false, shooting, blockers);
        damageFacts += static_cast<unsigned>(runtime.fortifications().damageFacts().size());
        if (damageFacts) break;
    }
    ASSERT_GT(damageFacts, 0U);
    const auto checkpoint = runtime.checkpoint(shooting);
    ASSERT_TRUE(captureOwnedDefenseCheckpoint(p, checkpoint, content));
    EXPECT_TRUE(validateProfileState(p, content).valid);
    EXPECT_GT(p.activeBaseDefense->fortificationGeometryRevision, 0U);
    SaveFixture save;
    SaveRepository repository{save.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    const auto disk = repository.load(content);
    ASSERT_TRUE(disk.profile);
    EXPECT_EQ(profileStateFingerprint(*disk.profile), profileStateFingerprint(p));
    EXPECT_EQ(disk.profile->activeBaseDefense->enemies, checkpoint.enemies);
    EXPECT_EQ(disk.profile->activeBaseDefense->fortificationAttacks.size(), checkpoint.fortificationAttacks.size());
}
