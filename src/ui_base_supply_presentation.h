#pragma once

#include <array>

#include "ui_item_context_presentation.h"

// Client-only browsing state. These keys never become persisted asset locations.
enum class UiBaseSupplyTab { Purchase, Recycle, Maintenance };
struct UiBaseSupplyKey {
    ItemDefinitionId definitionId;
    AssetInstanceId instanceId{};
    bool operator==(const UiBaseSupplyKey &) const = default;
};
struct UiBaseSupplyRow {
    UiBaseSupplyKey key;
    std::string shortName;
    std::uint32_t quantity{};
    std::optional<std::uint64_t> quote;
    std::uint64_t componentValue{};
    bool relief{};
    std::optional<std::uint32_t> durability;
    std::uint32_t maximumDurability{};
};
[[nodiscard]] std::vector<UiBaseSupplyRow> projectUiBaseSupplyRows(
    const ProfileState &, const ContentRegistry &, UiBaseSupplyTab, UiLanguage);
inline constexpr std::size_t kUiBaseSupplyPageSize = 8;
[[nodiscard]] std::size_t uiBaseSupplyPageCount(std::size_t rows) noexcept;

enum class UiBaseSupplyControl { Row, Tab, Previous, Next, Main, Relief, Components, Close };
struct UiBaseSupplyHit {
    UiBaseSupplyControl control{};
    std::size_t index{};
    std::optional<UiBaseSupplyKey> key;
    bool operator==(const UiBaseSupplyHit &) const = default;
};
struct UiBaseSupplyState {
    UiBaseSupplyTab tab{};
    std::array<std::size_t, 3> pages{};
    std::optional<UiBaseSupplyKey> selection;
    std::optional<UiBaseSupplyHit> press;
    void reset() noexcept;
    void selectTab(UiBaseSupplyTab) noexcept;
    void page(int delta, std::size_t rows) noexcept;
    void reconcile(const std::vector<UiBaseSupplyRow> &);
    [[nodiscard]] std::size_t currentPage() const noexcept;
};

[[nodiscard]] SDL_FRect uiBaseSupplyTabRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiBaseSupplyRowRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiBaseSupplyListRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseSupplyControlRect(UiBaseSupplyControl) noexcept;
[[nodiscard]] std::optional<UiBaseSupplyHit> hitUiBaseSupply(
    Vec2, const UiBaseSupplyState &, const std::vector<UiBaseSupplyRow> &);

enum class UiBaseSupplyEventKind { Down, Up, Move, Wheel };
struct UiBaseSupplyEvent {
    UiBaseSupplyEventKind kind{};
    Vec2 position{};
    int pageDelta{};
};
