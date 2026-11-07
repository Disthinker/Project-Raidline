#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include "alpha_content_ids.h"
#include "base_world.h"

namespace
{
bool overlaps(ContentRect a,ContentRect b)
{
    return a.position.x<b.position.x+b.size.x && a.position.x+a.size.x>b.position.x &&
        a.position.y<b.position.y+b.size.y && a.position.y+a.size.y>b.position.y;
}
std::optional<Vec2> openLane(const BaseWorld &world)
{
    const auto blockers=world.basePlacementBlockers();
    const auto core=world.baseParcel();
    const ContentRect protectedArea{{core.position.x-kHomePerimeterTransitionWidth,core.position.y-kHomePerimeterTransitionWidth},
        {core.size.x+2*kHomePerimeterTransitionWidth,core.size.y+2*kHomePerimeterTransitionWidth}};
    for(float y=320;y+760<world.worldSize().y-24;y+=160)
        for(float x=320;x+1100<world.worldSize().x-24;x+=160)
        {
            const ContentRect lane{{x,y},{1100,760}};
            if(!overlaps(lane,protectedArea) && std::none_of(blockers.begin(),blockers.end(),[&](const auto &b){return overlaps(lane,b);}))
                return Vec2{x+80,y+100};
        }
    return {};
}
Vec2 playerCenter(const BaseWorld &world)
{
    return {world.playerPosition().x+world.playerSize().x*.5F,world.playerPosition().y+world.playerSize().y*.5F};
}
Vec2 enemyCenter(const Enemy &enemy)
{
    return {enemy.position().x+enemy.size().x*.5F,enemy.position().y+enemy.size().y*.5F};
}
float distance(Vec2 a,Vec2 b){return std::hypot(a.x-b.x,a.y-b.y);}
std::optional<Vec2> clearCoreApproach(const BaseWorld &world)
{
    const auto core=world.baseParcel();const auto blockers=world.basePlacementBlockers();
    for(float y=core.position.y+80;y<core.position.y+core.size.y-80;y+=80)
    {
        const ContentRect path{{core.position.x-110,y-26},{230,52}};
        if(std::none_of(blockers.begin(),blockers.end(),[&](const auto &b){return overlaps(path,b);}))return path.position;
    }
    return {};
}
HomePerimeterSiteSnapshot oneDailyEnemy(const BaseWorld &world,Vec2 center,int health=100)
{
    HomePerimeterSiteSnapshot snapshot;
    snapshot.baseSiteDefinitionId=RegionalBaseSiteDefinitionId{world.siteDefinitionId()};snapshot.cycleIndex=7;snapshot.seed=431;
    const Vec2 position{center.x-25,center.y-25};
    snapshot.enemies.push_back({17,position,position,{50,50},health,health});return snapshot;
}
BaseDefenseSnapshot defenseSeed()
{
    BaseDefenseSnapshot seed;seed.eventId="base-player-systems-defense";seed.siegeSequence=1;seed.seed=73;
    seed.frozenPopulation=8;seed.frozenMoraleTier=1;seed.frozenSiteThreat=1;return seed;
}
std::optional<BaseDefenseSnapshot> oneDefenseEnemy(BaseWorld &world,Vec2 center,int health=0)
{
    auto snapshot=world.prepareBaseDefenseSnapshot(defenseSeed(),publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    if(!snapshot || snapshot->wavePlans.empty())return {};
    const auto &wave=snapshot->wavePlans.front();
    Enemy enemy{{center.x-16,center.y-24},{32,48},{},wave.enemyMaxHealth,wave.enemyIds.front()};
    if(health>0 && health<wave.enemyMaxHealth)static_cast<void>(enemy.takeDamage(wave.enemyMaxHealth-health,EnemyImpactReaction::None));
    snapshot->enemies={enemy.checkpoint()};snapshot->spawnedEnemyCount=1;snapshot->nextSpawnDelay=1000;
    return snapshot;
}
const GrenadeUseDefinition &grenadeUse()
{
    return *publishedContentRegistry().item(ItemDefinitionId{"item.throwable.frag_grenade_basic"}).grenadeUse;
}
const NoiseDecoyUseDefinition &decoyUse(){return *publishedContentRegistry().item(alpha_content::noiseDecoy).noiseDecoyUse;}
class BasePlayerSystemsTest : public testing::Test
{
protected:
    BaseWorld world;
    Vec2 start{};
    void SetUp() override
    {
        const auto lane=openLane(world);ASSERT_TRUE(lane)<<"published Base must contain an unobstructed exterior test lane";
        start=*lane;ASSERT_TRUE(world.restorePlayerBody(start,{1,0},0,0));
        ASSERT_EQ(world.playerSafetyZone(),HomeRegionSafetyZone::Perimeter);
    }
};
}

TEST_F(BasePlayerSystemsTest, WalkingSprintingAndDiagonalNormalizationUseShared240And360Speeds)
{
    GameplayInput input;input.moveRight=true;
    static_cast<void>(world.update(input,1));EXPECT_NEAR(world.playerPosition().x-start.x,240,.02F);
    ASSERT_TRUE(world.restorePlayerBody(start,{1,0},0,0));input.sprint=true;
    static_cast<void>(world.update(input,1));EXPECT_NEAR(world.playerPosition().x-start.x,360,.02F);
    ASSERT_TRUE(world.restorePlayerBody(start,{1,0},0,0));input.moveDown=true;input.movementSpeedMultiplier=.5F;
    static_cast<void>(world.update(input,1));EXPECT_NEAR(distance(start,world.playerPosition()),180,.02F);
}

TEST_F(BasePlayerSystemsTest, ControlBlocksMovementFireMeleeAndInteractionUntilTimerExpires)
{
    world.configureWeaponFire(*publishedContentRegistry().item(alpha_content::rifle).weaponUse);
    world.applyPlayerControl(.3F);GameplayInput input;input.moveRight=true;input.fireJustPressed=input.firePressed=true;
    input.meleeJustPressed=input.interactJustPressed=true;input.aimWorldPosition=Vec2{start.x+600,start.y};
    EXPECT_FALSE(world.update(input,.1F));EXPECT_FLOAT_EQ(world.playerPosition().x,start.x);EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_TRUE(world.playerControlled());EXPECT_NEAR(world.playerControlRemaining(),.2F,.0001F);
    static_cast<void>(world.update({},.3F));EXPECT_FALSE(world.playerControlled());
    input.fireJustPressed=input.firePressed=false;static_cast<void>(world.update(input,.1F));
    EXPECT_NEAR(world.playerPosition().x-start.x,24,.02F);
}

TEST_F(BasePlayerSystemsTest, ImpactSlowUsesTheSameElapsedPortionAndRecoversNormalMovement)
{
    world.applyPlayerImpactSlow(.2F);GameplayInput input;input.moveRight=true;
    static_cast<void>(world.update(input,.1F));EXPECT_NEAR(world.playerPosition().x-start.x,24*.28F,.02F);
    EXPECT_NEAR(world.playerImpactSlowRemaining(),.1F,.0001F);
    static_cast<void>(world.update({},.2F));EXPECT_FLOAT_EQ(world.playerImpactSlowRemaining(),0);
    const auto before=world.playerPosition();static_cast<void>(world.update(input,.1F));EXPECT_NEAR(world.playerPosition().x-before.x,24,.02F);
}

TEST_F(BasePlayerSystemsTest, CoreRejectsBothThrowableStartsAndOtherwiseLegalCoreLandings)
{
    BaseWorld coreWorld;ASSERT_EQ(coreWorld.playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    auto p=playerCenter(coreWorld);EXPECT_FALSE(coreWorld.previewGrenade({p.x+100,p.y},grenadeUse()));
    EXPECT_FALSE(coreWorld.previewNoiseDecoy({p.x+100,p.y},decoyUse()));
    const auto approach=clearCoreApproach(world);ASSERT_TRUE(approach);
    ASSERT_TRUE(world.restorePlayerBody(*approach,{1,0},0,0));p=playerCenter(world);
    ASSERT_EQ(world.playerSafetyZone(),HomeRegionSafetyZone::TransitionBuffer);
    const Vec2 landing{world.baseParcel().position.x+60,p.y};
    // Placement blockers also reserve the player's body and facility work
    // sockets. The physical throw index uses the published walls/facilities.
    std::vector<BallisticBlocker> walls;
    for(const auto &bounds:world.layout().movementBlockers)walls.push_back({walls.size()+1,{bounds.position,bounds.size}});
    for(const auto &facility:world.facilities())if(facility.active)walls.push_back({walls.size()+1,facility.bounds});
    const auto index=RaidSpaceBlockerIndex::build(world.worldSize(),walls);ASSERT_TRUE(index);
    const auto rawGrenade=queryGrenadeLanding(p,landing,world.activeSpaceId(),grenadeUse(),*index);
    const auto rawDecoy=queryNoiseDecoyLanding(p,landing,world.activeSpaceId(),decoyUse(),*index);
    ASSERT_TRUE(rawGrenade);ASSERT_TRUE(rawDecoy);
    ASSERT_EQ(queryHomeRegionSafetyZone(rawGrenade->point,world.baseParcel()),HomeRegionSafetyZone::SafeCore);
    ASSERT_EQ(queryHomeRegionSafetyZone(rawDecoy->point,world.baseParcel()),HomeRegionSafetyZone::SafeCore);
    EXPECT_FALSE(world.previewGrenade(landing,grenadeUse()));EXPECT_FALSE(world.previewNoiseDecoy(landing,decoyUse()));
}

TEST_F(BasePlayerSystemsTest, ArmedFlightAndPendingExplosionSurviveWalkingAcrossTheCoreBoundary)
{
    const auto approach=clearCoreApproach(world);ASSERT_TRUE(approach);ASSERT_TRUE(world.restorePlayerBody(*approach,{1,0},0,0));
    const auto p=playerCenter(world);auto landing=world.previewGrenade({p.x-120,p.y},grenadeUse());ASSERT_TRUE(landing);
    landing->effectIdentity="base-cross-core-throw";RaidGrenadeRuntime prepared;ASSERT_TRUE(prepared.start(*landing));world.commitGrenade(std::move(prepared));
    GameplayInput cross;cross.moveRight=true;static_cast<void>(world.update(cross,.45F));
    ASSERT_EQ(world.playerSafetyZone(),HomeRegionSafetyZone::SafeCore);ASSERT_TRUE(world.grenade().flight());
    EXPECT_EQ(world.grenade().flight()->landing.effectIdentity,"base-cross-core-throw");
    static_cast<void>(world.update({},1.65F));ASSERT_TRUE(world.grenade().pendingExplosion());
    const auto id=world.grenade().pendingExplosion()->id;static_cast<void>(world.update({},.1F));
    ASSERT_TRUE(world.grenade().pendingExplosion());EXPECT_EQ(world.grenade().pendingExplosion()->id,id);
    const auto plan=world.prepareGrenadeExplosion(100);ASSERT_TRUE(plan);EXPECT_FALSE(plan->playerDamage);
}

TEST_F(BasePlayerSystemsTest, NoiseDecoyChangesDailyInvestigationAndPhysicalMovementWithoutVisualAcquisition)
{
    const auto p=playerCenter(world);world.configureGroundBlockers({{{p.x+245,p.y-65},{10,130}}});
    auto snapshot=oneDailyEnemy(world,{p.x+600,p.y});world.configureHomePerimeter(&snapshot);
    auto landing=world.previewNoiseDecoy({p.x+300,p.y+300},decoyUse());ASSERT_TRUE(landing);landing->effectIdentity="daily-decoy";
    RaidNoiseDecoyRuntime prepared;ASSERT_TRUE(prepared.start(*landing));world.commitNoiseDecoy(std::move(prepared));
    for(unsigned i=0;i<6;++i)static_cast<void>(world.update({},.1F));
    ASSERT_TRUE(world.noiseDecoy().investigations().contains(17));ASSERT_EQ(world.perimeterEnemies().size(),1U);
    const auto before=distance(enemyCenter(world.perimeterEnemies().front()),landing->point);
    for(unsigned i=0;i<4;++i)static_cast<void>(world.update({},.1F));
    EXPECT_LT(distance(enemyCenter(world.perimeterEnemies().front()),landing->point),before);
    EXPECT_NE(world.perimeterEnemies().front().awarenessState(),EnemyAwarenessState::Alerted);
    EXPECT_EQ(world.perimeterDamageLastUpdate(),0);
}

TEST_F(BasePlayerSystemsTest, NoiseDecoyAlsoChangesRealDefenseActorInvestigation)
{
    const auto p=playerCenter(world);world.configureGroundBlockers({{{p.x+245,p.y-65},{10,130}}});
    const auto snapshot=oneDefenseEnemy(world,{p.x+600,p.y});ASSERT_TRUE(snapshot);std::string reason;
    ASSERT_TRUE(validateBaseDefenseSnapshot(*snapshot,reason))<<reason;ASSERT_TRUE(world.resumeBaseDefense(*snapshot));
    const auto id=snapshot->enemies.front().id;
    auto landing=world.previewNoiseDecoy({p.x+300,p.y+300},decoyUse());ASSERT_TRUE(landing);landing->effectIdentity="defense-decoy";
    RaidNoiseDecoyRuntime prepared;ASSERT_TRUE(prepared.start(*landing));world.commitNoiseDecoy(std::move(prepared));
    for(unsigned i=0;i<6;++i)static_cast<void>(world.update({},.1F));
    ASSERT_TRUE(world.noiseDecoy().investigations().contains(id));ASSERT_EQ(world.baseDefenseEnemies().size(),1U);
    const auto before=distance(enemyCenter(world.baseDefenseEnemies().front()),landing->point);
    for(unsigned i=0;i<4;++i)static_cast<void>(world.update({},.1F));
    EXPECT_LT(distance(enemyCenter(world.baseDefenseEnemies().front()),landing->point),before);
    EXPECT_NE(world.baseDefenseEnemies().front().awarenessState(),EnemyAwarenessState::Alerted);
    EXPECT_TRUE(world.baseDefenseState()->killedIds.empty());EXPECT_TRUE(world.baseDefenseState()->breachedIds.empty());
}

TEST_F(BasePlayerSystemsTest, DailyExplosionEmitsOneRemovalAndTrueBodyFactAndCannotResurrectFromSameCycle)
{
    const auto p=playerCenter(world);auto snapshot=oneDailyEnemy(world,{p.x+300,p.y+300},1);world.configureHomePerimeter(&snapshot);
    auto landing=world.previewGrenade({p.x+300,p.y+300},grenadeUse());ASSERT_TRUE(landing);landing->effectIdentity="daily-grenade";
    RaidGrenadeRuntime prepared;ASSERT_TRUE(prepared.start(*landing));world.commitGrenade(std::move(prepared));
    static_cast<void>(world.update({},2.0F));ASSERT_TRUE(world.grenade().pendingExplosion());
    const auto plan=world.prepareGrenadeExplosion(100);ASSERT_TRUE(plan);ASSERT_EQ(plan->enemies.size(),1U);
    ASSERT_TRUE(world.commitPreparedGrenadeExplosion(*plan));ASSERT_TRUE(world.perimeterEnemies().empty());
    ASSERT_EQ(world.perimeterRemovalsLastUpdate().size(),1U);EXPECT_EQ(world.perimeterRemovalsLastUpdate().front().id,17U);
    ASSERT_EQ(world.baseEnemyDeathsLastUpdate().size(),1U);const auto death=world.baseEnemyDeathsLastUpdate().front();
    EXPECT_EQ(death.id,17U);EXPECT_FLOAT_EQ(death.size.x,50);EXPECT_FLOAT_EQ(death.size.y,50);
    EXPECT_FALSE(world.commitPreparedGrenadeExplosion(*plan));EXPECT_EQ(world.baseEnemyDeathsLastUpdate().size(),1U);
    world.configureHomePerimeter(&snapshot);EXPECT_TRUE(world.perimeterEnemies().empty());
    static_cast<void>(world.update({},.1F));EXPECT_TRUE(world.perimeterRemovalsLastUpdate().empty());EXPECT_TRUE(world.baseEnemyDeathsLastUpdate().empty());
}

TEST_F(BasePlayerSystemsTest, DefenseExplosionRetiresOneStablePlannedIdentityAndReplayCannotDuplicateKill)
{
    const auto p=playerCenter(world);const auto snapshot=oneDefenseEnemy(world,{p.x+300,p.y+300},1);ASSERT_TRUE(snapshot);
    ASSERT_TRUE(world.resumeBaseDefense(*snapshot));const auto id=snapshot->enemies.front().id;
    auto landing=world.previewGrenade({p.x+300,p.y+300},grenadeUse());ASSERT_TRUE(landing);landing->effectIdentity="defense-grenade";
    RaidGrenadeRuntime prepared;ASSERT_TRUE(prepared.start(*landing));world.commitGrenade(std::move(prepared));
    for(unsigned i=0;i<21 && !world.grenade().pendingExplosion();++i)static_cast<void>(world.update({},.1F));
    ASSERT_TRUE(world.grenade().pendingExplosion());const auto plan=world.prepareGrenadeExplosion(100);ASSERT_TRUE(plan);ASSERT_EQ(plan->enemies.size(),1U);
    ASSERT_TRUE(world.commitPreparedGrenadeExplosion(*plan));EXPECT_TRUE(world.baseDefenseEnemies().empty());
    EXPECT_EQ(std::count(world.baseDefenseState()->killedIds.begin(),world.baseDefenseState()->killedIds.end(),id),1);
    EXPECT_FALSE(world.commitPreparedGrenadeExplosion(*plan));
    EXPECT_EQ(std::count(world.baseDefenseState()->killedIds.begin(),world.baseDefenseState()->killedIds.end(),id),1);
    const auto checkpoint=world.baseDefenseCheckpoint();ASSERT_TRUE(checkpoint);std::string reason;EXPECT_TRUE(validateBaseDefenseSnapshot(*checkpoint,reason))<<reason;
}

TEST_F(BasePlayerSystemsTest, DayNightLampAndActualWallShareTheAuthoritativeObservation)
{
    const auto p=playerCenter(world);const Vec2 target{p.x+720,p.y};
    world.setDaylight(true);ASSERT_TRUE(world.observation().validGeometry());EXPECT_TRUE(world.observation().pointVisible(target));
    EXPECT_EQ(world.observationKey().environment,RaidLightEnvironment::DayOutdoor);
    world.setDaylight(false);EXPECT_FALSE(world.observation().pointVisible(target));EXPECT_EQ(world.observationKey().environment,RaidLightEnvironment::NightOutdoor);
    world.setWeaponLight(21,22,60,900,120);EXPECT_TRUE(world.observation().pointVisible(target));
    EXPECT_EQ(world.observationKey().lightAssetId,21U);EXPECT_EQ(world.observationKey().lightBatteryId,22U);
    const Vec2 behind{p.x-60,p.y};const auto &background=world.weaponLightBackgroundObservations();
    EXPECT_TRUE(background[0].pointVisible(behind)||background[1].pointVisible(behind));
    const auto oldKey=world.observationKey();world.configureGroundBlockers({{{p.x+300,p.y-200},{20,400}}});
    EXPECT_NE(world.observationKey().geometryRevision,oldKey.geometryRevision);EXPECT_FALSE(world.observation().pointVisible(target));
    EXPECT_FALSE(world.boundsObservable({{target.x-10,target.y-10},{20,20}}));
    world.setWeaponLight(0,0,0,0,0);EXPECT_EQ(world.observationKey().lightAssetId,0U);EXPECT_FLOAT_EQ(world.weaponLightBackgroundRadius(),0);
}

TEST_F(BasePlayerSystemsTest, ModalFreezesObservationDirectionWhilePreservingTheWindowAimState)
{
    const auto initial = world.observationKey();
    const auto p = playerCenter(world);
    GameplayInput modal;
    modal.freezeObservationDirection = true;
    modal.aimWorldPosition = Vec2{p.x - 300, p.y};
    for (unsigned frame = 0; frame < 20; ++frame)
        static_cast<void>(world.update(modal, .1F));
    EXPECT_FLOAT_EQ(world.observationKey().direction.x, initial.direction.x);
    EXPECT_FLOAT_EQ(world.observationKey().direction.y, initial.direction.y);
    const auto heldAim = world.weaponAimWorldPosition();
    modal.freezeObservationDirection = false;
    static_cast<void>(world.update(modal, 0));
    EXPECT_LT(world.observationKey().direction.x, 0);
    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().x, heldAim.x);
    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().y, heldAim.y);
}

