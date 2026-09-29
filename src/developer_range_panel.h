#pragma once
#include "developer_runtime_panel.h"
#include <array>

enum class RangePanelAction { WeaponPrevious, WeaponNext, AmmoPrevious, AmmoNext,
    ArmorPrevious, ArmorNext, SupplyPrevious, SupplyNext, EnemyPrevious, EnemyNext,
    PadPrevious, PadNext, CountPrevious, CountNext, GrantKit, GrantSupply, Spawn, Clear, Exit, Close };
inline constexpr DeveloperPanelRect rangePanelButton(RangePanelAction action)
{
    const auto index = static_cast<int>(action);
    if (index < 14) return {index % 2 ? 1060.0F : 995.0F,
        118.0F + static_cast<float>(index / 2) * 53, 55, 34};
    switch (action) {
    case RangePanelAction::GrantKit: return {130, 510, 300, 40};
    case RangePanelAction::GrantSupply: return {445, 510, 245, 40};
    case RangePanelAction::Spawn: return {705, 510, 180, 40};
    case RangePanelAction::Clear: return {900, 510, 220, 40};
    case RangePanelAction::Exit: return {130, 620, 350, 40};
    default: return {900, 620, 220, 40};
    }
}
inline std::optional<RangePanelAction> rangePanelActionAt(DeveloperPanelPoint point)
{
    for (int i = 0; i < 20; ++i) {
        auto action = static_cast<RangePanelAction>(i);
        if (developerPanelContains(rangePanelButton(action), point)) return action;
    }
    return std::nullopt;
}
