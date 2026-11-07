#pragma once

#include <string>
#include "definition_id.h"

enum class WeaponActionActivity { Raid, BaseDaily, BaseDefense, Training };

// Activity identity is supplied by the owning service, never inferred from a
// screen name. BaseDaily uses its real site ID; Defense its real event ID.
struct WeaponActionScope
{
    WeaponActionActivity activity{WeaponActionActivity::Raid};
    RegionalBaseSiteDefinitionId baseSiteDefinitionId;
    std::string activityId;
    friend bool operator==(const WeaponActionScope &, const WeaponActionScope &) = default;
};

struct ProfileState;
[[nodiscard]] bool weaponActionScopeMatches(const ProfileState &, const WeaponActionScope &) noexcept;
