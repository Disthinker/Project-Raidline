#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "alpha_content_ids.h"
#include "raid_corpse_search.h"
#include "raid_lifecycle.h"

namespace
{
using Json = nlohmann::json;
const MapDefinitionId frontier{"map.raid.frontier_exchange"};

Json &runnerDefinition(Json &root)
{
    for (auto &entry : root.at("enemy_combat_definitions"))
        if (entry.at("id") == "enemy.infected.runner") return entry;
    throw std::logic_error{"missing runner definition in test content"};
}

Json &runnerEncounter(Json &root)
{
    for (auto &map : root.at("maps"))
        if (map.at("id").get<std::string>() == frontier.value())
            for (auto &group : map.at("procedural_outdoor").at("encounter_archetypes"))
                if (group.at("id") == "encounter.frontier.route_ambush") return group;
    throw std::logic_error{"missing runner encounter in test content"};
}

ProfileState deployed(const ContentRegistry &content, std::uint64_t seed = 4107U,
    const MapDefinitionId &map = frontier)
{
    auto profile = makeNewAlphaProfile("runner-domain", content);
    const auto receipt = executeDeploy(profile, content,
        DeployCommand{"runner-raid", "runner-settlement", seed, map},
        {profile.revision, "runner-deploy"});
    if (!receipt.succeeded) throw std::runtime_error{receipt.message};
    return profile;
}

std::size_t firstRunner(const PendingRaidSnapshot &raid)
{
    const auto found = std::find_if(raid.enemies.begin(), raid.enemies.end(),
        [](const auto &enemy) { return isRunnerEnemyVariant(enemy.variant); });
    if (found == raid.enemies.end()) throw std::logic_error{"test Raid has no runner"};
    return static_cast<std::size_t>(found - raid.enemies.begin());
}
}

TEST(RunnerInfectedDomainTest, PublishedContentDefinesAnIsolatedAmbushReplacement)
{
    const auto &content = publishedContentRegistry();
    EXPECT_EQ(content.contentVersion(), "raid-runner-infected-content-73");
    const auto &definition = content.enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.runner"});
    EXPECT_EQ(definition.maximumHealth, 8);
    EXPECT_FALSE(definition.torsoArmor);
    ASSERT_TRUE(definition.movement);
    EXPECT_EQ(*definition.movement, runnerEnemyVariantSnapshot().movement);
    std::size_t references{};
    for (const auto &map : content.maps())
        for (const auto &group : map.proceduralOutdoor.encounterArchetypes)
            if (group.runnerMemberDefinition)
            {
                ++references;
                EXPECT_EQ(map.id, frontier);
                EXPECT_EQ(group.id, "encounter.frontier.route_ambush");
                EXPECT_EQ(group.kind, RaidEncounterKind::Ambush);
                EXPECT_EQ(group.minimumGroups, 2U);
                EXPECT_EQ(group.maximumGroups, 3U);
                EXPECT_EQ(group.minimumMembers, 3U);
                EXPECT_EQ(group.maximumMembers, 5U);
                EXPECT_FALSE(group.armoredMemberDefinition);
            }
    EXPECT_EQ(references, 1U);
}

TEST(RunnerInfectedDomainTest, ContentRejectsUnversionedVitalsMovementAndAttackChanges)
{
    const auto good = Json::parse(publishedContentJson());
    for (const auto hp : {0, 7, 12})
    {
        auto bad = good;
        runnerDefinition(bad)["maximum_health"] = hp;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    }
    for (const auto speed : {0.0F, 72.0F, 91.0F})
    {
        auto bad = good;
        runnerDefinition(bad)["movement"]["normal_move_speed"] = speed;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    }
    for (const auto speed : {0.0F, 135.0F, 271.0F})
    {
        auto bad = good;
        runnerDefinition(bad)["movement"]["visible_pursuit_move_speed"] = speed;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    }
    auto bad = good;
    runnerDefinition(bad).erase("movement");
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerDefinition(bad)["movement"]["visible_pursuit_move_speed"] = nullptr;
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerDefinition(bad)["movement"]["attack_profile"] = "enemy.attack.runner-new";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerDefinition(bad)["torso_armor"] = good.at("enemy_combat_definitions").at(1).at("torso_armor");
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
}

