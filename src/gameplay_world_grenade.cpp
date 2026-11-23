#include "gameplay_world.h"
#include "grenade_damage.h"
#include <algorithm>
#include <cmath>

namespace {
Vec2 grenadeEnemyCenter(const Enemy &e) { return {e.position().x+e.size().x/2,e.position().y+e.size().y/2}; }
bool samePoint(Vec2 a,Vec2 b) { return a.x==b.x && a.y==b.y; }
constexpr std::uint64_t barrelExplosionId(std::uint32_t id) noexcept
{ return (std::uint64_t{1} << 63) | id; }
static_assert(GrenadeUseDefinition{}.radius == kRaidVolatileBarrelBlastRadius);

const Enemy *findGrenadeHit(const Enemy *first, const Enemy *last,
                           const Enemy *&next, CombatTargetId id)
{
    const auto matches = [id](const Enemy &enemy) { return enemy.combatTargetId() == id; };
    const auto *target = std::find_if(next, last, matches);
    if (target != last)
    {
        next = target + 1;
        return target;
    }

    // Prepared hits follow roster order. A caller can still submit reordered
    // or repeated stable IDs, which must resolve exactly as before.
    target = std::find_if(first, next, matches);
    return target == next ? last : target;
}
}

std::optional<GrenadeLanding> GameplayWorld::previewGrenade(Vec2 aim,const GrenadeUseDefinition &use) const
{
    if(!grenadeEnabled_ || !raidSession_.isActive() || player_.isDead() || player_.isControlled() ||
       grenade_.active() || noiseDecoy_.flight() || thrownIllumination_.flight() || smoke_.flight() || fireZones_.flight() || melee_.recoverySeconds()>0)return std::nullopt;
    const auto p=player_.position();const float s=player_.size();
    return queryGrenadeLanding({p.x+s/2,p.y+s/2},aim,activeRaidSpaceId(),use,activeBlockerIndex());
}
std::optional<ThrownIlluminationLanding> GameplayWorld::previewThrownIllumination(
    Vec2 aim, const ThrownIlluminationUseDefinition &use) const
{
    if (!grenadeEnabled_ || !raidSession_.isActive() || player_.isDead() ||
        player_.isControlled() || grenade_.active() || noiseDecoy_.flight() ||
        thrownIllumination_.active() || smoke_.flight() || fireZones_.flight() || melee_.recoverySeconds() > 0.0F)
        return std::nullopt;
    const Vec2 position = player_.position();
    const float half = player_.size() / 2.0F;
    return queryThrownIlluminationLanding({position.x + half, position.y + half},
        aim, activeRaidSpaceId(), use, activeBlockerIndex());
}

std::optional<SmokeLanding> GameplayWorld::previewSmoke(Vec2 aim, const SmokeUseDefinition &use) const
{
    if (!grenadeEnabled_ || developerRange_ || !raidSession_.isActive() || player_.isDead() ||
        player_.isControlled() || !use.valid() || !smoke_.canStart() || grenade_.active() ||
        noiseDecoy_.flight() || thrownIllumination_.flight() || fireZones_.flight() || melee_.recoverySeconds() > 0.0F)
        return std::nullopt;
    NoiseDecoyUseDefinition ray;
    ray.range = use.maximumRange;
    ray.flightSeconds = use.flightSeconds;
    const Vec2 player = player_.position();
    const float half = player_.size() * 0.5F;
    const auto landing = queryNoiseDecoyLanding({player.x + half, player.y + half}, aim,
        activeRaidSpaceId(), ray, activeBlockerIndex());
    if (!landing) return std::nullopt;
    return SmokeLanding{landing->origin, landing->point, landing->space, use};
}

void GameplayWorld::refreshSmokeSight() noexcept
{
    const auto sight = smoke_.sight(activeRaidSpaceId());
    if (sight == activeSmokeSight_) return;
    activeSmokeSight_ = sight;
    indoorObservationCacheReady_ = false;
    scavengerSightSamples_.clear();
}

