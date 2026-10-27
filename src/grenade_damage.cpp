#include "grenade_damage.h"

#include <cmath>

int grenadeDamageAtDistance(float distance, const GrenadeUseDefinition &use) noexcept
{
    if (!use.valid() || !std::isfinite(distance) || distance < 0.0F || distance > use.radius)
        return 0;
    if (distance <= use.coreRadius) return use.coreDamage;
    const double fraction = (static_cast<double>(distance) - use.coreRadius) /
        (static_cast<double>(use.radius) - use.coreRadius);
    return static_cast<int>(std::ceil(use.coreDamage -
        static_cast<double>(use.coreDamage - use.edgeDamage) * fraction));
}
