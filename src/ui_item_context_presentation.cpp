#include "ui_item_context_presentation.h"
#include "medical_domain.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

std::vector<UiItemActionEntry> projectUiItemActions(
    const ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, bool inRaid) {
    const auto *asset = profile.assets.find(id);
    if (!asset) return {};
    const auto &definition = content.item(asset->definitionId);
    std::vector<UiItemActionEntry> result;
    if (definition.weaponUse)
        result = {{UiItemAction::WeaponDetails, "WEAPON DETAILS"},
                  {UiItemAction::ModifyWeapon, "MODIFY WEAPON"}};
    if (definition.meleeUse)
        result = {{UiItemAction::MeleeDetails, "MELEE DETAILS"}};
    if (definition.noiseDecoyUse)
        result = {{UiItemAction::DecoyDetails, "DECOY DETAILS"}};
    if (const auto action = queryProfileContextAction(profile, content, id, inRaid)) {
        switch (*action) {
        case ProfileContextActionKind::UnloadMagazine:
            result.push_back({UiItemAction::UnloadMagazine,
                inRaid ? "UNLOAD MAGAZINE (3 SEC)" : "UNLOAD ALL TO STASH"});
            break;
        case ProfileContextActionKind::ChamberWeapon:
            result.push_back({UiItemAction::ChamberRound, "CHAMBER ROUND"});
            break;
        case ProfileContextActionKind::UseMedkit: {
            const auto plan = queryMedicalUse(profile, content, id,
                inRaid ? MedicalAccess::CarriedOnly : MedicalAccess::AnyOwned);
            const char *label = "USE MEDKIT";
            switch (plan.effect) {
            case MedicalUseEffect::RestoreHealth:
                label = inRaid ? "USE MEDKIT (5 SEC)" : "USE MEDKIT"; break;
            case MedicalUseEffect::StopLightBleeding:
                label = inRaid ? "APPLY BANDAGE (2 SEC)" : "APPLY BANDAGE"; break;
            case MedicalUseEffect::StopAnyBleeding:
                label = inRaid ? "APPLY TOURNIQUET (4 SEC)" : "APPLY TOURNIQUET"; break;
            case MedicalUseEffect::SuppressPain:
                label = inRaid ? "TAKE PAINKILLER (2 SEC)" : "TAKE PAINKILLER"; break;
            }
            result.push_back({UiItemAction::UseMedical, label});
            break;
        }
        }
    }
    return result;
}

std::vector<std::string> projectUiItemSummary(
    const AssetRecord &asset, const ItemDefinition &definition) {
    const auto footprint = inventoryFootprint(definition, asset.orientation);
    std::vector<std::string> lines{
        fmt::format("ITEM SIZE {} x {} | QUANTITY {}", footprint.width,
                    footprint.height, asset.quantity),
        fmt::format("WEIGHT {:.2f} KG", definition.unitWeightGrams / 1000.0)};
    if (definition.armorProtection) {
        const auto &armor = *definition.armorProtection;
        lines.push_back(fmt::format("PROTECTION {} | {}", armor.protectionRequirement,
            armor.material == ArmorMaterial::Soft ? "SOFT" :
            armor.material == ArmorMaterial::Composite ? "COMPOSITE" : "METAL"));
    }
    if (asset.currentMaximumDurability) {
        if (definition.weaponCondition || definition.meleeUse)
            lines.push_back(fmt::format("DURABILITY {:.2f}/{:.2f}",
                asset.currentDurability / 100.0, asset.currentMaximumDurability / 100.0));
        else
            lines.push_back(fmt::format("DURABILITY {}/{}", asset.currentDurability,
                                       asset.currentMaximumDurability));
    }
    if (!definition.containerCompartments.empty()) {
        std::uint32_t cells{};
        std::size_t magazine{}, general{};
        for (const auto &pocket : definition.containerCompartments) {
            cells += static_cast<std::uint32_t>(pocket.width * pocket.height);
            if (pocket.pocketKind == ContainerPocketKind::MagazineOnly) ++magazine;
            else ++general;
        }
        lines.push_back(fmt::format("CAPACITY {} CELLS", cells));
        lines.push_back(fmt::format("POCKETS {} MAG / {} UTIL", magazine, general));
    }
    if (definition.medicalUse)
        lines.push_back(fmt::format("REMAINING USES {}", asset.remainingCharges));
    return lines;
}

