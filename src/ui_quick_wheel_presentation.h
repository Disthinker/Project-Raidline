#pragma once

#include "medical_domain.h"
#include "ui_foundation.h"

// Presentation consumers only: neither projection nor layout selects/uses an item.
enum class UiQuickWheelKind { Medical, Throwable };
struct UiQuickWheelEntry {
    AssetInstanceId instanceId{};
    std::string name, count, status;
    bool usable{};
    ItemDefinitionId definitionId;
    std::uint64_t quantity{}, remainingUses{};
};
[[nodiscard]] std::vector<AssetInstanceId> quickWheelCandidates(
    const ProfileState &, const ContentRegistry &, UiQuickWheelKind);
[[nodiscard]] std::optional<std::size_t> quickWheelSector(Vec2 offset) noexcept;
[[nodiscard]] Vec2 moveQuickWheelPointer(Vec2 offset, Vec2 delta) noexcept;
[[nodiscard]] std::vector<UiQuickWheelEntry> projectUiQuickWheel(
    const ProfileState &, const ContentRegistry &,
    const std::vector<AssetInstanceId> &, UiQuickWheelKind);

struct UiQuickWheelProjection {
    std::vector<UiQuickWheelEntry> entries;
    std::uint64_t generation{};
    bool refresh(const ProfileState &, const ContentRegistry &,
                 const std::vector<AssetInstanceId> &, UiQuickWheelKind);
private:
    bool valid_{};
    UiQuickWheelKind kind_{};
    std::string profileId_;
    ProfileRevision revision_{};
    int health_{};
    BleedingSeverity bleeding_{};
    std::vector<AssetInstanceId> ids_;
};
struct UiQuickWheelText {
    SDL_FRect bounds{};
    std::string text;
    int size{16};
};
struct UiQuickWheelRow {
    std::size_t index{};
    SDL_FRect bounds{}, image{};
    UiQuickWheelText name, count;
};
struct UiQuickWheelLayout {
    SDL_FRect panel{384, 104, 512, 512};
    SDL_FRect summary{}, image{};
    UiQuickWheelText title, subtitle, name, count, status, hint, cancel, range;
    std::vector<SDL_FRect> markers;
    std::vector<UiQuickWheelRow> rows;
    std::size_t selected{};
    std::size_t page{};
};
using UiQuickWheelMeasure = std::function<float(std::string_view,int)>;
[[nodiscard]] UiQuickWheelLayout layoutUiQuickWheel(
    const std::vector<UiQuickWheelEntry> &, std::size_t selected,
    UiQuickWheelKind, UiLanguage, const UiQuickWheelMeasure &, std::size_t page = 0);

struct UiQuickWheelEmphasis {
    AssetInstanceId id{};
    UiQuickWheelKind kind{};
    float started{};
    void select(AssetInstanceId next, UiQuickWheelKind nextKind, float now) noexcept;
    [[nodiscard]] float amount(float now) const noexcept;
};