void GameplayWorld::commitGrenade(RaidGrenadeRuntime prepared) noexcept
{
    grenade_=std::move(prepared);
    if (!grenade_.active())
    {
        std::erase_if(localNoiseInvestigations_,
            [](const auto &entry) { return entry.second.fromGrenade; });
    }
    if(grenade_.active()) {
        // A zero-time observation cancels only the player's extraction progress.
        // Existing signal/high-risk facts remain authoritative.
        raidSession_.update(0,false,false,false,false,false);
    }
}
std::optional<GrenadeExplosionPlan> GameplayWorld::prepareGrenadeExplosion() const
{
    if(!raidSession_.isActive()) return std::nullopt;
    GrenadeExplosionPlan plan;
    if (grenadeEnabled_ && grenade_.pendingExplosion() && grenade_.pendingExplosion()->landing.space == activeRaidSpaceId())
        plan.explosion = *grenade_.pendingExplosion();
    else
    {
        if (!inOutdoorRaidSpace() || pendingBarrelIds_.empty()) return std::nullopt;
        plan.barrelId = pendingBarrelIds_.front();
        const auto barrel = std::ranges::find(volatileBarrels_, plan.barrelId, &RaidVolatileBarrelSpawn::id);
        if (barrel == volatileBarrels_.end() || barrel->spent) return std::nullopt;
        plan.explosion = {barrelExplosionId(barrel->id), {barrel->center, barrel->center,
            activeRaidSpaceId(), {}, "barrel:" + std::to_string(barrel->id)}};
    }
    plan.playerPosition=player_.position();plan.playerHealth=player_.health();
    const auto &landing=plan.explosion.landing;
    struct BlastWall { Rect bounds; float left,right,top,bottom; };
    std::vector<std::size_t> candidates;
    const float reach=landing.use.radius+1.0F;
    activeBlockerIndex().queryCandidateIndices({{landing.point.x-reach,landing.point.y-reach},{reach*2,reach*2}},candidates);
    const auto bounds=activeBlockerIndex().allBlockerBounds();
    std::vector<BlastWall> walls;walls.reserve(candidates.size());
    for(const auto id:candidates) {
        const auto &b=bounds[id];
        walls.push_back({b,b.position.x,b.position.x+b.size.x,b.position.y,b.position.y+b.size.y});
    }
    plan.enemies.reserve(activeEnemies().size());
    const auto damageAt=[&](Vec2 target) {
        const float d=std::hypot(target.x-landing.point.x,target.y-landing.point.y);
        const int damage=grenadeDamageAtDistance(d,landing.use);
        if(!damage)return 0;
        if(walls.empty())return damage;
        const float left=std::min(landing.point.x,target.x),right=std::max(landing.point.x,target.x);
        const float top=std::min(landing.point.y,target.y),bottom=std::max(landing.point.y,target.y);
        // One source, one immutable local geometry collection for this blast.
        // Keep the canonical blocker-interior predicate for every relevant ray.
        const auto *const stop=walls.data()+walls.size();
        for(const auto *wall=walls.data();wall!=stop;++wall) {
            if(wall->right<left || wall->left>right || wall->bottom<top || wall->top>bottom)continue;
            if(raidSpaceSegmentIntersectsBlockerInterior(landing.point,target,wall->bounds))return 0;
        }
        return damage;
    };
    for(const auto &enemy:activeEnemies())if(!enemy.isDead()) {
        const int raw=damageAt(grenadeEnemyCenter(enemy));if(!raw)continue;
        CombatDamageCommand command{raw,HitRegion::Torso,landing.use.penetration,landing.use.armorDamage,false,
            enemy.torsoArmor() ? std::optional{enemy.torsoArmor()->protection()}:std::nullopt};
        const auto resolution=resolveCombatDamage(command);
        if(!resolution.resolved())return std::nullopt;
        plan.enemies.push_back({enemy.combatTargetId(),enemy.position(),enemy.health(),enemy.torsoArmor(),resolution});
    }
    const auto p=player_.position();const float half=player_.size()/2;
    if(const int raw=damageAt({p.x+half,p.y+half});raw>0)
        plan.playerDamage=CombatDamageCommand{raw,HitRegion::Torso,landing.use.penetration,landing.use.armorDamage,false,{}};
    return plan;
}
bool GameplayWorld::validatePreparedGrenadeExplosion(const GrenadeExplosionPlan &plan) const
{
    if(!raidSession_.isActive() || plan.explosion.landing.space!=activeRaidSpaceId() ||
       !samePoint(player_.position(),plan.playerPosition) || player_.health()!=plan.playerHealth)return false;
    if (plan.barrelId)
    {
        if (!inOutdoorRaidSpace() || pendingBarrelIds_.empty() || pendingBarrelIds_.front() != plan.barrelId ||
            plan.explosion.id != barrelExplosionId(plan.barrelId)) return false;
        const auto barrel = std::ranges::find(volatileBarrels_, plan.barrelId, &RaidVolatileBarrelSpawn::id);
        if (barrel == volatileBarrels_.end() || barrel->spent || !samePoint(barrel->center, plan.explosion.landing.point) ||
            plan.explosion.landing.use != GrenadeUseDefinition{}) return false;
    }
    else if (!grenadeEnabled_ || !grenade_.pendingExplosion() || grenade_.pendingExplosion()->id != plan.explosion.id)
        return false;
    return validateAreaDamageTargets(plan.enemies);
}

