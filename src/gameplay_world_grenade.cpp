#include "gameplay_world.h"
#include "grenade_damage.h"
#include <algorithm>
#include <cmath>

namespace {
Vec2 grenadeEnemyCenter(const Enemy &e) { return {e.position().x+e.size().x/2,e.position().y+e.size().y/2}; }
bool samePoint(Vec2 a,Vec2 b) { return a.x==b.x && a.y==b.y; }
}

std::optional<GrenadeLanding> GameplayWorld::previewGrenade(Vec2 aim,const GrenadeUseDefinition &use) const
{
    if(!grenadeEnabled_ || !raidSession_.isActive() || player_.isDead() || player_.isControlled() ||
       grenade_.active() || noiseDecoy_.flight() || melee_.recoverySeconds()>0)return std::nullopt;
    const auto p=player_.position();const float s=player_.size();
    return queryGrenadeLanding({p.x+s/2,p.y+s/2},aim,activeRaidSpaceId(),use,activeBlockerIndex());
}
void GameplayWorld::commitGrenade(RaidGrenadeRuntime prepared) noexcept
{
    grenade_=std::move(prepared);
    if(!grenade_.active())grenadeInvestigations_.clear();
    if(grenade_.active()) {
        // A zero-time observation cancels only the player's extraction progress.
        // Existing signal/high-risk facts remain authoritative.
        raidSession_.update(0,false,false,false,false,false);
    }
}
std::optional<GrenadeExplosionPlan> GameplayWorld::prepareGrenadeExplosion() const
{
    if(!grenadeEnabled_ || !raidSession_.isActive() || !grenade_.pendingExplosion() ||
       grenade_.pendingExplosion()->landing.space!=activeRaidSpaceId())return std::nullopt;
    GrenadeExplosionPlan plan;
    plan.explosion=*grenade_.pendingExplosion();
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
    if(!grenadeEnabled_ || !raidSession_.isActive() || !grenade_.pendingExplosion() ||
       grenade_.pendingExplosion()->id!=plan.explosion.id || plan.explosion.landing.space!=activeRaidSpaceId() ||
       !samePoint(player_.position(),plan.playerPosition) || player_.health()!=plan.playerHealth)return false;
    for(const auto &hit:plan.enemies) {
        const auto *enemy=activeEnemies().find(hit.id);
        if(!enemy || !samePoint(enemy->position(),hit.position) || enemy->health()!=hit.health || enemy->torsoArmor()!=hit.armor)
            return false;
    }
    return true;
}
bool GameplayWorld::commitPreparedGrenadeExplosion(const GrenadeExplosionPlan &plan)
{
    if(!validatePreparedGrenadeExplosion(plan))return false;
    auto &actors=activeEnemies();
    for(const auto &hit:plan.enemies) {
        auto enemy=std::find_if(actors.begin(),actors.end(),[&](const Enemy &e){return e.combatTargetId()==hit.id;});
        enemy->applyArmorLoss(hit.damage.armorDurabilityLoss);
        static_cast<void>(enemy->takeDamage(hit.damage.damageApplied,EnemyImpactReaction::None));
    }
    const auto removals=actors.removeDead();
    consumeEnemyRemovals(removals);
    enemyObservationCache_.clear();enemyObservationIndexReady_=false;
    emitGrenadeNoise(plan.explosion.landing.point,plan.explosion.landing.use.noiseRadius);
    lastGrenadeExplosion_=plan.explosion;grenadeFlashSeconds_=0.25F;grenadeExplodedLastUpdate_=true;
    grenade_.acknowledgeExplosion();
    return true;
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
            grenadeInvestigations_.insert_or_assign(enemy.combatTargetId(),
                GrenadeInvestigation{activeRaidSpaceId(),source,enemy.searchTimeRemaining()});
        }
    }
}

void GameplayWorld::updateGrenadeInvestigations(float seconds)
{
    if(grenadeInvestigations_.empty() || !std::isfinite(seconds) || seconds<=0)return;
    const auto space=activeRaidSpaceId();
    for(auto it=grenadeInvestigations_.begin();it!=grenadeInvestigations_.end();) {
        if(it->second.space!=space){++it;continue;}
        it->second.remaining=std::max(0.0F,it->second.remaining-seconds);
        if(it->second.remaining<=0 || !activeEnemies().find(it->first))it=grenadeInvestigations_.erase(it);
        else ++it;
    }
}
