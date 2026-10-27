#include "ui_item_details_presentation.h"

#include "weapon_component_domain.h"
#include "ui_text_renderer.h"

#include <algorithm>
#include <fmt/format.h>

namespace {
std::string label(UiLanguage language, const char *chinese, const char *english) {
    return language == UiLanguage::SimplifiedChinese ? chinese : english;
}
} // namespace

std::optional<UiItemDetailsProjection> projectUiItemDetails(
    const ProfileState &profile, const ContentRegistry &content,
    AssetInstanceId id, UiLanguage language) {
    const auto *asset = profile.assets.find(id);
    if (!asset) return std::nullopt;
    const auto &definition = content.item(asset->definitionId);
    if (!definition.weaponUse && !definition.meleeUse && !definition.noiseDecoyUse && !definition.grenadeUse)
        return std::nullopt;
    UiItemDetailsProjection result;
    result.instanceId = id;
    result.definitionId = definition.definitionId;
    result.name = localizeUiText(language, definition.displayName);
    const auto metric = [&](const char *zh, const char *en, std::string value) {
        result.metrics.push_back({label(language, zh, en), std::move(value)});
    };
    if (definition.meleeUse) {
        const auto &use = *definition.meleeUse;
        result.kind = UiItemDetailsKind::Melee;
        result.title = label(language, "近战详情", "MELEE DETAILS");
        metric("耐久", "Durability", fmt::format("{:.1f} / {:.1f}", asset->currentDurability / 100.0F, asset->currentMaximumDurability / 100.0F));
        metric("伤害", "Damage", fmt::format("{}", use.damage));
        metric("距离 / 角度", "Reach / arc", fmt::format("{:.0f} / {:.0f}°", use.range, use.angleDegrees));
        metric("恢复", "Recovery", fmt::format("{:.2f} s", use.recoverySeconds));
        metric("重量", "Weight", fmt::format("{} g", definition.unitWeightGrams));
        metric("尺寸", "Size", fmt::format("{} × {}", definition.inventoryWidthCells, definition.inventoryHeightCells));
        result.warning = asset->currentDurability == 0;
        result.status = label(language, result.warning ? "刀具已损坏 · 回基地维护" : "可用于贴身自卫", result.warning ? "Broken · service at base" : "Ready for close self-defense");
        result.notes = {
            label(language, "装备到快速近战栏，按 V 使用；保留当前枪械。", "Equip in Quick Melee; press V without switching firearms."),
            label(language, "单次命中一个目标；墙体阻挡，护甲减伤。", "One target per swing; walls block and armor reduces damage."),
            label(language, "命中才磨损；局内不能维护，不造成连续硬直。", "Wear on contact only; no raid repairs or stun lock.")};
    } else if (definition.grenadeUse) {
        const auto &use = *definition.grenadeUse;
        result.kind = UiItemDetailsKind::Grenade;
        result.title = label(language, "手雷详情", "GRENADE DETAILS");
        metric("数量", "Quantity", fmt::format("{}", asset->quantity));
        metric("单件重量", "Unit weight", fmt::format("{} g", definition.unitWeightGrams));
        metric("投掷距离", "Throw range", fmt::format("{:.0f}", use.range));
        metric("飞行 / 落地引信", "Flight / ground fuse", fmt::format("{:.1f} / {:.1f} s", use.flightSeconds, use.fuseSeconds));
        metric("核心 / 边缘伤害", "Core / edge damage", fmt::format("{} / {}", use.coreDamage, use.edgeDamage));
        metric("伤害半径", "Damage radius", fmt::format("{:.0f}", use.radius));
        result.warning = true;
        result.status = label(language, "普通对局使用 · 爆炸可能伤及自己", "Regular raids · the explosion can hurt you");
        result.notes = {
            label(language, "放入胸挂；按住4选择，松开瞄准，左键投掷。", "Carry in the rig; hold 4, release to aim, click to throw."),
            label(language, "右键或Esc取消；成功投掷才消耗一枚。", "Right-click or Esc cancels; only a successful throw consumes one."),
            language == UiLanguage::SimplifiedChinese ?
                fmt::format("穿甲{} / 甲伤{}；爆伤可自伤，墙体阻挡。", use.penetration, use.armorDamage) :
                fmt::format("Penetration {} / armor damage {}; walls block blast damage, including self-damage.", use.penetration, use.armorDamage),
            label(language, "未爆前不能进出建筑或开始撤离。", "Doors and new extraction progress are blocked until it explodes.")};
    } else if (definition.noiseDecoyUse) {
        const auto &use = *definition.noiseDecoyUse;
        result.kind = UiItemDetailsKind::Decoy;
        result.title = label(language, "诱饵详情", "DECOY DETAILS");
        metric("数量", "Quantity", fmt::format("{}", asset->quantity));
        metric("单件重量", "Unit weight", fmt::format("{} g", definition.unitWeightGrams));
        metric("投掷距离", "Throw range", fmt::format("{:.0f}", use.range));
        metric("声音范围", "Sound radius", fmt::format("{:.0f}", use.radius));
        metric("持续", "Duration", fmt::format("{:.1f} s", use.durationSeconds));
        metric("尺寸", "Size", fmt::format("{} × {}", definition.inventoryWidthCells, definition.inventoryHeightCells));
        result.status = label(language, "普通出击使用 · 确认投掷后消耗", "Regular raids · consumed on a successful throw");
        result.notes = {
            label(language, "放入胸挂；按住 4 选择，松开后瞄准，左键投掷。", "Carry in the rig; hold 4 to select, release to aim, click to throw."),
            label(language, "右键或 Esc 取消；声音不穿墙、不跨空间。", "Right-click or Esc cancels; sound stays in the current space and does not cross walls."),
            label(language, "吸引未发现你的普通感染者；不会解除追击。", "Attracts unaware common infected; does not end an active pursuit."),
            label(language, "不造成伤害，不提供照明；使用不消耗电池。", "No damage or illumination; using a decoy consumes no battery.")};
    } else {
        const auto use = effectiveWeaponUse(profile, content, id);
        result.title = label(language, "枪械详情", "WEAPON DETAILS");
        metric("后坐控制", "Recoil control", fmt::format("{}", use.recoilControl));
        metric("稳定", "Stability", fmt::format("{}", use.stability));
        metric("操控", "Handling", fmt::format("{}", use.handlingSpeed));
        metric("人机工效", "Ergonomics", fmt::format("{}", use.ergonomics));
        metric("精度", "Accuracy", fmt::format("{}", use.accuracy));
        metric("有效距离", "Effective range", fmt::format("{:.0f}", use.effectiveRange));
        for (const auto &[partId, part] : profile.assets.records()) {
            const auto *location = std::get_if<InstalledWeaponComponentLocation>(&part.location);
            if (location && location->weaponAssetId == id)
                result.components.push_back(localizeUiText(language, content.item(part.definitionId).displayName));
        }
        result.status = label(language, "属性包含当前已装部件", "Values include installed components");
    }
    return result;
}

