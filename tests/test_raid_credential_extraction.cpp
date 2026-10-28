#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "raid_corpse_search.h"
#include "raid_credential_extraction.h"
#include "raid_lifecycle.h"

namespace
{
const MapDefinitionId kFrontier{"map.raid.frontier_exchange"};

DeployReceipt deployCredentialRaid(
    ProfileState &profile,
    const ContentRegistry &content,
    std::uint64_t seed = 880031U,
    std::string suffix = "first")
{
    return executeDeploy(profile, content,
        DeployCommand{"credential-raid-" + suffix,
            "credential-settlement-" + suffix, seed, kFrontier},
        CommandContext{profile.revision, "credential-deploy-" + suffix});
}

RaidCredentialInvestigationFact investigationFact(const ProfileState &profile)
{
    const auto &definition = profile.pendingRaid->credentialExtraction->definition;
    return {definition.investigationPointId,
        definition.investigationSpaceId, definition.investigationPoint};
}

bool overlaps(ContentRect left, ContentRect right)
{
    return left.position.x < right.position.x + right.size.x &&
        right.position.x < left.position.x + left.size.x &&
        left.position.y < right.position.y + right.size.y &&
        right.position.y < left.position.y + left.size.y;
}

class RaidCredentialExtractionTest : public ::testing::Test
{
protected:
    const ContentRegistry &content{publishedContentRegistry()};
    ProfileState profile{makeNewAlphaProfile("credential-contract", content)};

    void SetUp() override
    {
        const auto receipt = deployCredentialRaid(profile, content);
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        ASSERT_TRUE(profile.pendingRaid);
        ASSERT_TRUE(profile.pendingRaid->credentialExtraction);
    }
};
}

TEST_F(RaidCredentialExtractionTest, DeployFreezesSeparateContractWithoutGrantingAnything)
{
    const auto &snapshot = *profile.pendingRaid->credentialExtraction;
    const auto &definition = *content.map(kFrontier).credentialExtraction;
    EXPECT_EQ(profile.pendingRaid->rulesVersion, kRaidRunnerRulesVersion);
    EXPECT_EQ(snapshot.rulesVersion, kRaidCredentialExtractionRulesVersion);
    EXPECT_EQ(snapshot.definition.permissionId, definition.permissionId);
    EXPECT_EQ(snapshot.definition.investigationSpaceId, definition.investigationSpaceId);
    EXPECT_FLOAT_EQ(snapshot.definition.investigationDurationSeconds, 2.0F);
    EXPECT_FLOAT_EQ(snapshot.definition.extractionDurationSeconds, 6.0F);
    EXPECT_FALSE(snapshot.investigationCompleted);
    EXPECT_FALSE(snapshot.unlockedAtDeploy);
    EXPECT_TRUE(profile.raidExtractionPermissions.empty());
    EXPECT_EQ(profile.raidIntelligence.counts.size(), 0U);
    EXPECT_EQ(profile.raidInteriorIntelligence.knownLayouts.size(), 0U);
}

TEST_F(RaidCredentialExtractionTest, InvestigationOnlyUpdatesPendingFactAndOneRevision)
{
    const ProfileState before = profile;
    const auto receipt = recordRaidCredentialInvestigation(
        profile, content, investigationFact(profile));
    ASSERT_TRUE(receipt.valid) << receipt.message;
    EXPECT_TRUE(profile.pendingRaid->credentialExtraction->investigationCompleted);
    EXPECT_FALSE(profile.pendingRaid->credentialExtraction->unlockedAtDeploy);
    EXPECT_TRUE(profile.raidExtractionPermissions.empty());
    EXPECT_EQ(profile.revision, before.revision + 1U);
    auto normalized = profile;
    normalized.pendingRaid->credentialExtraction->investigationCompleted = false;
    normalized.revision = before.revision;
    EXPECT_EQ(profileStateFingerprint(normalized), profileStateFingerprint(before));
}

