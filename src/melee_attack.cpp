#include "melee_attack.h"
#include <cmath>
#include <limits>

MeleeAttackResult resolveMeleeAttack(Vec2 origin, Vec2 direction,
    const MeleeUseDefinition &use, std::span<const MeleeTargetSnapshot> candidates) noexcept
{
    MeleeAttackResult result;
    const float length=std::hypot(direction.x,direction.y);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(length) || length<=0 ||
        !std::isfinite(use.range) || use.range<=0 || !std::isfinite(use.angleDegrees) ||
        use.angleDegrees<=0 || use.angleDegrees>180 || use.damage<=0 || use.penetration<0 || use.armorDamage<0)
        return result;
    result.attempted=true;result.origin=origin;result.direction={direction.x/length,direction.y/length};
    const float cosine=std::cos(use.angleDegrees*0.5F*3.14159265358979323846F/180.0F);
    float best=std::numeric_limits<float>::max();
    for (const auto &target:candidates)
    {
        ++result.candidatesExamined;
        if (!target.reachable || target.health<=0 || target.id==kInvalidCombatTargetId) continue;
        const Vec2 delta{target.center.x-origin.x,target.center.y-origin.y};
        const float distance=std::hypot(delta.x,delta.y);
        if (!std::isfinite(distance) || distance>use.range+0.0001F ||
            (distance>0.0001F && (delta.x*result.direction.x+delta.y*result.direction.y)/distance+0.000001F<cosine)) continue;
        if (distance<best || (distance==best && (!result.target || target.id<result.target->id)))
        {best=distance;result.target=target;}
    }
    if (result.target)
    {
        result.damage=resolveCombatDamage({use.damage,HitRegion::Torso,use.penetration,use.armorDamage,false,result.target->armor});
        result.contact=result.damage.resolved();result.impact=result.target->center;
    }
    return result;
}
