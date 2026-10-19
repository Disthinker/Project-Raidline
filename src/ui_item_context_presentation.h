#pragma once

#include "profile_inventory_interaction.h"
#include "ui_foundation.h"
#include "weapon_supply_projection.h"

// These IDs name existing client entry points, never new domain operations.
enum class UiItemAction {
    WeaponDetails, ModifyWeapon, MeleeDetails, DecoyDetails,
    UnloadMagazine, UseMedical, ChamberRound
};
struct UiItemActionEntry {
    UiItemAction id{};
    std::string label;
};
[[nodiscard]] std::string compactUiItemName(const ItemDefinition &, UiLanguage);
[[nodiscard]] std::vector<UiItemActionEntry> projectUiItemActions(
    const ProfileState &, const ContentRegistry &, AssetInstanceId, bool inRaid);
[[nodiscard]] std::vector<std::string> projectUiItemSummary(
    const AssetRecord &, const ItemDefinition &);
[[nodiscard]] std::vector<std::string> projectUiWeaponSupplyLines(
    const WeaponSupplyReadiness &, const WeaponSupplyProjection &,
    bool inRaid, EquipmentSlotKind activeSlot);

using UiItemMeasure = std::function<float(std::string_view, int)>;
struct UiItemTextBlock {
    SDL_FRect bounds{};
    std::string text;
    int size{16};
};
struct UiItemContextLayout {
    SDL_FRect panel{};
    UiItemTextBlock title;
    std::vector<UiItemTextBlock> rows;
    [[nodiscard]] std::optional<std::size_t> rowAt(Vec2) const noexcept;
};
[[nodiscard]] UiItemContextLayout layoutUiItemContext(
    Vec2 anchor, std::string_view title,
    const std::vector<UiItemActionEntry> &, UiLanguage, const UiItemMeasure &);

struct UiItemCardLayout {
    SDL_FRect panel{}, image{};
    std::vector<UiItemTextBlock> text;
};
[[nodiscard]] UiItemCardLayout layoutUiItemCard(
    SDL_FRect target, std::string_view title,
    const std::vector<std::string> &summary,
    const std::vector<std::string> &supply, UiLanguage, const UiItemMeasure &);

// Stable-ID hover state: travelling inside one item does not restart the delay.
struct UiItemHoverState {
    AssetInstanceId instanceId{};
    float elapsed{};
    SDL_FRect anchor{};
    void update(AssetInstanceId, SDL_FRect, float seconds) noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] float fade() const noexcept;
};
struct UiItemMenuPress {
    bool held{};
    std::optional<UiItemAction> action;
    void down(std::optional<UiItemAction>) noexcept;
    [[nodiscard]] std::optional<UiItemAction> up(std::optional<UiItemAction>) noexcept;
};
