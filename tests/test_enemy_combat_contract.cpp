#include "base_world.h"
#include "content_registry.h"
#include "hit_feedback_presentation.h"
#include "home_perimeter_domain.h"
#include "save_repository.h"

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

namespace
{
using Json = nlohmann::json;
Json contentJson() { return Json::parse(publishedContentJson()); }
const RegionalBaseSiteDefinitionId site{"regional_base_site.greyline_yard"};
HomePerimeterGenerationContext context()
{
    return {site, {12800, 7200}, {{5200, 2800}, {1600, 1100}}, {}};
}
BaseDefenseSnapshot defenseSeed()
{
    BaseDefenseSnapshot s;
    s.eventId = "combat-contract"; s.siegeSequence = 1; s.seed = 12345;
    s.frozenPopulation = 8;
    return s;
}
void useLegacyInlineSpawns(Json &value)
{
    if (value.is_object() && value.contains("enemy_combat_definition"))
    {
        value.erase("enemy_combat_definition");
        value["maximum_health"] = 12;
    }
    if (value.is_object() || value.is_array())
        for (auto &child : value) useLegacyInlineSpawns(child);
}
}

TEST(EnemyCombatDefinitionTest, OneDefinitionFeedsDailyDefenseOutdoorInteriorAndPressure)
{
    for (const int health : {12, 21})
    {
        auto json = contentJson();
        json["enemy_combat_definitions"][0]["maximum_health"] = health;
        const auto content = ContentRegistry::fromJson(json.dump());
        const auto &definition = content.enemyCombatDefinition(ordinaryInfectedDefinitionId());
        EXPECT_EQ(definition.maximumHealth, health);
        auto profile = makeNewAlphaProfile("combat-generation", content);
        ASSERT_TRUE(ensureHomePerimeterSnapshot(profile, content, context(),
            {profile.revision, "generate"}).succeeded);
        for (const auto &enemy : profile.homePerimeter.sites.at(site).enemies)
        {
            EXPECT_EQ(enemy.maximumHealth, health);
            EXPECT_EQ(enemy.health, health);
        }
        BaseWorld base;
        const auto prepared = base.prepareBaseDefenseSnapshot(defenseSeed(), definition);
        ASSERT_TRUE(prepared);
        for (const auto &wave : prepared->wavePlans) EXPECT_EQ(wave.enemyMaxHealth, health);
        for (const auto &deployment : content.enemyDeployments())
            for (const auto &enemy : deployment.enemies) EXPECT_EQ(enemy.maximumHealth, health);
        for (const auto &map : content.maps())
        {
            for (const auto &enemy : map.highRisk.pressureSpawns) EXPECT_EQ(enemy.maximumHealth, health);
            for (const auto &interior : map.interiors)
                for (const auto &enemy : interior.enemies) EXPECT_EQ(enemy.maximumHealth, health);
        }
    }
}

TEST(EnemyCombatDefinitionTest, RejectsDuplicateUnknownInvalidAndAmbiguousDefinitions)
{
    const auto valid = contentJson();
    auto json = valid;
    json["enemy_combat_definitions"].push_back(json["enemy_combat_definitions"][0]);
    EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
    for (const int invalid : {0, -1})
    {
        json = valid; json["enemy_combat_definitions"][0]["maximum_health"] = invalid;
        EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
    }
    json = valid; json.erase("enemy_combat_definitions");
    EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
    json = valid; json["enemy_deployments"][0]["enemies"][0]["enemy_combat_definition"] = "enemy.unknown";
    EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
    json = valid; json["enemy_deployments"][0]["enemies"][0]["maximum_health"] = 3;
    EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
    json["enemy_deployments"][0]["enemies"][0].erase("enemy_combat_definition");
    EXPECT_THROW(ContentRegistry::fromJson(json.dump()), ContentRegistryError);
}

TEST(EnemyCombatDefinitionTest, DistinctDefinitionMayHaveDistinctHealthAndLegacyInlineStillLoads)
{
    auto json = contentJson();
    json["enemy_combat_definitions"].push_back({{"id", "enemy.test.heavy"}, {"maximum_health", 27}});
    json["enemy_deployments"][0]["enemies"][0]["enemy_combat_definition"] = "enemy.test.heavy";
    const auto custom = ContentRegistry::fromJson(json.dump());
    EXPECT_EQ(custom.enemyDeployments().front().enemies.front().maximumHealth, 27);
    json = contentJson();
    useLegacyInlineSpawns(json);
    json.erase("enemy_combat_definitions");
    json["content_version"] = "base-wishes-resource-tradeoff-content-59";
    json["enemy_deployments"][0]["enemies"][0]["maximum_health"] = 3;
    EXPECT_EQ(ContentRegistry::fromJson(json.dump()).enemyDeployments().front().enemies.front().maximumHealth, 3);
}