namespace {
constexpr float padding = 16.0F;
float heightFor(std::string_view text, float width, int size,
                UiLanguage language, const UiItemMeasure &measure) {
    const auto localized = localizeUiText(language, text);
    const auto lines = wrapUiText(localized, width, 0,
        [&](auto value) { return measure(value, size); });
    return std::ceil(std::max(std::size_t{1}, lines.size()) * size * 1.4F) + 1;
}
void translate(SDL_FRect &rect, float x, float y) {
    rect.x += x; rect.y += y;
}
}

std::optional<std::size_t> UiItemContextLayout::rowAt(Vec2 point) const noexcept {
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (uiContains(rows[i].bounds, point)) return i;
    return std::nullopt;
}
UiItemContextLayout layoutUiItemContext(
    Vec2 anchor, std::string_view title,
    const std::vector<UiItemActionEntry> &actions, UiLanguage language,
    const UiItemMeasure &measure) {
    float width = 280;
    for (const auto &action : actions)
        width = std::max(width, measure(localizeUiText(language, action.label), 18) + 32);
    width = std::clamp(width, 280.0F, 360.0F);
    UiItemContextLayout layout;
    const float titleHeight = heightFor(title, width - 32, 18, language, measure);
    layout.title = {{16, 12, width - 32, titleHeight}, std::string(title), 18};
    float y = titleHeight + 24;
    for (const auto &action : actions) {
        const float height = std::max(44.0F,
            std::ceil(heightFor(action.label, (width - 40) / 1.025F,
                                18, language, measure) * 1.025F) + 16);
        layout.rows.push_back({{8, y, width - 16, height}, action.label, 18});
        y += height;
    }
    layout.panel = {std::clamp(anchor.x, padding, 1280 - padding - width),
                    std::clamp(anchor.y, padding, std::max(padding, 720 - padding - y - 8)),
                    width, y + 8};
    translate(layout.title.bounds, layout.panel.x, layout.panel.y);
    for (auto &row : layout.rows) translate(row.bounds, layout.panel.x, layout.panel.y);
    return layout;
}

UiItemCardLayout layoutUiItemCard(
    SDL_FRect target, std::string_view title,
    const std::vector<std::string> &summary, const std::vector<std::string> &supply,
    UiLanguage language, const UiItemMeasure &measure) {
    UiItemCardLayout layout;
    // Widen before clipping: all current supply facts remain in one card.
    const std::vector<float> widths = supply.empty()
        ? std::vector<float>{360, 440, 520, 680, 840, 1000, 1248}
        : std::vector<float>{640, 720, 800, 880, 1000, 1248};
    for (const float width : widths) {
        layout = {};
        const auto add = [&](const std::string &value, float x, float y,
                             float w, int size) {
            const float h = heightFor(value, w, size, language, measure);
            layout.text.push_back({{x, y, w, h}, value, size});
            return h;
        };
        float y = padding;
        y += add(std::string(title), padding, y, width - 32, 18) + 10;
        const float left = supply.empty() ? width - 32 : 224;
        layout.image = {padding, y, left, 72};
        float leftY = y + 82;
        for (const auto &line : summary)
            leftY += add(line, padding, leftY, left, 16) + 4;
        float rightY = y;
        if (!supply.empty()) {
            const float x = left + 32;
            for (const auto &line : supply)
                rightY += add(line, x, rightY, width - x - padding, 16) + 6;
        }
        layout.panel = {0, 0, width, std::max(leftY, rightY) + 12};
        if (layout.panel.h <= 688) break;
    }
    const float width = layout.panel.w, height = layout.panel.h;
    float x = target.x + target.w + 16;
    if (x + width > 1264) x = target.x - width - 16;
    x = std::clamp(x, padding, 1280 - padding - width);
    const float y = std::clamp(target.y, padding, std::max(padding, 720 - padding - height));
    layout.panel.x = x; layout.panel.y = y;
    translate(layout.image, x, y);
    for (auto &block : layout.text) translate(block.bounds, x, y);
    return layout;
}

void UiItemHoverState::update(AssetInstanceId id, SDL_FRect bounds, float seconds) noexcept {
    if (!id) { *this = {}; return; }
    if (id != instanceId) { instanceId = id; elapsed = 0; anchor = bounds; }
    elapsed += std::max(0.0F, seconds);
}
bool UiItemHoverState::visible() const noexcept { return instanceId && elapsed >= 0.4F; }
float UiItemHoverState::fade() const noexcept { return uiHoverAmount(elapsed - 0.4F); }
void UiItemMenuPress::down(std::optional<UiItemAction> id) noexcept {
    if (held) return;
    held = true; action = id;
}
std::optional<UiItemAction> UiItemMenuPress::up(std::optional<UiItemAction> id) noexcept {
    const auto result = held && id == action ? action : std::nullopt;
    *this = {};
    return result;
}

