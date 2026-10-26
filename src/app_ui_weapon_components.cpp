#include "app.h"

#include <algorithm>
#include <fmt/format.h>

namespace {
using Control = UiComponentControl;
using Kind = UiComponentEventKind;
constexpr const char *slots[]{"BARREL", "HANDGUARD", "GRIP", "WEAPON LIGHT", "MUZZLE"};
static_assert(std::size(slots) == kUiComponentSlotCount);
bool samePlan(const WeaponComponentPlan &a, const WeaponComponentPlan &b) {
    if (a.canCommit != b.canCommit || a.revision != b.revision || a.before != b.before ||
        a.after != b.after || a.returned.size() != b.returned.size()) return false;
    for (std::size_t i = 0; i < a.returned.size(); ++i)
        if (a.returned[i].assetId != b.returned[i].assetId || a.returned[i].destination != b.returned[i].destination) return false;
    return true;
}
}

void App::clearWeaponComponentAction() {
    componentActionSelected_ = false;
    componentChoice_ = 0;
    componentPreview_.reset();
    componentPress_.reset();
    componentDetailScroll_ = 0;
}
void App::resetWeaponComponentsUi() {
    clearWeaponComponentAction();
    componentWeapon_ = 0; componentSlot_ = WeaponComponentSlot::Barrel;
    componentWeaponPage_ = componentChoicePage_ = 0;
    componentRows_ = {}; componentRowsRevision_.reset();
}
bool App::openWeaponComponentsUi(AssetInstanceId id) {
    resetWeaponComponentsUi();
    refreshWeaponComponents();
    const auto it = std::find_if(componentRows_.weapons.begin(), componentRows_.weapons.end(),
        [id](const auto &r) { return r.id == id; });
    if (id && it == componentRows_.weapons.end()) return false;
    if (id) {
        componentWeapon_ = id;
        componentWeaponPage_ = static_cast<std::size_t>(it - componentRows_.weapons.begin()) / kUiComponentPageSize;
    } else if (!componentRows_.weapons.empty()) componentWeapon_ = componentRows_.weapons.front().id;
    refreshWeaponComponents(); uiMessage_.clear();
    return true;
}
void App::refreshWeaponComponents(bool force) {
    const auto &p = gameSession_.profile();
    const bool changed = componentRowsRevision_ &&
        (*componentRowsRevision_ != p.revision || componentRowsProfile_ != p.profileId);
    if (changed) {
        if (componentActionSelected_) uiMessage_ = "COMPONENT STATE CHANGED - SELECT AGAIN";
        clearWeaponComponentAction();
        if (componentRowsProfile_ != p.profileId) componentWeapon_ = 0;
    }
    if (!force && !changed && componentRowsRevision_ && componentRowsWeapon_ == componentWeapon_ &&
        componentRowsSlot_ == componentSlot_ && componentRowsLanguage_ == uiTextRenderer_.language()) return;
    componentRows_ = projectUiWeaponComponents(p, publishedContentRegistry(), componentWeapon_, componentSlot_, uiTextRenderer_.language());
    if (componentWeapon_ && !componentRows_.current) {
        componentWeapon_ = 0; clearWeaponComponentAction();
        uiMessage_ = "COMPONENT STATE CHANGED - SELECT AGAIN";
    }
    if (componentActionSelected_ && std::none_of(componentRows_.choices.begin(), componentRows_.choices.end(),
        [&](const auto &r) { return r.id == componentChoice_; })) {
        clearWeaponComponentAction(); uiMessage_ = "COMPONENT STATE CHANGED - SELECT AGAIN";
    }
    componentWeaponPage_ = std::min(componentWeaponPage_, uiComponentPageCount(componentRows_.weapons.size()) - 1);
    componentChoicePage_ = std::min(componentChoicePage_, uiComponentPageCount(componentRows_.choices.size()) - 1);
    componentRowsRevision_ = p.revision; componentRowsProfile_ = p.profileId;
    componentRowsWeapon_ = componentWeapon_; componentRowsSlot_ = componentSlot_;
    componentRowsLanguage_ = uiTextRenderer_.language();
}
const WeaponComponentPlan &App::weaponComponentPreview() {
    const auto &p = gameSession_.profile();
    const WeaponComponentCommand command{componentWeapon_, componentSlot_, componentChoice_};
    if (!componentPreview_ || componentPreview_->revision != p.revision || componentPreviewProfile_ != p.profileId || componentPreviewCommand_ != command) {
        componentPreview_ = queryWeaponComponentChange(p, publishedContentRegistry(), command);
        componentPreviewCommand_ = command; componentPreviewProfile_ = p.profileId;
    }
    return *componentPreview_;
}
void App::closeWeaponComponentsUi() {
    if (inventoryWeaponComponentsOpen_) inventoryWeaponComponentsOpen_ = false;
    else {
        weaponComponentsOpen_ = false;
        supplyUi_.selection.reset(); supplyRowsRevision_.reset(); supplyUi_.press.reset();
    }
    resetWeaponComponentsUi(); uiMessage_.clear();
    input_.suppressPrimaryPointerUntilRelease();
}
void App::handleWeaponComponentsEvent(Kind kind, Vec2 position, int delta) {
    const bool base = gameFlow_.state() == GameFlowState::Base;
    const bool range = gameSession_.developerRangeAtConsole();
    const bool validSource = (inventoryWeaponComponentsOpen_ && inventoryOverlayState_.isOpen()) ||
        (weaponComponentsOpen_ && base && gameFlow_.activeBaseFacility() == BaseFacilityKind::Supply);
    if (!validSource || (!base && !range) || pauseMenu_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        componentPress_.reset(); return;
    }
    refreshWeaponComponents(kind != Kind::Move);
    if (kind == Kind::Wheel) {
        componentPress_.reset();
        if (!delta) return;
        if (uiContains(uiComponentDetails(), position)) {
            componentDetailScroll_ = std::max(0, componentDetailScroll_ + delta * 3); return;
        }
        const bool weapon = uiContains({64, 188, 254, 336}, position);
        const bool choice = uiContains({338, 330, 468, 210}, position);
        if (!weapon && !choice) return;
        auto &page = weapon ? componentWeaponPage_ : componentChoicePage_;
        const auto count = uiComponentPageCount(weapon ? componentRows_.weapons.size() : componentRows_.choices.size());
        page = static_cast<std::size_t>(std::clamp(static_cast<int>(page) + delta, 0, static_cast<int>(count) - 1));
        clearWeaponComponentAction(); uiMessage_.clear(); return;
    }
    const auto command = componentActionSelected_ ? std::optional{WeaponComponentCommand{componentWeapon_, componentSlot_, componentChoice_}} : std::nullopt;
    const auto hit = hitUiWeaponComponents(position, componentRows_, componentWeaponPage_, componentChoicePage_, command);
    if (kind == Kind::Move) { if (hit != componentPress_) componentPress_.reset(); return; }
    if (kind == Kind::Down) { componentPress_ = hit; return; }
    const auto pressed = componentPress_; componentPress_.reset();
    if (!hit || hit != pressed) return;
    switch (hit->control) {
    case Control::Back: closeWeaponComponentsUi(); return;
    case Control::Weapon:
        clearWeaponComponentAction(); componentWeapon_ = hit->id; componentChoicePage_ = 0; break;
    case Control::Slot:
        clearWeaponComponentAction(); componentSlot_ = static_cast<WeaponComponentSlot>(hit->index); componentChoicePage_ = 0; break;
    case Control::Choice:
        clearWeaponComponentAction(); componentChoice_ = hit->id; componentActionSelected_ = true; break;
    case Control::WeaponPrevious: case Control::WeaponNext: case Control::ChoicePrevious: case Control::ChoiceNext: {
        auto &page = (hit->control == Control::WeaponPrevious || hit->control == Control::WeaponNext) ? componentWeaponPage_ : componentChoicePage_;
        const auto count = uiComponentPageCount((&page == &componentWeaponPage_) ? componentRows_.weapons.size() : componentRows_.choices.size());
        const int direction = (hit->control == Control::WeaponPrevious || hit->control == Control::ChoicePrevious) ? -1 : 1;
        page = static_cast<std::size_t>(std::clamp(static_cast<int>(page) + direction, 0, static_cast<int>(count) - 1));
        clearWeaponComponentAction(); break;
    }
    case Control::Confirm: {
        if (!command || !componentPreview_ || !componentPreview_->canCommit) return;
        // Re-query once on explicit confirmation. Never silently accept a plan
        // different from the preview, even if an external caller missed revision.
        const auto fresh = queryWeaponComponentChange(gameSession_.profile(), publishedContentRegistry(), *command);
        if (!samePlan(*componentPreview_, fresh)) {
            clearWeaponComponentAction(); uiMessage_ = "COMPONENT STATE CHANGED - SELECT AGAIN"; return;
        }
        const auto receipt = gameSession_.executeBaseWeaponComponentChange(*command, nextProfileTransactionId("weapon-component"));
        gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
        if (receipt.succeeded) {
            clearWeaponComponentAction(); refreshWeaponComponents(true); uiMessage_ = "COMPONENT CHANGE SAVED";
        } else { uiMessage_ = receipt.message; componentDetailScroll_ = 0; }
        return;
    }
    }
    uiMessage_.clear(); refreshWeaponComponents();
    if (componentActionSelected_) static_cast<void>(weaponComponentPreview());
}