TEST(RunnerInfectedDomainTest, ContentRejectsMissingReferenceAndOtherEncounterPolicies)
{
    const auto good = Json::parse(publishedContentJson());
    auto bad = good;
    runnerEncounter(bad).erase("runner_member_definition");
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerEncounter(bad)["runner_member_definition"] = "enemy.infected.basic";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerEncounter(bad)["id"] = "encounter.frontier.other_ambush";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    runnerEncounter(bad)["minimum_groups"] = 1;
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    bad = good;
    for (auto &map : bad.at("maps"))
        if (map.at("id").get<std::string>() == frontier.value())
            map["procedural_outdoor"]["encounter_archetypes"][0]["runner_member_definition"] = "enemy.infected.runner";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
}

TEST(RunnerInfectedDomainTest, ReplacementPreservesAllOriginalRandomStreamsAssetsAndCorpseBindings)
{
    auto legacyJson = Json::parse(publishedContentJson());
    legacyJson["content_version"] = "raid-fragmentation-grenade-content-72";
    const auto legacyContent = ContentRegistry::fromJson(legacyJson.dump());
    const auto &content = publishedContentRegistry();
    for (std::uint64_t seed = 1U; seed <= 8U; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto old = deployed(legacyContent, seed);
        auto current = deployed(content, seed);
        const auto &before = *old.pendingRaid;
        auto &after = *current.pendingRaid;
        ASSERT_EQ(after.rulesVersion, kRaidRunnerRulesVersion);
        ASSERT_EQ(before.rulesVersion, kRaidCorpseRulesVersion);
        ASSERT_EQ(after.enemies.size(), before.enemies.size());
        EXPECT_EQ(after.spatialLayout, before.spatialLayout);
        EXPECT_EQ(after.encounterGroups, before.encounterGroups);
        EXPECT_EQ(after.loot, before.loot);
        EXPECT_EQ(after.highRiskCrisis, before.highRiskCrisis);
        EXPECT_EQ(current.assets.nextAssetId(), old.assets.nextAssetId());
        std::size_t runners{}, guards{};
        for (const auto &group : after.encounterGroups)
        {
            const bool ambush = group.definitionId == "encounter.frontier.route_ambush";
            for (std::size_t member{}; member < group.memberEnemyIndices.size(); ++member)
            {
                const auto &enemy = after.enemies.at(group.memberEnemyIndices[member]);
                ASSERT_TRUE(enemy.variant);
                EXPECT_EQ(isRunnerEnemyVariant(enemy.variant), ambush && member == 0U);
            }
        }
        for (std::size_t index{}; index < after.enemies.size(); ++index)
        {
            auto &enemy = after.enemies[index];
            ASSERT_TRUE(enemy.variant);
            EXPECT_TRUE(validEnemyVariantSnapshot(*enemy.variant));
            if (isRunnerEnemyVariant(enemy.variant))
            {
                ++runners;
                EXPECT_EQ(enemy.maximumHealth, 8);
                EXPECT_FALSE(enemy.torsoArmor);
                EXPECT_TRUE(enemy.sourceEnemyId.empty());
                EXPECT_EQ(enemy.spaceId, outdoorRaidSpaceId());
                enemy.maximumHealth = before.enemies[index].maximumHealth;
            }
            else if (enemy.variant->kind == EnemyVariantKind::Armored)
            {
                ++guards;
                EXPECT_EQ(enemy.torsoArmor, before.enemies[index].torsoArmor);
                EXPECT_FALSE(enemy.sourceEnemyId.empty());
            }
            else EXPECT_FALSE(enemy.torsoArmor);
            enemy.variant.reset();
        }
        EXPECT_GE(runners, 2U);
        EXPECT_LE(runners, 3U);
        EXPECT_EQ(guards, 3U);
        EXPECT_EQ(after.enemies, before.enemies);
        after.rulesVersion = before.rulesVersion;
        // Comparison normalizes only the approved versioned replacement.
        // Equality of the complete Profile hash also covers owners and IDs.
        EXPECT_EQ(profileStateFingerprint(current), profileStateFingerprint(old));
        EXPECT_TRUE(validateProfileState(current, content).valid);
        EXPECT_TRUE(validateProfileState(old, content).valid);
    }
}

TEST(RunnerInfectedDomainTest, OtherMapsAndHistoricalContentKeepLegacySnapshots)
{
    const auto &content = publishedContentRegistry();
    for (const char *map : {"map.v0.test", "map.raid.industrial", "map.raid.hospital_district"})
    {
        const auto profile = deployed(content, 9101U, MapDefinitionId{map});
        EXPECT_NE(profile.pendingRaid->rulesVersion, kRaidRunnerRulesVersion);
        for (const auto &enemy : profile.pendingRaid->enemies) EXPECT_FALSE(enemy.variant);
    }
    for (const char *version : {"base-field-medical-supplies-content-70",
         "weapon-suppressor-acoustics-content-71", "raid-fragmentation-grenade-content-72"})
    {
        auto json = Json::parse(publishedContentJson());
        json["content_version"] = version;
        const auto legacy = ContentRegistry::fromJson(json.dump());
        const auto profile = deployed(legacy);
        EXPECT_EQ(profile.pendingRaid->rulesVersion, kRaidCorpseRulesVersion);
        for (const auto &enemy : profile.pendingRaid->enemies) EXPECT_FALSE(enemy.variant);
        EXPECT_TRUE(validateProfileState(profile, content).valid);
    }
}