std::vector<std::string> projectUiWeaponSupplyLines(
    const WeaponSupplyReadiness &weapon, const WeaponSupplyProjection &supply,
    bool inRaid, EquipmentSlotKind activeSlot) {
  std::vector<std::string> lines;
  if (!weapon.definitionKnown)
    lines.emplace_back("UNKNOWN WEAPON DEFINITION - CHECK CONTENT");
  else if (weapon.canFireNow)
    lines.emplace_back("FIRE READY NOW | NEXT SHOT USES THE CHAMBERED ROUND");
  else if (weapon.fireResult == WeaponAmmoResult::Broken)
    lines.emplace_back("CANNOT FIRE: BROKEN WEAPON | MAINTENANCE REQUIRED");
  else if (weapon.fireResult == WeaponAmmoResult::BlockedByMalfunction)
    lines.emplace_back("CANNOT FIRE: JAMMED | CLEAR THE MALFUNCTION");
  else if (weapon.fireResult == WeaponAmmoResult::Chambered)
    lines.emplace_back("CHAMBER EMPTY | NEXT FIRE INPUT ONLY CHAMBERS A ROUND");
  else
    lines.emplace_back(
        "CANNOT FIRE: NO FEED | LOOSE OR SPARE AMMO IS NOT LOADED");
  lines.push_back(
      fmt::format("CURRENT CHAMBER {} | INSTALLED MAG {} | CARRIED LOOSE {}",
                  weapon.chamberedRounds, weapon.installedMagazineRounds,
                  weapon.carriedLooseRounds));
  lines.push_back(
      weapon.quickReloadMagazineId
          ? fmt::format(
                "R CANDIDATE: {} ROUNDS | {}", weapon.quickReloadMagazineRounds,
                weapon.quickReloadCanInstall ? "CAN RELOAD" : "CANNOT INSTALL")
          : "R CANDIDATE: NONE | USE CHEST RIG MAGAZINE POCKETS");
  lines.push_back(
      fmt::format("OTHER LOADED SPARES {} / {} ROUNDS | NOT R-READY",
                  weapon.otherLoadedSpareMagazineCount,
                  weapon.otherLoadedSpareMagazineRounds));
  if (inRaid && activeSlot != weapon.slot)
    lines.emplace_back(
        "CLOSE TAB; SELECT THIS WEAPON BEFORE RELOADING OR CLEARING");
  for (const auto &suggestion : weapon.suggestions) {
    switch (suggestion.kind) {
    case WeaponSupplySuggestionKind::LoadMagazine:
      lines.emplace_back("DRAG COMPATIBLE AMMO TO A MAGAZINE TO LOAD IT");
      break;
    case WeaponSupplySuggestionKind::InstallMagazine:
      lines.emplace_back(
          "DRAG THE MAGAZINE TO THE WEAPON TO INSTALL AND CHAMBER");
      break;
    case WeaponSupplySuggestionKind::ChamberWeapon:
      lines.emplace_back(
          inRaid ? "CLOSE TAB; USE FIRE ONCE TO CHAMBER, AGAIN TO SHOOT"
                 : "RIGHT-CLICK THE WEAPON AND CHOOSE CHAMBER");
      break;
    case WeaponSupplySuggestionKind::MoveSpareToChestRig:
      lines.emplace_back("MOVE A LOADED SPARE TO A CHEST RIG MAGAZINE POCKET");
      break;
    case WeaponSupplySuggestionKind::ClearMalfunction:
      lines.emplace_back("CLOSE TAB; TRY FIRE, THEN SHAKE THE MOUSE TO CLEAR");
      break;
    case WeaponSupplySuggestionKind::MaintainWeapon:
      lines.emplace_back("USE A MAINTENANCE KIT OR THE BASE GUNSMITH");
      break;
    }
  }
  if (!supply.hasCarriedMedical)
    lines.emplace_back(
        "OPTIONAL: CARRY MEDICINE; RIG MEDICINE IS AVAILABLE ON 5");
  if (!supply.hasBodyArmor)
    lines.emplace_back(
        "OPTIONAL: EQUIP BODY ARMOR; THIS DOES NOT BLOCK DEPLOY");
  return lines;
}