TEST(EnemyCombatCompatibilityTest, OldPerimeterDeadAndInjuredSnapshotsAreNotRebalancedOrRerolled)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("old-perimeter", content);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(profile, content, context(),
        {profile.revision, "generate"}).succeeded);
    auto &saved = profile.homePerimeter.sites.at(site);
    ASSERT_GE(saved.enemies.size(), 2U);
    for (auto &enemy : saved.enemies) enemy.maximumHealth = enemy.health = 3;
    saved.enemies[0].health = 0;
    saved.enemies[1].health = 1;
    const auto before = saved;
    auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile,
        "base-wishes-resource-tradeoff-content-59"), content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->homePerimeter.sites.at(site), before);
    const auto fingerprint = profileStateFingerprint(*loaded.profile);
    ASSERT_TRUE(ensureHomePerimeterSnapshot(*loaded.profile, content, context(),
        {loaded.profile->revision, "same-cycle"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    BaseWorld world;
    world.configureHomePerimeter(&loaded.profile->homePerimeter.sites.at(site));
    ASSERT_EQ(world.perimeterEnemies().size(), before.enemies.size() - 1);
    EXPECT_EQ(world.perimeterEnemies().front().health(), 1);
    EXPECT_EQ(world.perimeterEnemies().front().maxHealth(), 3);
    world.configureHomePerimeter(&before);
    EXPECT_EQ(world.perimeterEnemies().front().health(), 1);
    const auto twice = deserializeProfileEnvelope(serializeProfileEnvelope(*loaded.profile,
        content.contentVersion()), content);
    ASSERT_TRUE(twice.profile) << twice.message;
    EXPECT_EQ(twice.profile->homePerimeter.sites.at(site), before);
}

TEST(EnemyCombatCompatibilityTest, ExistingDefenseFreezesHealthAndInjuryAcrossResume)
{
    BaseWorld world;
    auto prepared = world.prepareBaseDefenseSnapshot(defenseSeed(),
        EnemyCombatDefinition{ordinaryInfectedDefinitionId(), 100});
    ASSERT_TRUE(prepared);
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(*prepared, {}));
    WorldShootingRuntime shooting;
    runtime.advance({}, 0.001F, prepared->playerPosition, {40, 52}, false, shooting, {});
    auto checkpoint = runtime.checkpoint(shooting);
    ASSERT_FALSE(checkpoint.enemies.empty());
    checkpoint.enemies.front().health = 17;
    BaseDefenseRuntime restored;
    ASSERT_TRUE(restored.resume(checkpoint, {}));
    EXPECT_EQ(restored.enemies().front().health(), 17);
    EXPECT_EQ(restored.enemies().front().maxHealth(), 100);
    const auto roundtrip = restored.checkpoint(shooting);
    EXPECT_EQ(roundtrip.enemies, checkpoint.enemies);
    EXPECT_EQ(baseDefenseCheckpointHash(roundtrip), baseDefenseCheckpointHash(checkpoint));
}

TEST(HitFeedbackContractTest, OnlyDomainEnemySpecialHitsActivateAndInvalidTimeCannotExtend)
{
    HitFeedbackPresentationState feedback;
    std::array<HitResult, 1> hits{};
    hits[0].region = HitRegion::Head; // Geometry alone is never sufficient.
    hits[0].targetKind = HitTargetKind::Enemy;
    feedback.consume(hits);
    EXPECT_EQ(feedback.snapshot(), HitFeedbackPresentationSnapshot{});
    hits[0].semantic = HitSemantic::WeakPoint;
    hits[0].targetKind = HitTargetKind::Obstacle;
    feedback.consume(hits);
    EXPECT_EQ(feedback.snapshot(), HitFeedbackPresentationSnapshot{});
    hits[0].targetKind = HitTargetKind::Enemy;
    feedback.consume(hits);
    EXPECT_EQ(feedback.snapshot().semantic, HitSemantic::WeakPoint);
    feedback.update(0.08F);
    const auto before = feedback.snapshot();
    feedback.update(-1); feedback.update(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(feedback.snapshot(), before);
    hits[0].semantic = HitSemantic::Normal;
    feedback.consume(hits);
    EXPECT_EQ(feedback.snapshot(), before); // normal hits do not refresh the X
    feedback.update(1);
    EXPECT_EQ(feedback.snapshot(), HitFeedbackPresentationSnapshot{});
}
