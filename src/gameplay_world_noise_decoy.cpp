#include "gameplay_world.h"
#include <cmath>

namespace {
Vec2 center(const Enemy &e) { return {e.position().x+e.size().x/2,e.position().y+e.size().y/2}; }
}
std::optional<NoiseDecoyLanding> GameplayWorld::previewNoiseDecoy(Vec2 aim,const NoiseDecoyUseDefinition &use) const
{
    if(!noiseDecoyEnabled_ || !raidSession_.isActive() || player_.isControlled() || player_.isDead() ||
        noiseDecoy_.flight() || melee_.recoverySeconds()>0)return std::nullopt;
    const auto p=player_.position();const float s=player_.size();
    return queryNoiseDecoyLanding({p.x+s/2,p.y+s/2},aim,activeRaidSpaceId(),use,activeBlockerIndex());
}
void GameplayWorld::updateNoiseDecoy(float seconds)
{
    if(!noiseDecoyEnabled_ || !raidSession_.isActive()){noiseDecoy_.clear();return;}
    noiseDecoy_.advance(activeRaidSpaceId(),seconds);
    auto &actors=activeEnemies();
    std::vector<CombatTargetId> alive;
    std::vector<NoiseDecoyListener> listeners;
    const bool pulse=noiseDecoy_.pulsesLastUpdate()>0;
    const bool hasBindings=!noiseDecoy_.investigations().empty();
    if(!pulse && !hasBindings)return;
    if(hasBindings)alive.reserve(actors.size());if(pulse)listeners.reserve(actors.size());
    for(const auto &enemy:actors)if(!enemy.isDead()){
        const auto id=enemy.combatTargetId();if(hasBindings)alive.push_back(id);
        if(!pulse)continue;
        const auto &state=actors.state(id);
        const auto &encounter=state.encounter;
        const Vec2 home=encounter.kind==RaidEncounterKind::Patrol && !encounter.patrolPoints.empty()
            ? encounter.patrolPoints[encounter.patrolPointIndex%encounter.patrolPoints.size()]:encounter.home;
        listeners.push_back({id,center(enemy),home,state.noiseResponsive &&
            enemy.awarenessState()!=EnemyAwarenessState::Alerted && enemy.attackPhase()==EnemyAttackPhase::Idle});
    }
    if(hasBindings)noiseDecoy_.prune(activeRaidSpaceId(),alive);
    if(pulse)noiseDecoy_.hearPulses(listeners,activeBlockerIndex());
}
std::optional<Vec2> GameplayWorld::noiseInvestigationGoal(Enemy &enemy,bool visible,Vec2 playerPosition)
{
    if(!noiseDecoyEnabled_)return std::nullopt;
    const auto id=enemy.combatTargetId();
    const auto binding=noiseDecoy_.investigations().find(id);
    if(binding==noiseDecoy_.investigations().end())return std::nullopt;
    // Normal perception wins. A sound never extends acquisition distance or
    // changes awareness, lastKnownTargetPosition, tactical role or attack state.
    const bool discovered=visible && std::hypot(center(enemy).x-playerPosition.x,center(enemy).y-playerPosition.y)
        <=enemy.acquireTargetDistance();
    if(enemy.isDead() || enemy.awarenessState()==EnemyAwarenessState::Alerted ||
        enemy.attackPhase()!=EnemyAttackPhase::Idle || discovered){noiseDecoy_.forget(id);return std::nullopt;}
    if(binding->second.returning &&
        enemy.awarenessState()==EnemyAwarenessState::Searching &&
        std::hypot(center(enemy).x-binding->second.home.x,center(enemy).y-binding->second.home.y)<=20)
        return center(enemy); // Let old search memory expire without leaving the returned post.
    return noiseDecoy_.investigationGoal(id,activeRaidSpaceId(),center(enemy));
}
