#include "base_construction_domain.h"
#include "base_defense_serialization.h"
#include "base_facility_layout_domain.h"
#include "base_population_domain.h"
#include "base_siege_domain.h"
#include "base_site_feature_domain.h"
#include "home_perimeter_domain.h"
#include "save_repository.h"
#include "world_shooting_runtime.h"
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>

namespace
{
ProfileState warningProfile()
{
    auto p = makeNewAlphaProfile("manual-defense", publishedContentRegistry());
    p.baseSiege.raidThreatUnits = 100;
    p.baseSiege.safeUntilWorldMinute = p.worldClock.elapsedWorldMinutes;
    EXPECT_TRUE(activateBaseSiegeWarningIfEligible(p));
    return p;
}
BaseDefenseSnapshot planFor(const ProfileState &p)
{
    BaseDefenseSnapshot s;
    s.eventId = baseSiegeEventId(p);
    s.siegeSequence = p.baseSiege.siegeSequence;
    s.siteDefinitionId = "regional_base_site.greyline_yard";
    s.layoutIdentity = "base-defense.test.v1";
    s.seed = 71;
    s.worldSize = {1000, 1000};
    s.safeCore = {{400, 400}, {100, 100}};
    s.corridors = {{{180, 430}, {220, 40}}};
    s.coreDefenseZones = {{{380, 420}, {20, 60}}};
    s.frozenPopulation = p.basePopulation.ordinaryResidents;
    s.frozenMoraleTier = static_cast<std::uint32_t>(p.baseMorale.tier);
    s.frozenSiteThreat = publishedContentRegistry()
                             .regionalBaseSite(RegionalBaseSiteDefinitionId{s.siteDefinitionId})
                             .dailyBaseThreatUnits;
    s.playerPosition = {450, 450};
    s.shooting = WorldShootingRuntime{}.checkpoint();
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        BaseDefenseWaveSnapshot w;
        w.releaseSeconds = static_cast<float>(i * 8);
        w.entry = {200, 450};
        w.target = {390, 450};
        w.route = {w.entry, w.target};
        for (std::uint64_t n = i * 8 + 1; n <= i * 8 + 8; ++n)
            w.enemyIds.push_back(n);
        s.wavePlans.push_back(w);
    }
    s.layoutHash = baseDefenseLayoutHash(s);
    return s;
}
void start(ProfileState &p)
{
    auto receipt = executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), planFor(p),
                                                   {p.revision, "start"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}
void complete(ProfileState &p)
{
    auto &s = *p.activeBaseDefense;
    for (const auto &w : s.wavePlans)
        s.killedIds.insert(s.killedIds.end(), w.enemyIds.begin(), w.enemyIds.end());
    s.spawnedEnemyCount = 24;
    s.currentWave = 3;
    s.elapsedSeconds = 70;
}
} // namespace

