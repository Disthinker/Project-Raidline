#pragma once

#include <cstddef>
#include <optional>

struct DeveloperPanelPoint
{
    float x{};
    float y{};
};

struct DeveloperPanelRect
{
    float x{};
    float y{};
    float width{};
    float height{};
};

enum class DeveloperPanelActionKind
{
    ToggleMapFog,
    ToggleInfiniteAmmo,
    ResetWeaponTuning,
    SelectWeaponParameter,
    DecreaseWeaponParameter,
    IncreaseWeaponParameter
};

struct DeveloperPanelAction
{
    DeveloperPanelActionKind kind{
        DeveloperPanelActionKind::SelectWeaponParameter};
    std::size_t parameterIndex{};

    friend bool operator==(
        const DeveloperPanelAction &,
        const DeveloperPanelAction &) = default;
};

inline constexpr DeveloperPanelRect developerPanelBounds() noexcept
{
    return {100.0F, 15.0F, 1080.0F, 690.0F};
}

inline constexpr DeveloperPanelRect developerFogButton() noexcept
{
    return {124.0F, 66.0F, 310.0F, 34.0F};
}

inline constexpr DeveloperPanelRect developerInfiniteAmmoButton() noexcept
{
    return {454.0F, 66.0F, 310.0F, 34.0F};
}

inline constexpr DeveloperPanelRect developerResetWeaponButton() noexcept
{
    return {914.0F, 66.0F, 240.0F, 34.0F};
}

inline constexpr float kDeveloperParameterFirstRowY{124.0F};
inline constexpr float kDeveloperParameterRowHeight{20.0F};

inline constexpr DeveloperPanelRect developerParameterRow(
    std::size_t index) noexcept
{
    return {
        116.0F,
        kDeveloperParameterFirstRowY +
            static_cast<float>(index) * kDeveloperParameterRowHeight,
        1038.0F,
        19.0F};
}

inline constexpr DeveloperPanelRect developerParameterDecreaseButton(
    std::size_t index) noexcept
{
    const DeveloperPanelRect row = developerParameterRow(index);
    return {1048.0F, row.y, 46.0F, row.height};
}

inline constexpr DeveloperPanelRect developerParameterIncreaseButton(
    std::size_t index) noexcept
{
    const DeveloperPanelRect row = developerParameterRow(index);
    return {1102.0F, row.y, 46.0F, row.height};
}

inline constexpr bool developerPanelContains(
    DeveloperPanelRect rect,
    DeveloperPanelPoint point) noexcept
{
    return point.x >= rect.x && point.y >= rect.y &&
        point.x < rect.x + rect.width &&
        point.y < rect.y + rect.height;
}

inline std::optional<DeveloperPanelAction> developerPanelActionAt(
    DeveloperPanelPoint point,
    std::size_t parameterCount) noexcept
{
    if (developerPanelContains(developerFogButton(), point))
        return DeveloperPanelAction{DeveloperPanelActionKind::ToggleMapFog};
    if (developerPanelContains(developerInfiniteAmmoButton(), point))
        return DeveloperPanelAction{
            DeveloperPanelActionKind::ToggleInfiniteAmmo};
    if (developerPanelContains(developerResetWeaponButton(), point))
        return DeveloperPanelAction{
            DeveloperPanelActionKind::ResetWeaponTuning};

    for (std::size_t index{}; index < parameterCount; ++index)
    {
        if (developerPanelContains(
                developerParameterDecreaseButton(index), point))
        {
            return DeveloperPanelAction{
                DeveloperPanelActionKind::DecreaseWeaponParameter,
                index};
        }
        if (developerPanelContains(
                developerParameterIncreaseButton(index), point))
        {
            return DeveloperPanelAction{
                DeveloperPanelActionKind::IncreaseWeaponParameter,
                index};
        }
        if (developerPanelContains(developerParameterRow(index), point))
        {
            return DeveloperPanelAction{
                DeveloperPanelActionKind::SelectWeaponParameter,
                index};
        }
    }
    return std::nullopt;
}