TEST_F(BasePlayerSystemsTest, GrenadeNoiseMakesSurvivorsInvestigateTheBlastInDailyAndDefense)
{
    for (const bool defense : {false, true}) {
        SCOPED_TRACE(defense ? "Defense" : "Daily");
        BaseWorld scene;
        ASSERT_TRUE(scene.restorePlayerBody(start, {1, 0}, 0, 0));
        const auto p = playerCenter(scene);
        scene.configureGroundBlockers({{{p.x + 245, p.y - 65}, {10, 130}}});
        if (defense) {
            const auto snapshot = oneDefenseEnemy(scene, {p.x + 600, p.y});
            ASSERT_TRUE(snapshot); ASSERT_TRUE(scene.resumeBaseDefense(*snapshot));
        } else {
            auto snapshot = oneDailyEnemy(scene, {p.x + 600, p.y});
            scene.configureHomePerimeter(&snapshot);
        }
        auto landing = scene.previewGrenade({p.x + 300, p.y + 300}, grenadeUse());
        ASSERT_TRUE(landing); landing->effectIdentity = defense ? "defense-blast-noise" : "daily-blast-noise";
        RaidGrenadeRuntime prepared; ASSERT_TRUE(prepared.start(*landing)); scene.commitGrenade(std::move(prepared));
        for (unsigned i = 0; i < 21; ++i) static_cast<void>(scene.update({}, .1F));
        ASSERT_TRUE(scene.grenade().pendingExplosion());
        const auto plan = scene.prepareGrenadeExplosion(100); ASSERT_TRUE(plan);
        ASSERT_TRUE(plan->enemies.empty()); // The survivor is outside blast damage, inside hearing range.
        ASSERT_TRUE(scene.commitPreparedGrenadeExplosion(*plan));
        const auto &actors = defense ? scene.baseDefenseEnemies() : scene.perimeterEnemies();
        ASSERT_EQ(actors.size(), 1U); EXPECT_EQ(actors.front().awarenessState(), EnemyAwarenessState::Searching);
        const auto before = distance(enemyCenter(actors.front()), landing->point);
        for (unsigned i = 0; i < 4; ++i) static_cast<void>(scene.update({}, .1F));
        EXPECT_LT(distance(enemyCenter(actors.front()), landing->point), before);
        EXPECT_NE(actors.front().awarenessState(), EnemyAwarenessState::Alerted);
    }
}