TEST(BaseDefenseDomainTest, RuntimeV2CannotBypassPendingWarningAndProfileCheckoutGate)
{
    auto p = warningProfile();
    auto s = planFor(p);
    s.rulesVersion = kFortifiedBaseDefenseRulesVersion;
    s.layoutHash = baseDefenseLayoutHash(s);
    std::string message;
    ASSERT_TRUE(validateBaseDefenseSnapshot(s, message)) << message;
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, publishedContentRegistry(), s).canCommit);
    EXPECT_FALSE(executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s,
        {p.revision, "unsupported-checkout"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
    start(p);
    p.activeBaseDefense->rulesVersion = kFortifiedBaseDefenseRulesVersion;
    p.activeBaseDefense->layoutHash = baseDefenseLayoutHash(*p.activeBaseDefense);
    EXPECT_FALSE(validateProfileState(p, publishedContentRegistry()).valid);
}

TEST(BaseDefenseDomainTest, QueryIsPureAndStartDoesNotChargeSecurity)
{
    auto p = warningProfile();
    p.baseResources.pool.security = 37;
    auto s = planFor(p);
    auto before = profileStateFingerprint(p);
    EXPECT_TRUE(queryBaseRealtimeDefenseStart(p, publishedContentRegistry(), s).canCommit);
    EXPECT_EQ(profileStateFingerprint(p), before);
    auto r =
        executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s, {p.revision, "start"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(p.baseResources.pool.security, 37U);
    EXPECT_TRUE(p.activeBaseDefense);
    EXPECT_FALSE(p.baseSiege.warningActive);
    EXPECT_FALSE(queryBaseAutoDefense(p, publishedContentRegistry()).canCommit);
    EXPECT_FALSE(activateBaseSiegeWarningIfEligible(p));
    EXPECT_FALSE(advanceBaseSiegeWarning(p, 180));
}

TEST(BaseDefenseDomainTest, StartRetryIsIdempotentAndNewTransactionCannotRestart)
{
    auto p = warningProfile();
    auto s = planFor(p);
    auto revision = p.revision;
    ASSERT_TRUE(
        executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s, {revision, "start"})
            .succeeded);
    auto before = profileStateFingerprint(p);
    EXPECT_TRUE(
        executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s, {revision, "start"})
            .alreadyCommitted);
    EXPECT_FALSE(executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s,
                                                 {p.revision, "another-start"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseDefenseDomainTest, StaleRevisionAndCorruptStartRejectWithoutMutation)
{
    auto p = warningProfile();
    auto s = planFor(p);
    auto before = profileStateFingerprint(p);
    EXPECT_FALSE(
        executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s, {p.revision - 1, "stale"})
            .succeeded);
    s.layoutHash ^= 1U;
    EXPECT_FALSE(
        executeBaseRealtimeDefenseStart(p, publishedContentRegistry(), s, {p.revision, "corrupt"})
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseDefenseDomainTest, StartRejectsChangedFrozenPopulationSiteAndPlot)
{
    auto p = warningProfile();
    auto s = planFor(p);
    ++s.frozenPopulation;
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, publishedContentRegistry(), s).canCommit);
    s = planFor(p);
    s.siteDefinitionId = "regional_base_site.ashworks_depot";
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, publishedContentRegistry(), s).canCommit);
    s = planFor(p);
    s.plotId = "unknown";
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(queryBaseRealtimeDefenseStart(p, publishedContentRegistry(), s).canCommit);
}

