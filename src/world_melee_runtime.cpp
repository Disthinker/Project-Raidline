#include "world_melee_runtime.h"
#include <algorithm>
#include <cmath>

void WorldMeleeRuntime::beginFrame(float seconds) noexcept
{
    result_={};removals_.clear();
    if (std::isfinite(seconds) && seconds>0)
    {recovery_=std::max(0.0F,recovery_-seconds);swingSeconds_=std::max(0.0F,swingSeconds_-seconds);}
}
bool WorldMeleeRuntime::blocksActions(float seconds) const noexcept
{return recovery_> (std::isfinite(seconds) ? std::max(0.0F,seconds):0.0F)+0.000001F;}
bool WorldMeleeRuntime::restoreRecovery(float seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds<0 || seconds>10) return false;
    recovery_=seconds;result_={};removals_.clear();swingSeconds_=0;return true;
}
void WorldMeleeRuntime::prepare(bool requested,bool suppressed,Vec2 origin,Vec2 direction,
    const EnemyLifecycle &actors,const std::function<bool(Vec2,Vec2)> &lineOfSight)
{
    if (!use_) return;
    // Build current-frame dynamic cells after enemy motion/retirement and old
    // ballistic flights, independently of the attack edge. Attacks visit only
    // nearby cells; no global search or stable-ID-to-pointer cache survives.
    cells_.clear();
    for (std::size_t i=0;i<actors.size();++i)
    {
        const auto &enemy=actors[i];if(enemy.isDead())continue;
        const auto p=enemy.position(),size=enemy.size();const Vec2 center{p.x+size.x/2,p.y+size.y/2};
        auto &cell=cells_[{static_cast<int>(std::floor(center.x/128)),static_cast<int>(std::floor(center.y/128))}];
        cell.push_back({enemy.combatTargetId(),center,enemy.health(),enemy.torsoArmor() ?
            std::optional{enemy.torsoArmor()->protection()}:std::nullopt,i,true});
    }
    if (!requested || suppressed || blocksActions()) return;
    candidates_.clear();
    for (int y=static_cast<int>(std::floor((origin.y-use_->range)/128));y<=static_cast<int>(std::floor((origin.y+use_->range)/128));++y)
        for (int x=static_cast<int>(std::floor((origin.x-use_->range)/128));x<=static_cast<int>(std::floor((origin.x+use_->range)/128));++x)
            if (const auto cell=cells_.find({x,y});cell!=cells_.end())
                for (auto candidate:cell->second)
                {
                    if (std::hypot(candidate.center.x-origin.x,candidate.center.y-origin.y)>use_->range+0.0001F) continue;
                    candidate.reachable=lineOfSight(origin,candidate.center);candidates_.push_back(candidate);
                }
    result_=resolveMeleeAttack(origin,direction,*use_,candidates_);
    if (!result_.attempted) return;
    result_.broken=!usable_;result_.equipmentAssetId=assetId_;
    if (!usable_) cancelPrepared();
    recovery_=use_->recoverySeconds;swingSeconds_=0.15F;swingOrigin_=origin;swingDirection_=result_.direction;
}
bool WorldMeleeRuntime::commitPrepared(EnemyLifecycle &actors)
{
    if (!result_.attempted || result_.committed) return false;
    if (!result_.contact) {result_.committed=true;return true;}
    const auto snapshot=*result_.target;
    if (snapshot.rosterPosition>=actors.size()) return false;
    auto &enemy=actors[snapshot.rosterPosition];
    const auto p=enemy.position(),size=enemy.size();
    if (enemy.combatTargetId()!=snapshot.id || enemy.isDead() || enemy.health()!=snapshot.health ||
        p.x+size.x/2!=snapshot.center.x || p.y+size.y/2!=snapshot.center.y) return false;
    const auto live=enemy.torsoArmor() ? std::optional{enemy.torsoArmor()->protection()}:std::nullopt;
    if (live.has_value()!=snapshot.armor.has_value() || (live &&
        (live->currentDurability!=snapshot.armor->currentDurability || live->coverage!=snapshot.armor->coverage ||
         live->protectionRequirement!=snapshot.armor->protectionRequirement || live->durabilityLossBasisPoints!=snapshot.armor->durabilityLossBasisPoints))) return false;
    enemy.applyArmorLoss(result_.damage.armorDurabilityLoss);
    result_.killed=enemy.takeDamage(result_.damage.damageApplied,EnemyImpactReaction::None);
    result_.committed=true;
    // Do not retain the reference above after roster mutation.
    removals_=actors.removeDead();return true;
}