TEST_F(RaidCredentialExtractionTest, ValidInvestigationReplayDoesNotMutateOrAdvanceRevision)
{
    const auto fact = investigationFact(profile);
    ASSERT_TRUE(recordRaidCredentialInvestigation(profile, content, fact).valid);
    const auto before = profileStateFingerprint(profile);
    EXPECT_TRUE(recordRaidCredentialInvestigation(profile, content, fact).valid);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    auto wrong = fact;
    wrong.spaceId = outdoorRaidSpaceId();
    EXPECT_FALSE(recordRaidCredentialInvestigation(profile, content, wrong).valid);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST_F(RaidCredentialExtractionTest, WrongPointWrongSpaceDistanceAndNonFiniteFactsAreRejectedAtomically)
{
    const auto valid = investigationFact(profile);
    std::vector<RaidCredentialInvestigationFact> invalid;
    auto fact = valid;
    fact.investigationPointId += ".other";
    invalid.push_back(fact);
    fact = valid;
    fact.spaceId = outdoorRaidSpaceId();
    invalid.push_back(fact);
    fact = valid;
    fact.playerPosition.x += 73.0F;
    invalid.push_back(fact);
    fact = valid;
    fact.playerPosition.x = std::numeric_limits<float>::quiet_NaN();
    invalid.push_back(fact);
    fact = valid;
    fact.playerPosition.y = std::numeric_limits<float>::infinity();
    invalid.push_back(fact);
    fact = valid;
    fact.playerPosition.x = -1.0F;
    invalid.push_back(fact);
    const auto before = profileStateFingerprint(profile);
    for (const auto &rejected : invalid)
    {
        EXPECT_FALSE(recordRaidCredentialInvestigation(profile, content, rejected).valid);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }
}

TEST_F(RaidCredentialExtractionTest, InvestigationChecksFrozenLineOfSightAndPublishedPoint)
{
    auto fact = investigationFact(profile);
    fact.playerPosition.x -= 50.0F;
    ASSERT_TRUE(queryRaidCredentialInvestigation(profile, content, fact).valid);
    auto &rooms = profile.pendingRaid->interiors;
    auto room = std::find_if(rooms.begin(), rooms.end(),
        [&](const auto &value) { return value.id == fact.spaceId; });
    ASSERT_NE(room, rooms.end());
    room->ballisticBlockers.push_back({
        {fact.playerPosition.x + 20.0F, fact.playerPosition.y - 10.0F}, {10.0F, 20.0F}});
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(queryRaidCredentialInvestigation(profile, content, fact).valid);
    EXPECT_FALSE(recordRaidCredentialInvestigation(profile, content, fact).valid);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    profile.pendingRaid->credentialExtraction->definition.investigationPoint.x += 1.0F;
    EXPECT_FALSE(queryRaidCredentialInvestigation(profile, content, fact).valid);
}

TEST_F(RaidCredentialExtractionTest, DeadOrRevisionExhaustedProfileCannotRecord)
{
    const auto fact = investigationFact(profile);
    profile.currentHealth = 0;
    auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(recordRaidCredentialInvestigation(profile, content, fact).valid);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    profile.currentHealth = 100;
    profile.revision = std::numeric_limits<ProfileRevision>::max();
    before = profileStateFingerprint(profile);
    EXPECT_FALSE(recordRaidCredentialInvestigation(profile, content, fact).valid);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST_F(RaidCredentialExtractionTest, SuccessfulSettlementRegistersExactlyOnceAndNextDeployFreezesPermission)
{
    const auto permission = profile.pendingRaid->credentialExtraction->definition.permissionId;
    ASSERT_TRUE(recordRaidCredentialInvestigation(profile, content, investigationFact(profile)).valid);
    const auto settlement = profile.pendingRaid->settlementId;
    auto receipt = settlePendingRaid(profile, content, settlement, RaidResultOutcome::Extracted);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.lastRaidResult);
    EXPECT_EQ(profile.raidExtractionPermissions,
        std::set<RaidExtractionPermissionDefinitionId>{permission});
    EXPECT_EQ(profile.lastRaidResult->unlockedExtractionPermissions,
        std::vector<RaidExtractionPermissionDefinitionId>{permission});
    const auto committed = profileStateFingerprint(profile);
    receipt = settlePendingRaid(profile, content, settlement, RaidResultOutcome::Extracted);
    EXPECT_TRUE(receipt.succeeded);
    EXPECT_TRUE(receipt.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
    ASSERT_TRUE(deployCredentialRaid(profile, content, 880032U, "second").succeeded);
    ASSERT_TRUE(profile.pendingRaid->credentialExtraction);
    EXPECT_TRUE(profile.pendingRaid->credentialExtraction->unlockedAtDeploy);
    EXPECT_FALSE(profile.pendingRaid->credentialExtraction->investigationCompleted);
    ASSERT_TRUE(recordRaidCredentialInvestigation(profile, content, investigationFact(profile)).valid);
    ASSERT_TRUE(settlePendingRaid(profile, content,
        profile.pendingRaid->settlementId, RaidResultOutcome::Extracted).succeeded);
    EXPECT_TRUE(profile.lastRaidResult->unlockedExtractionPermissions.empty());
    EXPECT_EQ(profile.raidExtractionPermissions.size(), 1U);
}

TEST_F(RaidCredentialExtractionTest, SuccessfulExitWithoutInvestigationDoesNotGrantPermission)
{
    const auto receipt = settlePendingRaid(profile, content,
        profile.pendingRaid->settlementId, RaidResultOutcome::Extracted);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(profile.raidExtractionPermissions.empty());
    EXPECT_TRUE(profile.lastRaidResult->unlockedExtractionPermissions.empty());
}

TEST_F(RaidCredentialExtractionTest, DeathAndActiveQuitDiscardUnbankedInvestigation)
{
    ASSERT_TRUE(recordRaidCredentialInvestigation(profile, content, investigationFact(profile)).valid);
    for (const auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto candidate = profile;
        const auto receipt = settlePendingRaid(candidate, content,
            candidate.pendingRaid->settlementId, outcome);
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        EXPECT_TRUE(candidate.raidExtractionPermissions.empty());
        EXPECT_TRUE(candidate.lastRaidResult->unlockedExtractionPermissions.empty());
    }
}

TEST_F(RaidCredentialExtractionTest, AbnormalRecoveryDoesNotGrantAndExistingPermissionSurvivesFailures)
{
    ASSERT_TRUE(recordRaidCredentialInvestigation(profile, content, investigationFact(profile)).valid);
    auto recovered = profile;
    const auto rollback = rollbackPendingRaidToBase(recovered, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_TRUE(recovered.raidExtractionPermissions.empty());
    const auto permission = profile.pendingRaid->credentialExtraction->definition.permissionId;
    ASSERT_TRUE(settlePendingRaid(profile, content,
        profile.pendingRaid->settlementId, RaidResultOutcome::Extracted).succeeded);
    ASSERT_TRUE(deployCredentialRaid(profile, content, 880033U, "owned").succeeded);
    for (const auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto candidate = profile;
        ASSERT_TRUE(settlePendingRaid(candidate, content,
            candidate.pendingRaid->settlementId, outcome).succeeded);
        EXPECT_TRUE(candidate.raidExtractionPermissions.contains(permission));
        EXPECT_TRUE(candidate.lastRaidResult->unlockedExtractionPermissions.empty());
    }
    EXPECT_TRUE(rollbackPendingRaidToBase(profile, content).succeeded);
    EXPECT_TRUE(profile.raidExtractionPermissions.contains(permission));
}

TEST_F(RaidCredentialExtractionTest, ForgedSnapshotCannotRegisterPermissionDuringSettlement)
{
    profile.pendingRaid->credentialExtraction->investigationCompleted = true;
    profile.pendingRaid->credentialExtraction->definition.investigationPoint.x += 1.0F;
    const auto before = profileStateFingerprint(profile);
    const auto receipt = settlePendingRaid(profile, content,
        profile.pendingRaid->settlementId, RaidResultOutcome::Extracted);
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(RaidCredentialCompatibilityTest, OldPendingRaidRemainsUntouchedAndSettlesWithoutPermission)
{
    auto json = nlohmann::json::parse(publishedContentJson());
    for (auto &map : json["maps"])
        map.erase("credential_extraction");
    const auto oldContent = ContentRegistry::fromJson(json.dump());
    auto profile = makeNewAlphaProfile("credential-old-pending", oldContent);
    ASSERT_TRUE(deployCredentialRaid(profile, oldContent).succeeded);
    ASSERT_TRUE(profile.pendingRaid);
    EXPECT_FALSE(profile.pendingRaid->credentialExtraction);
    EXPECT_EQ(findRaidAnchorPlacement(profile.pendingRaid->spatialLayout,
        kRaidAnchorCredentialExtraction), nullptr);
    const auto before = profileStateFingerprint(profile);
    const auto valid = validateProfileState(profile, publishedContentRegistry());
    EXPECT_TRUE(valid.valid) << valid.message;
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto receipt = settlePendingRaid(profile, publishedContentRegistry(),
        profile.pendingRaid->settlementId, RaidResultOutcome::Extracted);
    EXPECT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(profile.raidExtractionPermissions.empty());
}

TEST(RaidCredentialCompatibilityTest, IndependentAnchorKeepsExistingFrozenRollsAndPositionsAcrossSeeds)
{
    const auto &content = publishedContentRegistry();
    auto json = nlohmann::json::parse(publishedContentJson());
    for (auto &map : json["maps"])
        map.erase("credential_extraction");
    const auto oldContent = ContentRegistry::fromJson(json.dump());
    for (std::uint64_t seed = 1U; seed <= 8U; ++seed)
    {
        SCOPED_TRACE(seed);
        auto current = makeNewAlphaProfile("credential-roll-preservation", content);
        auto old = makeNewAlphaProfile("credential-roll-preservation", oldContent);
        const auto receipt = deployCredentialRaid(current, content, seed);
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        ASSERT_TRUE(deployCredentialRaid(old, oldContent, seed).succeeded);
        const auto &raid = *current.pendingRaid;
        const auto &previous = *old.pendingRaid;
        ASSERT_TRUE(raid.credentialExtraction);
        EXPECT_EQ(raid.rulesVersion, previous.rulesVersion);
        EXPECT_EQ(raid.loot, previous.loot);
        EXPECT_EQ(raid.enemies, previous.enemies);
        EXPECT_EQ(current.assets.nextAssetId(), old.assets.nextAssetId());
        EXPECT_EQ(current.assets.records().size(), old.assets.records().size());
        const auto *exit = findRaidAnchorPlacement(raid.spatialLayout,
            kRaidAnchorCredentialExtraction);
        ASSERT_NE(exit, nullptr);
        EXPECT_EQ(exit->bounds, raid.credentialExtraction->definition.extractionPoint);
        for (const auto &anchor : previous.spatialLayout.anchorPlacements)
        {
            const auto *retained = findRaidAnchorPlacement(raid.spatialLayout, anchor.id);
            ASSERT_NE(retained, nullptr);
            EXPECT_EQ(retained->bounds, anchor.bounds);
            EXPECT_FALSE(overlaps(exit->bounds, anchor.bounds));
        }
        for (const auto &blocker : raid.spatialLayout.ballisticBlockers)
            EXPECT_FALSE(overlaps(exit->bounds, blocker));
        EXPECT_TRUE(validateProfileState(current, content).valid);
    }
}

TEST(RaidCredentialCompatibilityTest, OtherMapsDoNotGainCredentialInvestigation)
{
    const auto &content = publishedContentRegistry();
    for (const auto &map : {MapDefinitionId{"map.v0.test"},
                           MapDefinitionId{"map.raid.hospital_district"}})
    {
        auto profile = makeNewAlphaProfile("credential-map-isolation", content);
        const auto receipt = executeDeploy(profile, content,
            DeployCommand{"other-map-raid", "other-map-settlement", 880034U, map},
            CommandContext{profile.revision, "other-map-deploy"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        EXPECT_FALSE(profile.pendingRaid->credentialExtraction);
        EXPECT_EQ(findRaidAnchorPlacement(profile.pendingRaid->spatialLayout,
            kRaidAnchorCredentialExtraction), nullptr);
    }
}

TEST(RaidCredentialCompatibilityTest, SelfRecoveryOnFrontierKeepsItsOriginalMissionContract)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("credential-self-recovery", content);
    const auto rifle = std::find_if(profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{rifle->first, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "credential-recovery-equip"}).succeeded);
    const auto permission = content.map(kFrontier).credentialExtraction->permissionId;
    profile.raidExtractionPermissions.insert(permission);
    ASSERT_TRUE(deployCredentialRaid(profile, content).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content,
        "credential-settlement-first", RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_TRUE(profile.lostRaidRecords.contains("credential-settlement-first"));
    const auto receipt = executeDeploy(profile, content,
        DeployCommand{"credential-recovery-raid", "credential-recovery-settlement",
            880035U, kFrontier, {}, "credential-settlement-first"},
        CommandContext{profile.revision, "credential-recovery-deploy"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid->selfRecovery);
    EXPECT_FALSE(profile.pendingRaid->credentialExtraction);
    EXPECT_EQ(findRaidAnchorPlacement(profile.pendingRaid->spatialLayout,
        kRaidAnchorCredentialExtraction), nullptr);
    EXPECT_TRUE(profile.raidExtractionPermissions.contains(permission));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