TEST(BaseDefenseDomainTest, PrematureVictoryAndFalseDownReject)
{
    auto p = warningProfile();
    start(p);
    auto before = profileStateFingerprint(p);
    auto id = p.activeBaseDefense->eventId;
    for (auto reason : {BaseDefenseEndReason::Completed, BaseDefenseEndReason::PlayerDown,
                        BaseDefenseEndReason::Breached})
        EXPECT_FALSE(executeBaseRealtimeDefenseSettlement(p, publishedContentRegistry(), id, reason,
                                                          {p.revision, "false-result"})
                         .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseDefenseDomainTest, VictoryPreservesPersonalAssetsAndHasSingleEventResult)
{
    auto p = warningProfile();
    start(p);
    complete(p);
    auto count = p.assets.records().size();
    auto high = p.assets.nextAssetId();
    auto currency = p.currency;
    auto security = p.baseResources.pool.security;
    auto materials = p.baseConstruction.materialUnits;
    auto time = p.worldClock.elapsedWorldMinutes;
    auto id = p.activeBaseDefense->eventId;
    auto r = executeBaseRealtimeDefenseSettlement(
        p, publishedContentRegistry(), id, BaseDefenseEndReason::Completed, {p.revision, "win"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(r.outcome, BaseSiegeOutcome::Defended);
    EXPECT_FALSE(p.activeBaseDefense);
    EXPECT_EQ(p.baseResources.pool.security, security);
    EXPECT_EQ(p.currency, currency);
    EXPECT_EQ(p.assets.records().size(), count);
    EXPECT_EQ(p.assets.nextAssetId(), high);
    EXPECT_EQ(p.baseConstruction.materialUnits, materials + 8);
    EXPECT_EQ(p.baseSiege.safeUntilWorldMinute, time + 7 * kWorldMinutesPerDay);
    auto before = profileStateFingerprint(p);
    EXPECT_TRUE(executeBaseRealtimeDefenseSettlement(
                    p, publishedContentRegistry(), id, BaseDefenseEndReason::Completed,
                    {p.revision, "win-again-different-transaction"})
                    .alreadyCommitted);
    EXPECT_TRUE(executeBaseAutoDefense(p, publishedContentRegistry(), {p.revision, "auto-again"})
                    .alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseDefenseDomainTest, BreachSoftFailureOnlyUsesExistingPublicLossClasses)
{
    auto p = warningProfile();
    p.baseResources.pool = {30, 30, 30, 30};
    start(p);
    auto &s = *p.activeBaseDefense;
    s.breachedIds = {1, 2, 3, 4, 5, 6};
    s.spawnedEnemyCount = 6;
    auto id = s.eventId;
    auto count = p.assets.records().size();
    auto time = p.worldClock.elapsedWorldMinutes;
    auto population = p.basePopulation.ordinaryResidents;
    auto r = executeBaseRealtimeDefenseSettlement(
        p, publishedContentRegistry(), id, BaseDefenseEndReason::Breached, {p.revision, "breach"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(r.outcome, BaseSiegeOutcome::SoftFailure);
    EXPECT_EQ(r.securitySpent, 0U);
    EXPECT_EQ(p.baseResources.pool.food, 25U);
    EXPECT_EQ(p.baseResources.pool.hygiene, 25U);
    EXPECT_EQ(p.baseResources.pool.morale, 25U);
    EXPECT_EQ(p.baseResources.pool.security, 30U);
    EXPECT_GE(p.basePopulation.ordinaryResidents, population - 1);
    EXPECT_GE(p.basePopulation.ordinaryResidents, 4U);
    EXPECT_EQ(p.assets.records().size(), count);
    EXPECT_TRUE(p.lostRaidRecords.empty());
    EXPECT_EQ(p.worldClock.elapsedWorldMinutes, time);
    EXPECT_EQ(p.baseSiege.safeUntilWorldMinute, time + 12 * kWorldMinutesPerDay);
}

TEST(BaseDefenseDomainTest, PlayerDownRescueAndTimeApplyExactlyOnce)
{
    auto p = warningProfile();
    start(p);
    p.currentHealth = 0;
    EXPECT_TRUE(validateProfileState(p, publishedContentRegistry()).valid);
    auto id = p.activeBaseDefense->eventId;
    auto time = p.worldClock.elapsedWorldMinutes;
    auto r = executeBaseRealtimeDefenseSettlement(
        p, publishedContentRegistry(), id, BaseDefenseEndReason::PlayerDown, {p.revision, "down"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(p.currentHealth, 35);
    EXPECT_EQ(p.worldClock.elapsedWorldMinutes, time + 240U);
    EXPECT_EQ(p.baseSiege.safeUntilWorldMinute, time + 240U + 12 * kWorldMinutesPerDay);
    auto after = profileStateFingerprint(p);
    EXPECT_TRUE(executeBaseRealtimeDefenseSettlement(p, publishedContentRegistry(), id,
                                                     BaseDefenseEndReason::PlayerDown,
                                                     {p.revision, "another-down"})
                    .alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p), after);
}

TEST(BaseDefenseDomainTest, ExplicitAbandonUsesSoftFailureNotRaidLoss)
{
    auto p = warningProfile();
    start(p);
    auto id = p.activeBaseDefense->eventId;
    auto assets = p.assets.records().size();
    auto hp = p.currentHealth;
    auto r = executeBaseRealtimeDefenseSettlement(p, publishedContentRegistry(), id,
                                                  BaseDefenseEndReason::Abandoned,
                                                  {p.revision, "abandon"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(r.outcome, BaseSiegeOutcome::SoftFailure);
    EXPECT_EQ(p.currentHealth, hp);
    EXPECT_EQ(p.assets.records().size(), assets);
    EXPECT_FALSE(p.lastRaidResult);
    EXPECT_TRUE(p.lostRaidRecords.empty());
}

TEST(BaseDefenseDomainTest, PerimeterHandoffDoesNotChargeReturnTimeAgain)
{
    auto p = warningProfile();
    const RegionalBaseSiteDefinitionId site{"regional_base_site.greyline_yard"};
    p.homePerimeter.sites.emplace(site, HomePerimeterSiteSnapshot{site, 0, 17, {}, {}});
    ASSERT_TRUE(beginHomePerimeterOuting(p, site, {p.revision, "outing"}).succeeded);
    const auto time = p.worldClock.elapsedWorldMinutes;
    start(p);
    EXPECT_FALSE(p.homePerimeter.activeOuting);
    EXPECT_EQ(p.worldClock.elapsedWorldMinutes, time);
    const auto id = p.activeBaseDefense->eventId;
    EXPECT_TRUE(p.homePerimeter.committedResults.contains(id + "-perimeter-handoff"));
    EXPECT_FALSE(beginHomePerimeterOuting(p, site, {p.revision, "double-activity"}).succeeded);
    p.currentHealth = 0;
    ASSERT_TRUE(executeBaseRealtimeDefenseSettlement(p, publishedContentRegistry(), id,
                                                     BaseDefenseEndReason::PlayerDown,
                                                     {p.revision, "down"})
                    .succeeded);
    EXPECT_FALSE(completeHomePerimeterOuting(p, false, {p.revision, "extra-return"}).succeeded);
    EXPECT_EQ(p.worldClock.elapsedWorldMinutes, time + 240U);
}

TEST(BaseDefenseDomainTest, RescueOverflowRejectsWithoutPartialState)
{
    auto p = warningProfile();
    start(p);
    p.currentHealth = 0;
    p.worldClock.elapsedWorldMinutes = std::numeric_limits<std::uint64_t>::max() - 100;
    const auto id = p.activeBaseDefense->eventId;
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(executeBaseRealtimeDefenseSettlement(p, publishedContentRegistry(), id,
                                                      BaseDefenseEndReason::PlayerDown,
                                                      {p.revision, "overflow"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
    EXPECT_TRUE(p.activeBaseDefense);
    EXPECT_EQ(p.currentHealth, 0);
}

TEST(BaseDefenseDomainTest, Schema46RoundTripsActiveDefenseAndServiceSequences)
{
    auto p = warningProfile();
    start(p);
    auto &s = *p.activeBaseDefense;
    s.commandSequence = 17;
    s.weaponFaultSequence = 18;
    s.medicalRandomSequence = 19;
    s.woundRandomSequence = 20;
    s.pendingWorldSeconds = 0.75;
    s.baseCombatElapsedSeconds = 14.5F;
    s.medicalTickAccumulatorSeconds = 0.17F;
    auto text = serializeProfileEnvelope(p, publishedContentRegistry().contentVersion(), 46);
    EXPECT_EQ(nlohmann::json::parse(text).at("schema_version"), 46);
    auto loaded = deserializeProfileEnvelope(text, publishedContentRegistry());
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(p));
    EXPECT_EQ(loaded.profile->activeBaseDefense->commandSequence, 17U);
}

TEST(BaseDefenseDomainTest, Schema45MigrationNeverInventsActiveOrResolvedEvents)
{
    auto p = warningProfile();
    p.baseSiege.autoDefensePresetSaved = true;
    auto text = serializeProfileEnvelope(p, publishedContentRegistry().contentVersion(), 45);
    auto loaded = deserializeProfileEnvelope(text, publishedContentRegistry());
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_FALSE(loaded.profile->activeBaseDefense);
    EXPECT_EQ(loaded.profile->baseSiege.lastResolvedSequence, 0U);
    EXPECT_TRUE(loaded.profile->baseSiege.warningActive);
    EXPECT_TRUE(loaded.profile->baseSiege.autoDefensePresetSaved);
}

TEST(BaseDefenseDomainTest, ActiveDefenseCannotBeWrittenToOldSchema)
{
    auto p = warningProfile();
    start(p);
    EXPECT_THROW(static_cast<void>(
                     serializeProfileEnvelope(p, publishedContentRegistry().contentVersion(), 45)),
                 std::runtime_error);
}

TEST(BaseDefenseDomainTest, CorruptHashDuplicateEnemyAndUnknownResultAreRejected)
{
    auto p = warningProfile();
    auto s = planFor(p);
    std::string why;
    EXPECT_TRUE(validateBaseDefenseSnapshot(s, why)) << why;
    s.wavePlans[1].enemyIds[0] = 1;
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
    s = planFor(p);
    s.killedIds = {9000};
    s.spawnedEnemyCount = 1;
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
    s = planFor(p);
    s.wavePlans[0].route[0].x = std::numeric_limits<float>::quiet_NaN();
    s.layoutHash = baseDefenseLayoutHash(s);
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
    s = planFor(p);
    auto j = baseDefenseSnapshotJson(s);
    j["layout_hash"] = 0;
    EXPECT_THROW(static_cast<void>(parseBaseDefenseSnapshotJson(j)), std::runtime_error);
}

TEST(BaseDefenseDomainTest, SpawnedAndResolvedEnemiesMustConservePopulation)
{
    auto s = planFor(warningProfile());
    std::string why;
    s.spawnedEnemyCount = 2;
    s.killedIds = {1};
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
    s.spawnedEnemyCount = 1;
    EXPECT_TRUE(validateBaseDefenseSnapshot(s, why)) << why;
    s.breachedIds = {1};
    s.spawnedEnemyCount = 2;
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
}

TEST(BaseDefenseDomainTest, FullEnemyPrivateCheckpointAndHashArePreserved)
{
    auto s = planFor(warningProfile());
    EnemyRuntimeCheckpoint e;
    e.id = 1;
    e.position = {210, 450};
    e.size = {32, 48};
    e.health = 70;
    e.maximumHealth = 100;
    e.attackType = 1;
    e.attackPhase = 2;
    e.attackRemaining = 0.13F;
    e.hitConsumed = true;
    e.grabCooldown = 1.2F;
    e.scratchCooldown = 0.4F;
    e.navigationTarget = CheckpointPoint{390, 450};
    e.navigationRefreshRemaining = 0.7F;
    s.enemies = {e};
    s.spawnedEnemyCount = 1;
    s.contacts = {{1, 0.2F}};
    auto before = baseDefenseCheckpointHash(s);
    auto loaded = parseBaseDefenseSnapshotJson(baseDefenseSnapshotJson(s));
    EXPECT_EQ(loaded.enemies, s.enemies);
    EXPECT_EQ(baseDefenseCheckpointHash(loaded), before);
    loaded.enemies[0].hitConsumed = false;
    EXPECT_NE(baseDefenseCheckpointHash(loaded), before);
}

TEST(BaseDefenseDomainTest, WarningAndActivityFreezeFacilityLayoutQueries)
{
    auto p = warningProfile();
    const RepositionBaseFacilityCommand command{
        BaseFacilityDefinitionId{"base_facility.warehouse"},
        {1400, 2400},
        {300, 220},
        {RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"},
         {{1000, 2000}, {1600, 1120}},
         {}}};
    EXPECT_FALSE(queryBaseFacilityLayout(p, publishedContentRegistry(), command).canCommit);
    start(p);
    EXPECT_FALSE(queryBaseFacilityLayout(p, publishedContentRegistry(), command).canCommit);
}

TEST(BaseDefenseDomainTest, FrozenObstaclesSurviveSaveAndAffectLayoutHash)
{
    auto s = planFor(warningProfile());
    s.movementBlockers = {{{100, 100}, {50, 70}}};
    s.layoutHash = baseDefenseLayoutHash(s);
    const auto restored = parseBaseDefenseSnapshotJson(baseDefenseSnapshotJson(s));
    ASSERT_EQ(restored.movementBlockers.size(), 1U);
    EXPECT_FLOAT_EQ(restored.movementBlockers[0].size.y, 70.0F);
    EXPECT_EQ(baseDefenseCheckpointHash(restored), baseDefenseCheckpointHash(s));
    s.movementBlockers[0].size.y = 71.0F;
    EXPECT_NE(baseDefenseLayoutHash(s), restored.layoutHash);
    std::string why;
    EXPECT_FALSE(validateBaseDefenseSnapshot(s, why));
}

TEST(BaseDefenseDomainTest, ActiveDefenseRejectsRestIncludingOldTransactionReplay)
{
    auto p = warningProfile();
    p.committedTransactions.insert("earlier-rest");
    start(p);
    const auto before = profileStateFingerprint(p);
    const auto query = queryBaseRest(p, BaseRestCommand{6});
    EXPECT_FALSE(query.canCommit);
    EXPECT_EQ(query.error, DomainErrorCode::IllegalDestination);
    for (const auto *id : {"defense-rest", "earlier-rest"})
    {
        const auto result =
            executeBaseRest(p, publishedContentRegistry(), BaseRestCommand{6}, {p.revision, id});
        EXPECT_FALSE(result.succeeded);
        EXPECT_EQ(result.error, DomainErrorCode::IllegalDestination);
        EXPECT_EQ(profileStateFingerprint(p), before);
    }
}

TEST(BaseDefenseDomainTest, WarningAndActiveDefenseRejectNewSiteFeatureTimeSkip)
{
    auto p = warningProfile();
    const BaseSiteFeatureRepairCommand repair{
        RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"}};
    auto check = [&]
    {
        const auto before = profileStateFingerprint(p);
        const auto query = queryBaseSiteFeatureRepair(p, publishedContentRegistry(), repair);
        EXPECT_FALSE(query.canCommit);
        EXPECT_EQ(query.error, DomainErrorCode::IllegalDestination);
        EXPECT_NE(query.message.find("Base defense"), std::string::npos);
        EXPECT_FALSE(executeBaseSiteFeatureRepair(p, publishedContentRegistry(), repair,
                                                  {p.revision, "time-skip-repair"})
                         .succeeded);
        EXPECT_EQ(profileStateFingerprint(p), before);
    };
    check();
    start(p);
    check();
}

TEST(BaseDefenseDomainTest, ExistingConstructionCompletesOnNormalClockDuringDefense)
{
    auto p = warningProfile();
    p.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{"base_construction.dormitory.level_2"}, 4U, 3U,
        p.worldClock.elapsedWorldMinutes, p.worldClock.elapsedWorldMinutes + 360U};
    start(p);
    for (int minute = 0; minute < 360; ++minute)
    {
        static_cast<void>(advanceWorldClock(p.worldClock, 1));
        static_cast<void>(applyBaseConstructionThrough(p, publishedContentRegistry()));
    }
    EXPECT_TRUE(p.activeBaseDefense);
    EXPECT_FALSE(p.baseConstruction.activeProject);
    EXPECT_EQ(p.baseConstruction.dormitoryLevel, 2U);
    EXPECT_EQ(p.basePopulation.bedCapacity, 14U);
    EXPECT_TRUE(validateProfileState(p, publishedContentRegistry()).valid);
}