bool GameplayWorld::validateAreaDamageTargets(std::span<const GrenadeEnemyDamage> hits) const
{
    const auto &actors = activeEnemies().view();
    const auto *const first = actors.data();
    const auto *const last = first + actors.size();
    const auto *next = first;
    for(const auto &hit:hits) {
        const auto *enemy = findGrenadeHit(first, last, next, hit.id);
        if(enemy == last || enemy->isDead() || !samePoint(enemy->position(),hit.position) ||
           enemy->health()!=hit.health || enemy->torsoArmor()!=hit.armor)
            return false;
    }
    return true;
}
bool GameplayWorld::commitPreparedGrenadeExplosion(const GrenadeExplosionPlan &plan)
{
    if(!validatePreparedGrenadeExplosion(plan))return false;
    applyAreaDamageTargets(plan.enemies);
    emitGrenadeNoise(plan.explosion.landing.point,plan.explosion.landing.use.noiseRadius);
    lastGrenadeExplosion_=plan.explosion;grenadeFlashSeconds_=0.25F;grenadeExplodedLastUpdate_=true;
    if (plan.barrelId)
    {
        std::ranges::find(volatileBarrels_, plan.barrelId, &RaidVolatileBarrelSpawn::id)->spent = true;
        std::erase_if(intactBarrelTargets_, [&](const auto &object) { return object.id == plan.barrelId; });
        pendingBarrelIds_.erase(pendingBarrelIds_.begin());
    }
    else
        grenade_.acknowledgeExplosion();
    return true;
}

void GameplayWorld::applyAreaDamageTargets(std::span<const GrenadeEnemyDamage> hits)
{
    auto &actors=activeEnemies();
    {
        const auto *const first = actors.view().data();
        const auto *const last = first + actors.size();
        const auto *next = first;
        for(const auto &hit:hits) {
            const auto *target = findGrenadeHit(first, last, next, hit.id);
            auto &enemy = actors[static_cast<std::size_t>(target - first)];
            enemy.applyArmorLoss(hit.damage.armorDurabilityLoss);
            static_cast<void>(enemy.takeDamage(hit.damage.damageApplied,EnemyImpactReaction::None));
        }
    }
    const auto removals=actors.removeDead();
    consumeEnemyRemovals(removals);
    enemyObservationCache_.clear();enemyObservationIndexReady_=false;
}
void GameplayWorld::emitGrenadeNoise(Vec2 source,float radius)
{
    auto &actors=activeEnemies();
    std::vector<std::string_view> groups;
    const auto eligible=[&](const Enemy &e) {
        return !e.isDead() && actors.state(e.combatTargetId()).noiseResponsive &&
            e.awarenessState()!=EnemyAwarenessState::Alerted && e.attackPhase()==EnemyAttackPhase::Idle;
    };
    for(const auto &enemy:actors)if(!enemy.isDead() && actors.state(enemy.combatTargetId()).noiseResponsive) {
        const auto center=grenadeEnemyCenter(enemy);
        if(std::hypot(center.x-source.x,center.y-source.y)>radius)continue;
        const auto &group=actors.state(enemy.combatTargetId()).encounter.groupInstanceId;
        if(!group.empty() && std::find(groups.begin(),groups.end(),group)==groups.end())groups.push_back(group);
    }
    for(auto &enemy:actors)if(eligible(enemy)) {
        const auto center=grenadeEnemyCenter(enemy);
        const auto &group=actors.state(enemy.combatTargetId()).encounter.groupInstanceId;
        if(std::hypot(center.x-source.x,center.y-source.y)<=radius ||
           (!group.empty() && std::find(groups.begin(),groups.end(),group)!=groups.end())) {
            noiseDecoy_.forget(enemy.combatTargetId());
            enemy.investigateSound(source);
            localNoiseInvestigations_.insert_or_assign(enemy.combatTargetId(),
                LocalNoiseInvestigation{activeRaidSpaceId(),source,enemy.searchTimeRemaining(),true});
        }
    }
}

void GameplayWorld::updateLocalNoiseInvestigations(float seconds)
{
    if(localNoiseInvestigations_.empty() || !std::isfinite(seconds) || seconds<=0)return;
    const auto space=activeRaidSpaceId();
    for(auto it=localNoiseInvestigations_.begin();it!=localNoiseInvestigations_.end();) {
        if(it->second.space!=space){++it;continue;}
        it->second.remaining=std::max(0.0F,it->second.remaining-seconds);
        if(it->second.remaining<=0 || !activeEnemies().find(it->first))it=localNoiseInvestigations_.erase(it);
        else ++it;
    }
}