SDL_FRect uiItemDetailsPanel() noexcept { return {40, 48, 1200, 656}; }
SDL_FRect uiItemDetailsBack() noexcept { return {860, 92, 340, 32}; }
SDL_FRect uiItemDetailsImage(UiItemDetailsKind kind) noexcept {
    if (kind == UiItemDetailsKind::Decoy || kind == UiItemDetailsKind::Grenade) return {80, 190, 490, 102};
    return kind == UiItemDetailsKind::Weapon ? SDL_FRect{80, 190, 490, 124} : SDL_FRect{80, 190, 490, 246};
}
SDL_FRect uiItemDetailsMetric(UiItemDetailsKind kind, std::size_t index) noexcept {
    if (kind == UiItemDetailsKind::Weapon)
        return {70.0F + static_cast<float>(index % 3) * 180.0F,
                334.0F + static_cast<float>(index / 3) * 58.0F, 166, 48};
    return {640.0F + static_cast<float>(index % 2) * 282.0F,
            190.0F + static_cast<float>(index / 2) * 82.0F, 266, 68};
}
SDL_FRect uiItemDetailsBattery(std::size_t index) noexcept {
    return {70 + 110.0F * static_cast<float>(index), 490, 104, 70};
}
SDL_FRect uiItemDetailsBatterySlot() noexcept { return {1090, 350, 96, 76}; }
SDL_FRect uiItemDetailsRemoveBattery() noexcept { return {640, 420, 150, 30}; }
SDL_FRect uiItemDetailsPrevious() noexcept { return {70, 602, 100, 32}; }
SDL_FRect uiItemDetailsNext() noexcept { return {190, 602, 100, 32}; }
SDL_FRect uiItemDetailsFeedback(UiItemDetailsKind kind) noexcept {
    if (kind == UiItemDetailsKind::Weapon) return {70, 644, 1120, 52};
    if (kind == UiItemDetailsKind::Melee) return {70, 654, 1120, 42};
    return {70, 662, 1120, 34};
}

