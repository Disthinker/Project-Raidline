#pragma once

#include "profile_container_presentation.h"
#include "ui_item_context_presentation.h"
#include "weapon_component_domain.h"

// Client projections only. Null action and the explicit restore-default ID (0)
// are distinct; neither a row number nor a screen page identifies an asset.
struct UiWeaponComponentRow {
    AssetInstanceId id{};
    ItemDefinitionId definitionId;
    std::string name;
    std::string source;
};
struct UiWeaponComponentsProjection {
    std::vector<UiWeaponComponentRow> weapons, choices;
    std::array<AssetInstanceId, 4> installed{};
    std::optional<WeaponUseDefinition> current;
};
[[nodiscard]] UiWeaponComponentsProjection projectUiWeaponComponents(
    const ProfileState &, const ContentRegistry &, AssetInstanceId,
    WeaponComponentSlot, UiLanguage);
[[nodiscard]] std::string uiComponentStashDestination(
    const ProfileState &, const ContentRegistry &, GridPosition);
inline constexpr std::size_t kUiComponentPageSize = 6;
[[nodiscard]] std::size_t uiComponentPageCount(std::size_t) noexcept;

enum class UiComponentControl {
    Weapon, Choice, Slot, WeaponPrevious, WeaponNext,
    ChoicePrevious, ChoiceNext, Confirm, Back
};
struct UiComponentHit {
    UiComponentControl control{};
    std::size_t index{};
    AssetInstanceId id{};
    std::optional<WeaponComponentCommand> command;
    bool operator==(const UiComponentHit &) const = default;
};
enum class UiComponentEventKind { Down, Up, Move, Wheel };
[[nodiscard]] SDL_FRect uiComponentWeaponRow(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiComponentChoiceRow(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiComponentSlot(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiComponentControl(UiComponentControl) noexcept;
[[nodiscard]] SDL_FRect uiComponentDetails() noexcept;
[[nodiscard]] std::optional<UiComponentHit> hitUiWeaponComponents(
    Vec2, const UiWeaponComponentsProjection &, std::size_t weaponPage,
    std::size_t choicePage, std::optional<WeaponComponentCommand>);
