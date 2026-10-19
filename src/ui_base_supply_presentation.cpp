#include "ui_base_supply_presentation.h"
#include "base_service_domain.h"
#include "weapon_component_domain.h"
#include <algorithm>

std::vector<UiBaseSupplyRow> projectUiBaseSupplyRows(
    const ProfileState &p, const ContentRegistry &c, UiBaseSupplyTab tab, UiLanguage language) {
    std::vector<UiBaseSupplyRow> rows;
    if (tab == UiBaseSupplyTab::Purchase) {
        for (const auto &id : c.fixedSupplyItemIds()) {
            const auto &d = c.item(id);
            const auto quantity = d.category == ItemCategory::Ammunition ? 30U : 1U;
            rows.push_back({{id, 0}, compactUiItemName(d, language), quantity,
                d.marketBuyPrice ? std::optional<std::uint64_t>{std::uint64_t{d.marketBuyPrice} * quantity} : std::nullopt});
        }
        return rows;
    }
    auto assets = assetsInContainer(p, ProfileContainerId::stash());
    std::stable_partition(assets.begin(), assets.end(), [&](const auto *a) {
        return c.item(a->definitionId).meleeUse.has_value();
    });
    for (const auto *a : assets) {
        const auto &d = c.item(a->definitionId);
        if (tab == UiBaseSupplyTab::Maintenance && !d.weaponCondition && !d.meleeUse) continue;
        UiBaseSupplyRow row{{a->definitionId, a->instanceId}, compactUiItemName(d, language), a->quantity};
        row.relief = a->reliefBatchId.has_value();
        if (d.weaponCondition || d.meleeUse) {
            row.durability = a->currentDurability;
            row.maximumDurability = a->currentMaximumDurability;
        }
        if (tab == UiBaseSupplyTab::Recycle) {
            row.componentValue = installedWeaponComponentRecycleValue(p, c, a->instanceId);
            if (d.marketRecyclePrice) row.quote = std::uint64_t{d.marketRecyclePrice} * a->quantity + row.componentValue;
        } else {
            const auto plan = queryGunsmithMaintenance(p, c, {a->instanceId});
            if (plan.canCommit) row.quote = plan.quotedCurrency;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
std::size_t uiBaseSupplyPageCount(std::size_t count) noexcept {
    return std::max<std::size_t>(1, (count + kUiBaseSupplyPageSize - 1) / kUiBaseSupplyPageSize);
}
void UiBaseSupplyState::reset() noexcept { *this = {}; }
void UiBaseSupplyState::selectTab(UiBaseSupplyTab next) noexcept {
    tab = next; selection.reset(); press.reset();
}
std::size_t UiBaseSupplyState::currentPage() const noexcept { return pages[static_cast<std::size_t>(tab)]; }
void UiBaseSupplyState::page(int delta, std::size_t rows) noexcept {
    auto &p = pages[static_cast<std::size_t>(tab)];
    p = static_cast<std::size_t>(std::clamp(static_cast<int>(p) + delta, 0, static_cast<int>(uiBaseSupplyPageCount(rows) - 1)));
    selection.reset(); press.reset();
}
void UiBaseSupplyState::reconcile(const std::vector<UiBaseSupplyRow> &rows) {
    auto &p = pages[static_cast<std::size_t>(tab)];
    p = std::min(p, uiBaseSupplyPageCount(rows.size()) - 1);
    if (selection && std::none_of(rows.begin(), rows.end(), [&](const auto &r) { return r.key == *selection; })) {
        selection.reset(); press.reset();
    }
}
SDL_FRect uiBaseSupplyTabRect(std::size_t i) noexcept { return {64 + 176.0F * i, 140, 164, 36}; }
SDL_FRect uiBaseSupplyRowRect(std::size_t i) noexcept { return {64, 192 + 44.0F * i, 548, 42}; }
SDL_FRect uiBaseSupplyListRect() noexcept { return {64, 190, 548, 354}; }
SDL_FRect uiBaseSupplyControlRect(UiBaseSupplyControl c) noexcept {
    switch(c) {
    case UiBaseSupplyControl::Previous: return {64, 552, 132, 32};
    case UiBaseSupplyControl::Next: return {480, 552, 132, 32};
    case UiBaseSupplyControl::Main: return {916, 552, 300, 40};
    case UiBaseSupplyControl::Relief: return {64, 608, 218, 38};
    case UiBaseSupplyControl::Components: return {294, 608, 218, 38};
    case UiBaseSupplyControl::Close: return {1084, 88, 132, 34};
    default: return {};
    }
}
std::optional<UiBaseSupplyHit> hitUiBaseSupply(Vec2 p, const UiBaseSupplyState &s, const std::vector<UiBaseSupplyRow> &rows) {
    for (std::size_t i = 0; i < 3; ++i)
        if (uiContains(uiBaseSupplyTabRect(i), p)) return UiBaseSupplyHit{UiBaseSupplyControl::Tab, i};
    for (std::size_t i = 0; i < kUiBaseSupplyPageSize; ++i) {
        const auto n = s.currentPage() * kUiBaseSupplyPageSize + i;
        if (n < rows.size() && uiContains(uiBaseSupplyRowRect(i), p)) return UiBaseSupplyHit{UiBaseSupplyControl::Row, i, rows[n].key};
    }
    for (auto control : {UiBaseSupplyControl::Previous, UiBaseSupplyControl::Next, UiBaseSupplyControl::Main,
         UiBaseSupplyControl::Relief, UiBaseSupplyControl::Components, UiBaseSupplyControl::Close})
        if (uiContains(uiBaseSupplyControlRect(control), p))
            return UiBaseSupplyHit{control, 0, control == UiBaseSupplyControl::Main ? s.selection : std::nullopt};
    return std::nullopt;
}