std::optional<UiItemDetailsControl> hitUiItemDetailsControl(
    Vec2 point, bool isWeapon, std::size_t page, std::size_t batteries,
    bool canRemove) noexcept {
    if (uiContains(uiItemDetailsBack(), point)) return UiItemDetailsControl::Back;
    if (!isWeapon) return std::nullopt;
    if (page && uiContains(uiItemDetailsPrevious(), point)) return UiItemDetailsControl::Previous;
    if ((page + 1) * kUiDetailsBatteryPageSize < batteries && uiContains(uiItemDetailsNext(), point)) return UiItemDetailsControl::Next;
    if (canRemove && uiContains(uiItemDetailsRemoveBattery(), point)) return UiItemDetailsControl::RemoveBattery;
    return std::nullopt;
}

bool UiItemDetailsInteraction::synchronize(const UiItemDetailsIdentity &current) noexcept {
    if (identity == current) return false;
    identity = current;
    pressed.reset();
    held = false;
    return true;
}
bool UiItemDetailsInteraction::down(std::optional<UiItemDetailsControl> control) noexcept {
    if (held) return false;
    held = true;
    pressed = control;
    return true;
}
void UiItemDetailsInteraction::move(std::optional<UiItemDetailsControl> control) noexcept {
    if (pressed != control) pressed.reset();
}
std::optional<UiItemDetailsControl> UiItemDetailsInteraction::up(
    std::optional<UiItemDetailsControl> control) noexcept {
    const auto result = held && pressed == control ? pressed : std::nullopt;
    held = false;
    pressed.reset();
    return result;
}

void renderUiItemDetailsFeedback(SDL_Renderer *renderer, UiTextRenderer &text,
    UiItemDetailsInteraction &state, UiItemDetailsKind kind, std::string_view message) {
    const auto localized = localizeUiText(text.language(), message);
    if (state.feedbackKey != localized) {
        state.feedbackKey = localized;
        state.feedbackScroll = 0;
    }
    const auto bounds = uiItemDetailsFeedback(kind);
    const auto lines = wrapUiText(localized, bounds.w - 116, 0,
        [&](std::string_view line) { return text.measure(line, 12); });
    const auto visible = std::max(1, static_cast<int>(bounds.h / 17));
    state.feedbackMaximum = std::max(0, static_cast<int>(lines.size()) - visible);
    state.feedbackScroll = std::clamp(state.feedbackScroll, 0, state.feedbackMaximum);
    for (int row = 0; row < visible && state.feedbackScroll + row < static_cast<int>(lines.size()); ++row)
        uiFinalText(renderer, text, {bounds.x, bounds.y + row * 17.0F, bounds.w - 116, 18},
            lines[state.feedbackScroll + row].text, 12, 1, {238, 205, 144, 255});
    if (state.feedbackMaximum)
        uiFinalText(renderer, text, {bounds.x + bounds.w - 108, bounds.y, 108, bounds.h},
            label(text.language(), "滚轮查看", "Scroll to read"), 12, 2, {157, 181, 174, 255});
}
