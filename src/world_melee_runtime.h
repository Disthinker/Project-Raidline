#pragma once
#include "enemy_lifecycle.h"
#include "melee_attack.h"
#include <functional>
#include <map>
#include <vector>

// Shared by Raid, daily Base and defense. Owns transient simulation only.
// Asset wear is accepted by the service before commitPrepared can apply HP.
class WorldMeleeRuntime
{
public:
    void configure(std::optional<MeleeUseDefinition> use, bool usable, std::uint64_t assetId=0) noexcept
    {if(assetId!=assetId_)cancelPrepared();use_=use;usable_=usable;assetId_=assetId;}
    void beginFrame(float seconds) noexcept;
    void prepare(bool requested, bool suppressed, Vec2 origin, Vec2 direction,
        const EnemyLifecycle &, const std::function<bool(Vec2,Vec2)> &lineOfSight);
    [[nodiscard]] bool commitPrepared(EnemyLifecycle &);
    void cancelPrepared() noexcept {result_.contact=false;result_.target.reset();}
    [[nodiscard]] const MeleeAttackResult &result() const noexcept {return result_;}
    [[nodiscard]] const std::vector<EnemyRemovalFact> &removals() const noexcept {return removals_;}
    [[nodiscard]] float recoverySeconds() const noexcept {return recovery_;}
    [[nodiscard]] bool blocksActions(float frameSeconds=0) const noexcept;
    [[nodiscard]] bool restoreRecovery(float seconds) noexcept;
    [[nodiscard]] float swingSeconds() const noexcept {return swingSeconds_;}
    [[nodiscard]] Vec2 swingOrigin() const noexcept {return swingOrigin_;}
    [[nodiscard]] Vec2 swingDirection() const noexcept {return swingDirection_;}
private:
    std::optional<MeleeUseDefinition> use_;
    bool usable_{};
    std::uint64_t assetId_{};
    float recovery_{},swingSeconds_{};
    Vec2 swingOrigin_{},swingDirection_{1,0};
    MeleeAttackResult result_;
    std::vector<EnemyRemovalFact> removals_;
    std::map<std::pair<int,int>,std::vector<MeleeTargetSnapshot>> cells_;
    std::vector<MeleeTargetSnapshot> candidates_;
};