void App::renderWeaponComponents() {
    refreshWeaponComponents();
    const auto &p = gameSession_.profile(); const auto &content = publishedContentRegistry();
    const WeaponComponentPlan *plan = componentActionSelected_ ? &weaponComponentPreview() : nullptr;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{40, 70, 1200, 600};
    SDL_SetRenderDrawColor(renderer_, 21, 29, 31, 252); SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 69, 88, 87, 255); SDL_RenderRect(renderer_, &panel);
    const SDL_FRect right{830, 138, 386, 442};
    SDL_SetRenderDrawColor(renderer_, 27, 38, 40, 255); SDL_RenderFillRect(renderer_, &right);
    const auto text = [&](SDL_FRect box, std::string_view value, int size = 14, UiTextAlign align = UiTextAlign::Left, int lines = 0, bool muted = false) {
        SDL_SetRenderDrawColor(renderer_, muted ? 150 : 222, muted ? 169 : 231, muted ? 164 : 226, 255);
        uiTextRenderer_.renderBox(renderer_, box, value, {size, align, lines});
    };
    const auto button = [&](Control control, const char *label, bool enabled = true) {
        renderFoundationButton(3400 + static_cast<int>(control), uiComponentControl(control), label, enabled, false, false, {}, 14);
    };
    text({64, 88, 850, 34}, "WEAPON MODIFICATION", 22);
    button(Control::Back, inventoryWeaponComponentsOpen_ ? "BACK TO INVENTORY" : "BACK TO SUPPLY");
    text({64, 148, 254, 28}, "SELECT WEAPON", 16);
    for (std::size_t i = 0; i < kUiComponentPageSize; ++i) {
        const auto index = componentWeaponPage_ * kUiComponentPageSize + i;
        if (index >= componentRows_.weapons.size()) break;
        const auto &r = componentRows_.weapons[index]; const auto box = uiComponentWeaponRow(i);
        renderFoundationButton(3420 + static_cast<int>(i), box, "", true, false, componentWeapon_ == r.id, {}, 14);
        renderBaseSupplyImage(r.definitionId, {box.x + 12, box.y + 8, 52, 32});
        text({box.x + 76, box.y + 4, 168, 22}, r.name, 14, UiTextAlign::Left, 1);
        text({box.x + 76, box.y + 28, 168, 18}, r.source, 12, UiTextAlign::Left, 1);
    }
    if (componentRows_.weapons.empty()) text({76, 270, 230, 70}, "NO ACCESSIBLE MODIFIABLE WEAPON", 14);
    button(Control::WeaponPrevious, "PREV", componentWeaponPage_ > 0);
    button(Control::WeaponNext, "NEXT", componentWeaponPage_ + 1 < uiComponentPageCount(componentRows_.weapons.size()));
    text({140, 553, 102, 24}, fmt::format("PAGE {}/{}", componentWeaponPage_ + 1, uiComponentPageCount(componentRows_.weapons.size())), 12, UiTextAlign::Center);

    if (componentRows_.current) {
        const auto *weapon = p.assets.find(componentWeapon_);
        renderBaseSupplyImage(weapon->definitionId, {350, 140, 444, 52});
        text({338, 194, 468, 24}, content.item(weapon->definitionId).displayName, 14, UiTextAlign::Center, 1);
    } else text({350, 155, 444, 50}, "SELECT AN ACCESSIBLE WEAPON", 14, UiTextAlign::Center);
    for (std::size_t i = 0; i < kUiComponentSlotCount; ++i) {
        const auto box = uiComponentSlot(i);
        renderFoundationButton(3440 + static_cast<int>(i), box, "", componentWeapon_ != 0, false, static_cast<std::size_t>(componentSlot_) == i, {}, 14);
        text({box.x + 10, box.y + 6, box.w - 20, 22}, uiTextRenderer_.language() == UiLanguage::English && i == 3 ? "LIGHT" : slots[i], 12, UiTextAlign::Center, 1);
        const auto *a = p.assets.find(componentRows_.installed[i]);
        text({box.x + 8, box.y + 32, box.w - 16, 18}, a ? compactUiItemName(content.item(a->definitionId), uiTextRenderer_.language()) : (uiTextRenderer_.language() == UiLanguage::English ? "DEFAULT" : "DEFAULT STRUCTURE"), 12, UiTextAlign::Center, 1);
    }
    const auto *installed = p.assets.find(componentRows_.installed[static_cast<std::size_t>(componentSlot_)]);
    text({338, 286, 468, 38}, fmt::format("CURRENT FIT: {}", installed ? localizeUiText(uiTextRenderer_.language(), content.item(installed->definitionId).displayName) : localizeUiText(uiTextRenderer_.language(), "DEFAULT STRUCTURE")), 14);
    for (std::size_t i = 0; i < kUiComponentPageSize; ++i) {
        const auto index = componentChoicePage_ * kUiComponentPageSize + i;
        if (index >= componentRows_.choices.size()) break;
        const auto &r = componentRows_.choices[index]; const auto box = uiComponentChoiceRow(i);
        renderFoundationButton(3460 + static_cast<int>(i), box, "", true, false, componentActionSelected_ && componentChoice_ == r.id, {}, 14);
        if (r.id) renderBaseSupplyImage(r.definitionId, {box.x + 12, box.y + 4, 32, 24});
        text({box.x + 54, box.y + 5, 270, 22}, r.name, 14, UiTextAlign::Left, 1);
        text({box.x + 326, box.y + 7, 130, 20}, r.source, 12, UiTextAlign::Right, 1);
    }
    button(Control::ChoicePrevious, "PREV", componentChoicePage_ > 0);
    button(Control::ChoiceNext, "NEXT", componentChoicePage_ + 1 < uiComponentPageCount(componentRows_.choices.size()));
    text({438, 553, 268, 24}, fmt::format("PAGE {}/{}", componentChoicePage_ + 1, uiComponentPageCount(componentRows_.choices.size())), 12, UiTextAlign::Center);

    text({844, 146, 358, 26}, "THIS CHANGE", 16);
    std::string operation = "VIEW CURRENT FIT ONLY";
    if (componentActionSelected_) {
        const auto *a = p.assets.find(componentChoice_);
        operation = a ? content.item(a->definitionId).displayName : "RESTORE DEFAULT";
    }
    text({844, 176, 358, 38}, operation, 14);
    const bool english = uiTextRenderer_.language() == UiLanguage::English;
    text({844, 214, 152, 20}, english ? "STAT" : "MOD ATTRIBUTE", 12);
    text({996, 214, 66, 20}, english ? "NOW" : "MOD CURRENT", 12, UiTextAlign::Right);
    text({1062, 214, 70, 20}, english ? "AFTER" : "MOD PREVIEW", 12, UiTextAlign::Right);
    text({1132, 214, 70, 20}, english ? "DELTA" : "MOD CHANGE", 12, UiTextAlign::Right);
    if (componentRows_.current) {
        const auto &current = *componentRows_.current;
        const auto stat = [&](int row, const char *label, double before, double after, bool range = false) {
            const float y = 240 + row * 21.0F;
            const auto number = [range](double value) { return range ? fmt::format("{:.1f}", value) : fmt::format("{:.0f}", value); };
            text({844, y, 152, 20}, label, 12);
            text({996, y, 66, 20}, number(before), 14, UiTextAlign::Right);
            text({1062, y, 70, 20}, plan && plan->canCommit ? number(after) : "—", 14, UiTextAlign::Right);
            text({1132, y, 70, 20}, plan && plan->canCommit ? ((after > before ? "+" : "") + number(after - before)) : "—", 14, UiTextAlign::Right, 1, !plan || !plan->canCommit || after == before);
        };
        const auto &after = plan && plan->canCommit ? plan->after : current;
        stat(0, "RECOIL CONTROL", current.recoilControl, after.recoilControl);
        stat(1, "STABILITY", current.stability, after.stability);
        stat(2, "HANDLING", current.handlingSpeed, after.handlingSpeed);
        stat(3, "ERGONOMICS", current.ergonomics, after.ergonomics);
        stat(4, "ACCURACY", current.accuracy, after.accuracy);
        stat(5, "EFFECTIVE RANGE", current.effectiveRange, after.effectiveRange, true);
        stat(6, "SHOT NOISE %", current.acoustics.noiseRadiusMultiplier * 100.0,
             after.acoustics.noiseRadiusMultiplier * 100.0);
    }
    text({844, 390, 358, 22}, "DETAILS AND RETURN", 14);
    std::vector<std::string> details;
    const auto add = [&](std::string_view value) {
        const auto localized = localizeUiText(uiTextRenderer_.language(), value);
        for (const auto &line : wrapUiText(localized, uiComponentDetails().w, 0,
            [&](std::string_view s) { return uiTextRenderer_.measure(s, 12); })) details.push_back(line.text);
    };
    if (!uiMessage_.empty()) add(uiMessage_);
    if (const auto *weapon = p.assets.find(componentWeapon_)) add("WEAPON NAME: " + localizeUiText(uiTextRenderer_.language(), content.item(weapon->definitionId).displayName));
    if (componentActionSelected_) {
        if (const auto *part = p.assets.find(componentChoice_)) add("SELECTED PART: " + localizeUiText(uiTextRenderer_.language(), content.item(part->definitionId).displayName));
    }
    if (componentRows_.current &&
        (componentRows_.current->acoustics.noiseRadiusMultiplier < 1.0F ||
         (plan && plan->canCommit && plan->after.acoustics.noiseRadiusMultiplier < 1.0F))) {
        add("NEARBY ENEMIES AND THEIR GROUP MAY STILL HEAR YOU");
        add("LOWER SHOT AUDIO IS TEMPORARY FEEDBACK");
    }
    if (plan && plan->canCommit) {
        if (plan->returned.empty()) add("NO COMPONENTS WILL BE REMOVED");
        for (const auto &r : plan->returned) {
            if (const auto *a = p.assets.find(r.assetId)) add("RETURN PART: " + localizeUiText(uiTextRenderer_.language(), content.item(a->definitionId).displayName));
            add(uiComponentStashDestination(p, content, r.destination.origin));
        }
    } else if (plan) { add("CANNOT APPLY - ITEMS UNCHANGED"); add(plan->message); }
    else add("CHOOSE A COMPONENT OR RESTORE DEFAULT TO PREVIEW");
    componentDetailScroll_ = std::clamp(componentDetailScroll_, 0, std::max(0, static_cast<int>(details.size()) - 7));
    for (int i = 0; i < 7 && componentDetailScroll_ + i < static_cast<int>(details.size()); ++i)
        text({844, 418 + 19.0F * i, 358, 19}, details[componentDetailScroll_ + i], 12, UiTextAlign::Left, 1);
    if (details.size() > 7) text({844, 553, 358, 20}, "SCROLL TO READ ALL DETAILS", 12, UiTextAlign::Right, 1);
    button(Control::Confirm, "CONFIRM COMPONENT CHANGE", plan && plan->canCommit);
    text({64, 606, 768, 44}, uiMessage_.empty() ? "BROWSE WITHOUT CHANGING | CONFIRM TO APPLY" : uiMessage_, 12);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
