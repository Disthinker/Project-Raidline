#include "base_defense_preparation.h"
#include "base_defense_positions.h"
#include "base_fortification_domain.h"
#include "base_siege_domain.h"
#include "game_flow.h"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

namespace {
const auto &content = publishedContentRegistry();
ProfileState prepared() {
    auto p = makeNewAlphaProfile("auto-wood", content);
    p.baseMorale.tier = BaseMoraleTier::Low; // room for both discounts above the floor
    BaseWorld world;
    for (const auto &slot : baseDefensePositionCandidates(world.layout(), "",
            *content.findFortification(kWoodBarricadeDefinition))) {
        if (!slot.available) continue;
        const FortificationInstanceId id{p.baseFortifications.nextInstanceId++};
        p.baseFortifications.instances.emplace(id, FortificationRecord{kWoodBarricadeDefinition, 120, slot.key});
    }
    p.baseSiege.raidThreatUnits = 100;
    p.baseSiege.safeUntilWorldMinute = p.worldClock.elapsedWorldMinutes;
    EXPECT_TRUE(activateBaseSiegeWarningIfEligible(p));
    p.baseDefenseWarning = prepareBaseDefenseWarning(p, content);
    EXPECT_TRUE(p.baseDefenseWarning->layout);
    EXPECT_TRUE(validateProfileState(p, content).valid);
    return p;
}
struct Store {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-auto-wood-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Store() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
}

TEST(BaseAutoFortificationTest, FrozenSidesSelectTwoStableOwnersNotThreeWavesOrReserves) {
    auto p = prepared();
    const auto before = profileStateFingerprint(p);
    const auto plan = queryBaseAutoDefense(p, content);
    ASSERT_EQ(plan.fortificationDiscount, 2U);
    EXPECT_NE(plan.participatingFortifications[0], plan.participatingFortifications[1]);
    EXPECT_EQ(profileStateFingerprint(p), before);
    auto baseline = p;
    baseline.baseDefenseWarning.reset();
    EXPECT_EQ(plan.requiredSecurity + 2, queryBaseAutoDefense(baseline, content).requiredSecurity);
    const FortificationInstanceId reserve{p.baseFortifications.nextInstanceId++};
    p.baseFortifications.instances.emplace(reserve, FortificationRecord{kWoodBarricadeDefinition, 120, {}});
    EXPECT_EQ(queryBaseAutoDefense(p, content).participatingFortifications, plan.participatingFortifications);
    for (const auto &[id, record] : p.baseFortifications.instances) {
        auto changed = p;
        changed.baseFortifications.instances.at(id).durability = 29;
        const bool selected = id == plan.participatingFortifications[0] || id == plan.participatingFortifications[1];
        EXPECT_EQ(queryBaseAutoDefense(changed, content).fortificationDiscount, selected ? 1U : 2U);
    }
}

TEST(BaseAutoFortificationTest, ThresholdAndFloorNeverChargeUnusedWood) {
    auto p = prepared();
    const auto selected = queryBaseAutoDefense(p, content).participatingFortifications;
    for (auto id : selected) p.baseFortifications.instances.at(id).durability = 30;
    EXPECT_EQ(queryBaseAutoDefense(p, content).fortificationDiscount, 2U);
    p.baseMorale.tier = BaseMoraleTier::High;
    EXPECT_EQ(queryBaseAutoDefense(p, content).requiredSecurity, 8U);
    EXPECT_EQ(queryBaseAutoDefense(p, content).fortificationDiscount, 0U);
    const auto owners = p.baseFortifications;
    ASSERT_TRUE(executeBaseAutoDefense(p, content, {p.revision, "floor"}).succeeded);
    EXPECT_EQ(p.baseFortifications.instances, owners.instances);
}

TEST(BaseAutoFortificationTest, OnePointAboveFloorConsumesOnlyOneOfTwoEligibleOwners) {
    auto p = prepared();
    p.baseMorale.tier = BaseMoraleTier::Stable;
    p.basePopulation = BasePopulationState{4, 10};
    auto withoutWood = p;
    withoutWood.baseDefenseWarning.reset();
    ASSERT_EQ(queryBaseAutoDefense(withoutWood, content).requiredSecurity, 9U);
    ASSERT_TRUE(validateProfileState(p, content).valid);
    const auto plan = queryBaseAutoDefense(p, content);
    ASSERT_EQ(plan.requiredSecurity, 8U);
    ASSERT_EQ(plan.fortificationDiscount, 1U);
    const auto owners = p.baseFortifications.instances;
    ASSERT_TRUE(executeBaseAutoDefense(p, content, {p.revision, "one"}).succeeded);
    for (const auto &[id, record] : owners)
        EXPECT_EQ(p.baseFortifications.instances.at(id).durability,
                  record.durability - (id == plan.participatingFortifications[0] ? 30 : 0));
}

TEST(BaseAutoFortificationTest, SuccessAndSoftFailurePayExactlyOnceAndKeepPersonalAssets) {
    for (bool enough : {false, true}) {
        auto p = prepared();
        const auto initial = queryBaseAutoDefense(p, content);
        for (auto id : initial.participatingFortifications) p.baseFortifications.instances.at(id).durability = 30;
        p.baseResources.pool.security = enough ? initial.requiredSecurity : 0;
        const auto plan = queryBaseAutoDefense(p, content);
        const auto owners = p.baseFortifications;
        const auto assets = serializeProfileEnvelope(p, content.contentVersion());
        const auto receipt = executeBaseAutoDefense(p, content, {p.revision, "auto"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        EXPECT_EQ(receipt.securitySpent, enough ? plan.requiredSecurity : 0U);
        EXPECT_EQ(receipt.outcome, enough ? BaseSiegeOutcome::Defended : BaseSiegeOutcome::SoftFailure);
        for (const auto &[id, record] : owners.instances) {
            const bool selected = id == plan.participatingFortifications[0] || id == plan.participatingFortifications[1];
            EXPECT_EQ(p.baseFortifications.instances.at(id).durability, record.durability - (selected ? 30 : 0));
            EXPECT_EQ(p.baseFortifications.instances.at(id).slot, record.slot);
        }
        auto original = deserializeProfileEnvelope(assets, content);
        ASSERT_TRUE(original.profile);
        original.profile->baseFortifications = p.baseFortifications;
        // Registry equality is checked via restoring every non-asset field onto
        // the result: no new personal debit or location change is permitted.
        auto assetOnly = *original.profile;
        assetOnly.assets = p.assets;
        EXPECT_EQ(profileStateFingerprint(assetOnly), profileStateFingerprint(*original.profile));
        EXPECT_EQ(p.baseFortifications.nextInstanceId, owners.nextInstanceId);
        const auto after = profileStateFingerprint(p);
        EXPECT_TRUE(executeBaseAutoDefense(p, content, {p.revision, "auto"}).alreadyCommitted);
        EXPECT_TRUE(executeBaseAutoDefense(p, content, {p.revision, "different"}).alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(p), after);
        const auto disk = deserializeProfileEnvelope(serializeProfileEnvelope(p, content.contentVersion()), content);
        ASSERT_TRUE(disk.profile);
        EXPECT_EQ(profileStateFingerprint(*disk.profile), after);
    }
}

TEST(BaseAutoFortificationTest, RepairChangesEligibilityWithoutRerollAndStaleCommandsReject) {
    auto p = prepared();
    const auto plan = queryBaseAutoDefense(p, content);
    const auto hash = p.baseDefenseWarning->layout->layoutHash;
    const auto id = plan.participatingFortifications[0];
    p.baseFortifications.instances.at(id).durability = 0;
    EXPECT_EQ(queryBaseAutoDefense(p, content).fortificationDiscount, 1U);
    p.baseConstruction.materialUnits = 10;
    const auto stale = p.revision;
    ASSERT_TRUE(executeFortificationCommand(p, content, RepairFortificationCommand{id}, {p.revision, "repair"}).succeeded);
    EXPECT_EQ(p.baseDefenseWarning->layout->layoutHash, hash);
    EXPECT_EQ(queryBaseAutoDefense(p, content).participatingFortifications, plan.participatingFortifications);
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(executeBaseAutoDefense(p, content, {stale, "stale"}).succeeded);
    EXPECT_FALSE(executeBaseAutoDefense(p, content, {p.revision, ""}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseAutoFortificationTest, LegacyAndUnavailableWarningNeverGainDiscount) {
    for (bool legacy : {false, true}) {
        auto p = prepared();
        if (legacy) p.baseDefenseWarning.reset(); else p.baseDefenseWarning->layout.reset();
        const auto text = serializeProfileEnvelope(p, content.contentVersion(), legacy ? 47 : 48);
        auto loaded = deserializeProfileEnvelope(text, content);
        ASSERT_TRUE(loaded.profile);
        const auto owners = loaded.profile->baseFortifications.instances;
        EXPECT_EQ(queryBaseAutoDefense(*loaded.profile, content).fortificationDiscount, 0U);
        ASSERT_TRUE(executeBaseAutoDefense(*loaded.profile, content, {loaded.profile->revision, "old"}).succeeded);
        EXPECT_EQ(loaded.profile->baseFortifications.instances, owners);
    }
}

TEST(BaseAutoFortificationTest, CorruptWarningRejectedBeforeAnyWearOrSecurityDebit) {
    auto p = prepared();
    ++p.baseDefenseWarning->layout->layoutHash;
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(executeBaseAutoDefense(p, content, {p.revision, "bad"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseAutoFortificationTest, RealTimeAndAutomaticAreMutuallyExclusive) {
    auto p = prepared();
    auto s = *p.baseDefenseWarning->layout;
    ASSERT_TRUE(executeBaseRealtimeDefenseStart(p, content, s, {p.revision, "manual"}).succeeded);
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(executeBaseAutoDefense(p, content, {p.revision, "auto"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
    ASSERT_TRUE(executeBaseRealtimeDefenseSettlement(p, content, s.eventId, BaseDefenseEndReason::Abandoned,
                                                   {p.revision, "end"}).succeeded);
    const auto after = profileStateFingerprint(p);
    EXPECT_TRUE(executeBaseAutoDefense(p, content, {p.revision, "again"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p), after);
}

TEST(BaseAutoFortificationTest, SaveFailureRetryAndReloadCommitOneWholeOutcome) {
    auto p = prepared();
    Store store;
    SaveRepository repository{store.path};
    ASSERT_TRUE(repository.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(store.path);
    ASSERT_TRUE(flow.continueGame());
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    const auto plan = flow.gameSession().baseAutoDefensePlan();
    const auto obstruction = store.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    EXPECT_FALSE(flow.gameSession().executeBaseAutoDefense("auto").succeeded);
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    ASSERT_TRUE(flow.gameSession().executeBaseAutoDefense("auto").succeeded);
    const auto disk = repository.load(content);
    ASSERT_TRUE(disk.profile);
    EXPECT_EQ(profileStateFingerprint(*disk.profile), profileStateFingerprint(flow.gameSession().profile()));
    for (auto id : plan.participatingFortifications) EXPECT_EQ(disk.profile->baseFortifications.instances.at(id).durability, 90U);
    GameFlow reload;
    reload.configurePersistence(store.path);
    ASSERT_TRUE(reload.continueGame());
    const auto after = profileStateFingerprint(reload.gameSession().profile());
    EXPECT_TRUE(reload.gameSession().executeBaseAutoDefense("retry").alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(reload.gameSession().profile()), after);
}

TEST(BaseAutoFortificationTest, SavedPresetUsesSamePlanAtDeadlineAndCannotRepeat) {
    auto p = prepared();
    p.baseSiege.autoDefensePresetSaved = true;
    const auto plan = queryBaseAutoDefense(p, content);
    Store store;
    ASSERT_TRUE(SaveRepository{store.path}.save(p, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(store.path);
    ASSERT_TRUE(flow.continueGame());
    flow.gameSession().advanceBaseWorldClock(181);
    EXPECT_FALSE(flow.gameSession().profile().baseSiege.warningActive);
    for (auto id : plan.participatingFortifications)
        EXPECT_EQ(flow.gameSession().profile().baseFortifications.instances.at(id).durability, 90U);
    const auto owners = flow.gameSession().profile().baseFortifications.instances;
    flow.gameSession().advanceBaseWorldClock(181);
    EXPECT_EQ(flow.gameSession().profile().baseFortifications.instances, owners);
}
