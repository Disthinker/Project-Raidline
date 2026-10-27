#pragma once

#include "tactical_device_domain.h"
#include "ui_foundation.h"

#include <optional>

enum class UiItemDetailsKind { Weapon, Melee, Decoy, Grenade };
struct UiItemDetailsMetric {
    std::string label;
    std::string value;
};
// Read-only presentation of an existing instance. No guessed prices, ownership,
// ammunition or transaction eligibility are added here.
struct UiItemDetailsProjection {
    UiItemDetailsKind kind{UiItemDetailsKind::Weapon};
    AssetInstanceId instanceId{};
    ItemDefinitionId definitionId;
    std::string title, name, status;
    std::vector<UiItemDetailsMetric> metrics;
    std::vector<std::string> notes, components;
    bool warning{};
};
[[nodiscard]] std::optional<UiItemDetailsProjection> projectUiItemDetails(
    const ProfileState &, const ContentRegistry &, AssetInstanceId, UiLanguage);

inline constexpr std::size_t kUiDetailsBatteryPageSize{10};
[[nodiscard]] SDL_FRect uiItemDetailsPanel() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsBack() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsImage(UiItemDetailsKind) noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsMetric(UiItemDetailsKind, std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsBattery(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsBatterySlot() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsRemoveBattery() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsPrevious() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsNext() noexcept;
[[nodiscard]] SDL_FRect uiItemDetailsFeedback(UiItemDetailsKind) noexcept;

enum class UiItemDetailsControl { Back, Previous, Next, RemoveBattery };
[[nodiscard]] std::optional<UiItemDetailsControl> hitUiItemDetailsControl(
    Vec2, bool isWeapon, std::size_t page, std::size_t batteries,
    bool canRemove) noexcept;

// A press is bound to the exact displayed object and page. Moving out cancels
// the press permanently; a later release cannot revive it or act on a new view.
struct UiItemDetailsIdentity {
    AssetInstanceId item{}, device{};
    ProfileRevision revision{};
    std::size_t page{};
    UiLanguage language{UiLanguage::SimplifiedChinese};
    bool raid{};
    bool operator==(const UiItemDetailsIdentity &) const = default;
};
struct UiItemDetailsInteraction {
    std::optional<UiItemDetailsIdentity> identity;
    std::optional<UiItemDetailsControl> pressed;
    bool held{};
    int feedbackScroll{}, feedbackMaximum{};
    std::string feedbackKey;
    [[nodiscard]] bool synchronize(const UiItemDetailsIdentity &) noexcept;
    [[nodiscard]] bool down(std::optional<UiItemDetailsControl>) noexcept;
    void move(std::optional<UiItemDetailsControl>) noexcept;
    [[nodiscard]] std::optional<UiItemDetailsControl> up(
        std::optional<UiItemDetailsControl>) noexcept;
};
void renderUiItemDetailsFeedback(SDL_Renderer *, UiTextRenderer &,
    UiItemDetailsInteraction &, UiItemDetailsKind, std::string_view);