TEST(RunnerInfectedDomainTest, ARunnerRaidLossCreatesAnUntypedSelfRecoveryMission)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("runner-recovery", content);
    const auto rifle = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{rifle->first, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "runner-equip"}).succeeded);
    ASSERT_TRUE(executeDeploy(profile, content,
        DeployCommand{"runner-loss", "runner-loss-result", 5901U, frontier},
        {profile.revision, "runner-loss-deploy"}).succeeded);
    ASSERT_EQ(profile.pendingRaid->rulesVersion, kRaidRunnerRulesVersion);
    ASSERT_TRUE(settlePendingRaid(profile, content, "runner-loss-result",
        RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_TRUE(profile.lostRaidRecords.contains("runner-loss-result"));
    const auto recovered = executeDeploy(profile, content,
        DeployCommand{"runner-recovery", "runner-recovery-result", 5902U, frontier,
            {}, std::string{"runner-loss-result"}},
        {profile.revision, "runner-recovery-deploy"});
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    ASSERT_TRUE(profile.pendingRaid->selfRecovery);
    EXPECT_EQ(profile.pendingRaid->rulesVersion, kRaidCorpseRulesVersion);
    for (const auto &enemy : profile.pendingRaid->enemies) EXPECT_FALSE(enemy.variant);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(RunnerInfectedDomainTest, TypedRaidRejectsMissingForgedOrMovedVariants)
{
    const auto &content = publishedContentRegistry();
    const auto original = deployed(content);
    const auto index = firstRunner(*original.pendingRaid);
    auto changed = original;
    changed.pendingRaid->enemies[index].variant.reset();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].variant = ordinaryEnemyVariantSnapshot();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].maximumHealth = 12;
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].variant->movement.normalMoveSpeed = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].variant->movement.visiblePursuitMoveSpeed = 135.0F;
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].variant->movement.attackProfileVersion = 2U;
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].sourceEnemyId = "forged.armored";
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    std::swap(changed.pendingRaid->enemies[index].variant, changed.pendingRaid->enemies[index + 1U].variant);
    std::swap(changed.pendingRaid->enemies[index].maximumHealth, changed.pendingRaid->enemies[index + 1U].maximumHealth);
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->enemies[index].spaceId = RaidSpaceDefinitionId{"raid_space.forged"};
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    for (auto &group : changed.pendingRaid->encounterGroups)
        if (group.memberEnemyIndices.front() == index)
            std::swap(group.memberEnemyIndices[0], group.memberEnemyIndices[1]);
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->rulesVersion = std::string{kRaidCorpseRulesVersion};
    EXPECT_FALSE(validateProfileState(changed, content).valid);
}

TEST(RunnerInfectedDomainTest, TypedRulesRejectAllSpecialActivityMarkers)
{
    const auto &content = publishedContentRegistry();
    const auto original = deployed(content);
    auto changed = original;
    changed.pendingRaid->selfRecovery.emplace();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->outpostRestoration.emplace();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->baseSiteClearance.emplace();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
    changed = original;
    changed.pendingRaid->basePerimeterSweep.emplace();
    EXPECT_FALSE(validateProfileState(changed, content).valid);
}

TEST(RunnerInfectedDomainTest, FrozenVariantParticipatesInEqualityAndFingerprint)
{
    const auto original = deployed(publishedContentRegistry());
    const auto index = firstRunner(*original.pendingRaid);
    auto changed = original;
    changed.pendingRaid->enemies[index].variant->movement.visiblePursuitMoveSpeed = 271.0F;
    EXPECT_NE(changed.pendingRaid->enemies, original.pendingRaid->enemies);
    EXPECT_NE(profileStateFingerprint(changed), profileStateFingerprint(original));
    changed = original;
    changed.pendingRaid->enemies[index].variant->kind = EnemyVariantKind::Basic;
    EXPECT_NE(profileStateFingerprint(changed), profileStateFingerprint(original));
    changed = original;
    changed.pendingRaid->enemies[index].variant.reset();
    EXPECT_NE(profileStateFingerprint(changed), profileStateFingerprint(original));
}
