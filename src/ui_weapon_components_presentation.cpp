#include "ui_weapon_components_presentation.h"

#include <algorithm>
#include <fmt/format.h>
#include <set>

std::size_t uiComponentPageCount(std::size_t count) noexcept {
    return std::max(std::size_t{1}, (count + kUiComponentPageSize - 1) / kUiComponentPageSize);
}
std::string uiComponentStashDestination(const ProfileState &p, const ContentRegistry &c, GridPosition cell) {
    ProfileInventoryViewport view;
    view.size = profileContainerSize(p, c, ProfileContainerId::stash());
    view.isolatedPages = true;
    view.reveal(cell, {1, 1});
    return fmt::format("STASH PAGE {} ({}, {})", view.page() + 1,
                       cell.x - view.origin.x + 1, cell.y - view.origin.y + 1);
}
namespace {
std::string source(const ProfileState &p, const ContentRegistry &c, const AssetRecord &a) {
    if (const auto *s = std::get_if<StoredAssetLocation>(&a.location)) {
        if (s->container.kind == ProfileContainerKind::Stash) return uiComponentStashDestination(p, c, s->origin);
        return fmt::format("{} ({}, {})", s->container.kind == ProfileContainerKind::BaseIntake ? "BASE INTAKE" : "CARRIED CONTAINER", s->origin.x + 1, s->origin.y + 1);
    }
    if (const auto *e = std::get_if<EquippedAssetLocation>(&a.location)) {
        switch (e->slot) {
        case EquipmentSlotKind::PrimaryWeapon: return "PRIMARY WEAPON";
        case EquipmentSlotKind::SecondaryWeapon: return "SECONDARY WEAPON";
        case EquipmentSlotKind::Sidearm: return "SIDEARM";
        default: return "EQUIPPED";
        }
    }
    return "";
}
}
UiWeaponComponentsProjection projectUiWeaponComponents(const ProfileState &p, const ContentRegistry &c,
    AssetInstanceId weaponId, WeaponComponentSlot slot, UiLanguage language) {
    UiWeaponComponentsProjection result;
    std::set<ItemDefinitionId> platforms;
    for (const auto &d : c.items()) if (d.weaponComponent) platforms.insert(d.weaponComponent->weaponDefinitionId);
    const auto *weapon = p.assets.find(weaponId);
    if (weapon && (!assetIsBaseAccessible(p, weaponId) || !platforms.contains(weapon->definitionId))) weapon = nullptr;
    if (weapon) {
        result.current = effectiveWeaponUse(p, c, weaponId);
        result.choices.push_back({0, {}, "RESTORE DEFAULT", ""});
    }
    for (const auto &[id, a] : p.assets.records()) {
        const auto &d = c.item(a.definitionId);
        if (weapon && d.weaponComponent) {
            if (const auto *installed = std::get_if<InstalledWeaponComponentLocation>(&a.location);
                installed && installed->weaponAssetId == weaponId)
                result.installed[static_cast<std::size_t>(d.weaponComponent->slot)] = id;
        }
        if (!assetIsBaseAccessible(p, id)) continue;
        if (platforms.contains(a.definitionId))
            result.weapons.push_back({id, a.definitionId, compactUiItemName(d, language), source(p, c, a)});
        if (weapon && d.weaponComponent && d.weaponComponent->weaponDefinitionId == weapon->definitionId &&
            d.weaponComponent->slot == slot && std::holds_alternative<StoredAssetLocation>(a.location))
            result.choices.push_back({id, a.definitionId, compactUiItemName(d, language), source(p, c, a)});
    }
    return result;
}
SDL_FRect uiComponentWeaponRow(std::size_t i) noexcept { return {64, 188 + 56.0F * i, 254, 50}; }
SDL_FRect uiComponentChoiceRow(std::size_t i) noexcept { return {338, 330 + 35.0F * i, 468, 32}; }
SDL_FRect uiComponentSlot(std::size_t i) noexcept { return {338 + 95.0F * i, 222, 88, 58}; }
SDL_FRect uiComponentDetails() noexcept { return {844, 418, 358, 148}; }
SDL_FRect uiComponentControl(UiComponentControl c) noexcept {
    switch (c) {
    case UiComponentControl::WeaponPrevious: return {64, 548, 76, 32};
    case UiComponentControl::WeaponNext: return {242, 548, 76, 32};
    case UiComponentControl::ChoicePrevious: return {338, 548, 100, 32};
    case UiComponentControl::ChoiceNext: return {706, 548, 100, 32};
    case UiComponentControl::Confirm: return {864, 604, 352, 44};
    case UiComponentControl::Back: return {950, 92, 266, 34};
    default: return {};
    }
}
std::optional<UiComponentHit> hitUiWeaponComponents(Vec2 p, const UiWeaponComponentsProjection &rows,
    std::size_t weaponPage, std::size_t choicePage, std::optional<WeaponComponentCommand> command) {
    for (std::size_t i = 0; i < kUiComponentPageSize; ++i) {
        auto n = weaponPage * kUiComponentPageSize + i;
        if (n < rows.weapons.size() && uiContains(uiComponentWeaponRow(i), p)) return UiComponentHit{UiComponentControl::Weapon, 0, rows.weapons[n].id};
        n = choicePage * kUiComponentPageSize + i;
        if (n < rows.choices.size() && uiContains(uiComponentChoiceRow(i), p)) return UiComponentHit{UiComponentControl::Choice, 0, rows.choices[n].id};
    }
    for (std::size_t i = 0; i < kUiComponentSlotCount; ++i) if (uiContains(uiComponentSlot(i), p)) return UiComponentHit{UiComponentControl::Slot, i};
    for (auto c : {UiComponentControl::WeaponPrevious, UiComponentControl::WeaponNext, UiComponentControl::ChoicePrevious,
                   UiComponentControl::ChoiceNext, UiComponentControl::Confirm, UiComponentControl::Back})
        if (uiContains(uiComponentControl(c), p)) return UiComponentHit{c, 0, 0, c == UiComponentControl::Confirm ? command : std::nullopt};
    return std::nullopt;
}
