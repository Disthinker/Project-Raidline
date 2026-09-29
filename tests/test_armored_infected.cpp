#include "content_registry.h"
#include "game_session.h"
#include "hit_resolution.h"
#include "hit_feedback_presentation.h"
#include "combat_runtime_checkpoint_json.h"
#include "raid_lifecycle.h"
#include "save_repository.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>

struct EnemyLifecycleTestAccess {
    static EnemyLifecycle &actors(GameplayWorld &world) { return world.activeEnemies(); }
    static WorldShootingRuntime &shooting(GameplayWorld &world) { return world.shooting_; }
};
namespace {
const auto &content() { return publishedContentRegistry(); }
EnemyArmorState armor() { return *content().enemyCombatDefinition(EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor; }
EnemyRoster<> roster() {
    EnemyRoster<> enemies;
    enemies.spawn(Enemy{{100,100},{50,100},{},100,1,armor()});
    return enemies;
}
ShotCollisionCandidate shot(ShotId id=1, int penetration=4, float y=150, bool headIntent=false) {
    return {id,{110,y},{110,y},1,6,headIntent ? std::optional{ShotAimIntent{1,HitRegion::Head,false}} : std::nullopt,penetration};
}
ProfileState deploy(const ContentRegistry &registry, std::uint64_t seed, const char *map="map.raid.frontier_exchange") {
    auto p=makeNewAlphaProfile("armored",registry);
    auto r=executeDeploy(p,registry,DeployCommand{"raid","settle",seed,MapDefinitionId{map}},{p.revision,"deploy"});
    if (!r.succeeded) throw std::runtime_error(r.message);
    return p;
}
}
TEST(ArmoredInfectedTest, StandardAndEnhancedTorsoShotsHaveDistinctDamage) {
    auto standard=roster(), enhanced=roster();
    const auto low=resolveShotEnemyHits({shot(1,4)},standard);
    const auto high=resolveShotEnemyHits({shot(1,7)},enhanced);
    ASSERT_EQ(low.hits.size(),1U); ASSERT_EQ(high.hits.size(),1U);
    EXPECT_EQ(low.hits[0].damageApplied,4); EXPECT_TRUE(low.hits[0].armorReducedDamage);
    EXPECT_EQ(high.hits[0].damageApplied,6); EXPECT_FALSE(high.hits[0].armorReducedDamage);
    EXPECT_EQ(standard.front().torsoArmor()->durability,3U);
    EXPECT_EQ(enhanced.front().torsoArmor()->durability,9U);
}
TEST(ArmoredInfectedTest, PhysicalHeadWithoutAimIntentDoesNotUseTorsoArmor) {
    for (bool aimed : {false,true}) {
        auto enemies=roster();
        auto result=resolveShotEnemyHits({shot(1,0,110,aimed)},enemies);
        ASSERT_EQ(result.hits.size(),1U);
        EXPECT_EQ(result.hits[0].damageApplied,aimed ? 12 : 6);
        EXPECT_EQ(result.hits[0].semantic,aimed ? HitSemantic::Headshot : HitSemantic::Normal);
        EXPECT_FALSE(result.hits[0].armorReducedDamage);
        EXPECT_EQ(enemies.front().torsoArmor()->durability,9U);
    }
    auto enemies=roster(); const auto legs=resolveShotEnemyHits({shot(1,0,190)},enemies);
    ASSERT_EQ(legs.hits.size(),1U); EXPECT_EQ(legs.hits[0].damageApplied,5);
    EXPECT_FALSE(legs.hits[0].armorReducedDamage); EXPECT_EQ(enemies.front().torsoArmor()->durability,9U);
}
TEST(ArmoredInfectedTest, DuplicateFlightIsConsumedOnceAndBrokenArmorStopsProtecting) {
    auto enemies=roster();
    auto result=resolveShotEnemyHits({shot(1),shot(1),shot(2),shot(3)},enemies);
    ASSERT_EQ(result.hits.size(),3U); EXPECT_EQ(result.consumedShotIds.size(),3U);
    EXPECT_FALSE(result.hits[0].armorBroken); EXPECT_TRUE(result.hits[1].armorBroken);
    EXPECT_EQ(result.hits[2].damageApplied,6); EXPECT_FALSE(result.hits[2].armorReducedDamage);
    EXPECT_EQ(enemies.front().health(),86); EXPECT_EQ(enemies.front().torsoArmor()->durability,0U);
}
TEST(ArmoredInfectedTest, InvalidAndBlockedShotsPreserveHealthAndArmor) {
    auto enemies=roster(); const auto before=enemies.front().checkpoint();
    EXPECT_TRUE(resolveShotEnemyHits({shot(1,-1)},enemies).hits.empty());
    auto result=resolveShotHits({shot()},enemies,{{1,{{105,140},{20,20}}}});
    ASSERT_EQ(result.hits.size(),1U); EXPECT_EQ(result.hits[0].targetKind,HitTargetKind::Obstacle);
    EXPECT_EQ(enemies.front().checkpoint(),before);
}
TEST(ArmoredInfectedTest, FrontierReplacementPreservesPopulationGeometryAndLootAcrossSeeds) {
    auto json=nlohmann::json::parse(publishedContentJson());
    for (auto &map:json["maps"]) if (map["id"]=="map.raid.frontier_exchange")
        for (auto &group:map["procedural_outdoor"]["encounter_archetypes"]) group.erase("armored_member_definition");
    const auto ordinary=ContentRegistry::fromJson(json.dump());
    for (unsigned seed=1;seed<=8;++seed) {
        const auto baseline=deploy(ordinary,seed);
        auto upgraded=deploy(content(),seed);
        auto &s=*upgraded.pendingRaid;
        EXPECT_EQ(s.enemies.size(),baseline.pendingRaid->enemies.size());
        unsigned count=0;
        for (auto &e:s.enemies) if(e.torsoArmor) { ++count; EXPECT_TRUE(e.encounterGroupInstanceId.starts_with("encounter.frontier.resource_guard")); e.torsoArmor.reset(); }
        EXPECT_EQ(count,3U); EXPECT_EQ(s.enemies,baseline.pendingRaid->enemies);
        EXPECT_EQ(s.loot,baseline.pendingRaid->loot);
    }
    for (const char *map:{"map.v0.test","map.raid.industrial","map.raid.hospital_district"}) {
        const auto p=deploy(content(),42,map);
        EXPECT_TRUE(std::none_of(p.pendingRaid->enemies.begin(),p.pendingRaid->enemies.end(),[](const auto &e){return e.torsoArmor.has_value();}));
    }
}
TEST(ArmoredInfectedTest, FrozenArmorRoundTripsAndOldSchemaDoesNotGainArmor) {
    auto p=deploy(content(),42);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,content().contentVersion()),content());
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(loaded.profile->pendingRaid->enemies,p.pendingRaid->enemies);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(p,content().contentVersion(),49)),std::invalid_argument);
    for(auto &e:p.pendingRaid->enemies) e.torsoArmor.reset();
    const auto old=deserializeProfileEnvelope(serializeProfileEnvelope(p,"weapon-components-content-63",49),content());
    ASSERT_TRUE(old.profile) << old.message;
    EXPECT_EQ(old.profile->pendingRaid->enemies,p.pendingRaid->enemies);
    p.pendingRaid->enemies.front().torsoArmor=EnemyArmorState{7,1001,10000};
    EXPECT_FALSE(validateProfileState(p,content()).valid);
}
TEST(ArmoredInfectedTest, InvalidArmorAndUnknownGuardReferencesCannotPublish) {
    const auto good=nlohmann::json::parse(publishedContentJson());
    for(const char *field:{"protection_requirement","durability","durability_loss_basis_points"}) {
        auto bad=good; bad["enemy_combat_definitions"][1]["torso_armor"][field]=0;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())),ContentRegistryError);
    }
    auto bad=good;
    for(auto &map:bad["maps"]) if(map["id"]=="map.raid.frontier_exchange")
        map["procedural_outdoor"]["encounter_archetypes"][1]["armored_member_definition"]="enemy.unknown";
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())),ContentRegistryError);
}
TEST(ArmoredInfectedTest, CheckpointPreservesPartialAndBrokenArmor) {
    auto enemies=roster();
    for(auto loss:{6U,3U}) {
        enemies.front().applyArmorLoss(loss);
        const auto snapshot=enemies.front().checkpoint();
        const nlohmann::json json=snapshot;
        auto restored=Enemy::restoreCheckpoint(json.get<EnemyRuntimeCheckpoint>());
        ASSERT_TRUE(restored); EXPECT_EQ(restored->checkpoint(),snapshot);
    }
    auto bad=enemies.front().checkpoint(); bad.torsoArmor->protectionRequirement=-1;
    EXPECT_FALSE(Enemy::restoreCheckpoint(bad));
}
TEST(ArmoredInfectedTest, FeedbackUsesResolvedFactsAndNeverAddsNormalHitCross) {
    auto enemies=roster(); const auto hit=resolveShotEnemyHits({shot()},enemies);
    HitFeedbackPresentationState feedback; feedback.consume(hit.hits);
    EXPECT_EQ(feedback.snapshot().remainingSeconds,0); EXPECT_GT(feedback.snapshot().armorFeedbackSeconds,0);
    EXPECT_FALSE(feedback.snapshot().armorBroken);
    feedback.consume(resolveShotEnemyHits({shot(2)},enemies).hits);
    EXPECT_TRUE(feedback.snapshot().armorBroken);
    feedback.update(1); EXPECT_EQ(feedback.snapshot(),HitFeedbackPresentationSnapshot{});
    feedback.consume(hit.hits); feedback.reset(); EXPECT_EQ(feedback.snapshot(),HitFeedbackPresentationSnapshot{});
}
TEST(ArmoredInfectedTest, SpaceRoundTripsKeepArmorLossAndDeathWithStableIdentity) {
    RaidWorldConfig config; config.worldSize={800,600}; config.playerSpawn={100,100}; config.extractionPoint={{650,450},{100,100}};
    EnemySpawn outside{{620,100},{50,50},12}; outside.torsoArmor=armor(); config.initialEnemies.push_back(outside);
    RaidInteriorWorldConfig inside; inside.id=RaidSpaceDefinitionId{"raid_space.test.armor"}; inside.displayName="Armor test";
    inside.worldSize={480,360}; inside.exteriorEntrance={{80,80},{100,100}}; inside.exteriorReturn={100,100};
    inside.interiorSpawn={80,80}; inside.interiorExit={{60,60},{120,120}};
    EnemySpawn guard{{300,80},{50,50},12}; guard.torsoArmor=armor(); inside.initialEnemies.push_back(guard); config.interiors.push_back(inside);
    GameplayWorld world{config};
    auto &outsideActors=EnemyLifecycleTestAccess::actors(world); const auto outsideId=outsideActors.front().combatTargetId();
    outsideActors.front().applyArmorLoss(6);
    GameplayInput portal; portal.interactJustPressed=true;
    world.update(portal,0); ASSERT_FALSE(world.inOutdoorRaidSpace());
    auto &insideActors=EnemyLifecycleTestAccess::actors(world); const auto insideId=insideActors.front().combatTargetId();
    insideActors.front().applyArmorLoss(9);
    world.update(portal,0); ASSERT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_EQ(world.enemies().front().combatTargetId(),outsideId); EXPECT_EQ(world.enemies().front().torsoArmor()->durability,3U);
    world.update(portal,0); ASSERT_FALSE(world.inOutdoorRaidSpace());
    EXPECT_EQ(world.enemies().front().combatTargetId(),insideId); EXPECT_EQ(world.enemies().front().torsoArmor()->durability,0U);
    auto &actors=EnemyLifecycleTestAccess::actors(world);
    ASSERT_TRUE(actors.front().takeDamage(100)); static_cast<void>(actors.removeDead());
    world.update(portal,0); world.update(portal,0); EXPECT_TRUE(world.enemies().empty());
}
TEST(ArmoredInfectedTest, LogicalFlightsKeepPerRoundPenetrationThroughActualWorldUpdate) {
    RaidWorldConfig config; config.worldSize={800,600}; config.playerSpawn={100,100}; config.extractionPoint={{650,450},{100,100}};
    EnemySpawn guard{{400,200},{50,100},100}; guard.torsoArmor=armor(); config.initialEnemies.push_back(guard);
    GameplayWorld world{config}; auto &shooting=EnemyLifecycleTestAccess::shooting(world);
    auto checkpoint=shooting.checkpoint();
    for(int penetration:{4,7}) {
        LogicalFlightCheckpoint f; f.id=checkpoint.nextShotId++; f.origin=f.position={415,250}; f.direction={1,0}; f.impact={515,250};
        f.speed=6000; f.extent=1; f.maximumDistance=100; f.damage=6; f.penetration=penetration; f.tracerLifetime=0.05F;
        checkpoint.flights.push_back(f);
    }
    ASSERT_TRUE(shooting.restoreCheckpoint(checkpoint));
    world.update({},0.001F); const auto &hits=world.hitResultsLastUpdate();
    ASSERT_EQ(hits.size(),2U); EXPECT_EQ(hits[0].damageApplied,4); EXPECT_EQ(hits[1].damageApplied,6);
    EXPECT_EQ(world.enemies().front().torsoArmor()->durability,3U);
    const int hp=world.enemies().front().health(); world.update({},0.001F); EXPECT_EQ(world.enemies().front().health(),hp);
}
TEST(ArmoredInfectedTest, ProductionSessionCreatesFrozenArmoredTargetsAndAbnormalExitRollsBack) {
    const auto path=std::filesystem::temp_directory_path()/("raidline-armor-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);} } cleanup{path};
    GameSession session; session.configurePersistence(path); ASSERT_TRUE(session.startNewProfile("armor-production"));
    const auto before=session.profile().assets.records().size();
    ASSERT_TRUE(session.deployAlpha(42,MapDefinitionId{"map.raid.frontier_exchange"}));
    EXPECT_EQ(std::count_if(session.world().enemies().begin(),session.world().enemies().end(),[](const auto &e){return e.torsoArmor().has_value();}),3);
    GameSession reopened; reopened.configurePersistence(path); ASSERT_TRUE(reopened.continueProfile());
    EXPECT_FALSE(reopened.profile().pendingRaid); EXPECT_EQ(reopened.profile().assets.records().size(),before);
}
