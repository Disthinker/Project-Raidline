#include "world_melee_runtime.h"
#include "gameplay_input.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kHeavyWindupSeconds = 0.45F;
constexpr float kHeavyRecoverySeconds = 1.0F;
int heavyDamage(int value) noexcept
{
    return value <= 0 ? value : value + std::min(value, std::numeric_limits<int>::max() - value);
}
}

HeavyMeleeInput HeavyMeleeInput::from(const GameplayInput &input) noexcept
{
    if (input.freezeObservationDirection || input.throwableModal)
        return {};
    return {input.meleeHeavyJustPressed, input.meleeHeavyHeld};
}

void WorldMeleeRuntime::configure(std::optional<MeleeUseDefinition> use, bool usable, std::uint64_t assetId) noexcept
{
    if (assetId != assetId_)
        cancelPrepared();
    if (assetId != assetId_ || !use || !usable)
        cancelHeavyCharge();
    use_ = use;
    usable_ = usable;
    assetId_ = assetId;
}

float WorldMeleeRuntime::heavyChargeProgress() const noexcept
{
    return heavyChargeSeconds_.value_or(0.0F) / kHeavyWindupSeconds;
}

void WorldMeleeRuntime::beginFrame(float seconds) noexcept
{
    result_={};removals_.clear();
    if (std::isfinite(seconds) && seconds>0)
    {
        recovery_=std::max(0.0F,recovery_-seconds);swingSeconds_=std::max(0.0F,swingSeconds_-seconds);
        if (chargingHeavy())
            *heavyChargeSeconds_ = std::min(kHeavyWindupSeconds, *heavyChargeSeconds_ + seconds);
    }
}
bool WorldMeleeRuntime::blocksActions(float seconds) const noexcept
{return chargingHeavy() || recovery_> (std::isfinite(seconds) ? std::max(0.0F,seconds):0.0F)+0.000001F;}
bool WorldMeleeRuntime::restoreRecovery(float seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds<0 || seconds>10) return false;
    cancelHeavyCharge();
    recovery_=seconds;result_={};removals_.clear();swingSeconds_=0;return true;
}
void WorldMeleeRuntime::prepare(bool requested,bool suppressed,Vec2 origin,Vec2 direction,
    const EnemyLifecycle &actors,const std::function<bool(Vec2,Vec2)> &lineOfSight, HeavyMeleeInput heavy)
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
    if (suppressed || recovery_ > 0.000001F)
    {
        cancelHeavyCharge();
        return;
    }
    if (!heavy.held)
        cancelHeavyCharge();
    else if (heavy.justPressed && usable_)
        heavyChargeSeconds_ = 0.0F;
    const bool heavyStrike = chargingHeavy() && *heavyChargeSeconds_ + 0.000001F >= kHeavyWindupSeconds;
    if (heavyStrike)
        cancelHeavyCharge();
    else if (!requested || chargingHeavy() || heavy.justPressed)
        return;
    candidates_.clear();
    for (int y=static_cast<int>(std::floor((origin.y-use_->range)/128));y<=static_cast<int>(std::floor((origin.y+use_->range)/128));++y)
        for (int x=static_cast<int>(std::floor((origin.x-use_->range)/128));x<=static_cast<int>(std::floor((origin.x+use_->range)/128));++x)
            if (const auto cell=cells_.find({x,y});cell!=cells_.end())
                for (auto candidate:cell->second)
                {
                    if (std::hypot(candidate.center.x-origin.x,candidate.center.y-origin.y)>use_->range+0.0001F) continue;
                    candidate.reachable=lineOfSight(origin,candidate.center);candidates_.push_back(candidate);
                }
    auto strike = *use_;
    if (heavyStrike)
    {
        strike.damage = heavyDamage(strike.damage);
        strike.armorDamage = heavyDamage(strike.armorDamage);
        strike.recoverySeconds = kHeavyRecoverySeconds;
    }
    result_=resolveMeleeAttack(origin,direction,strike,candidates_);
    if (!result_.attempted) return;
    result_.broken=!usable_;result_.equipmentAssetId=assetId_;
    if (!usable_) cancelPrepared();
    swingHeavy_ = heavyStrike;
    recovery_=strike.recoverySeconds;swingSeconds_=0.15F;swingOrigin_=origin;swingDirection_=result_.direction;
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
