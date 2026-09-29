#include "developer_test_range.h"
#include "game_flow.h"
#include "alpha_content_ids.h"
#include <cmath>
#include <type_traits>

namespace {
template<class Predicate> std::vector<ItemDefinitionId> choices(Predicate predicate)
{
    std::vector<ItemDefinitionId> result;
    for (const auto &item : publishedContentRegistry().items())
        if (item.visualAssetsPublished && predicate(item)) result.push_back(item.definitionId);
    return result;
}
bool addSupply(ProfileState &profile, const ItemDefinition &item)
{
    const auto &content = publishedContentRegistry();
    for (auto slot : {EquipmentSlotKind::Backpack, EquipmentSlotKind::ChestRig})
    {
        const auto owner = equippedAsset(profile, slot);
        if (!owner) continue;
        const auto &container = content.item(profile.assets.find(*owner)->definitionId);
        for (std::uint32_t index = 0; index < container.containerCompartments.size(); ++index)
        {
            const auto id = ProfileContainerId::compartment(*owner, index);
            const auto fit = findFirstProfileFit(profile, content, id, item, ItemOrientation::Degrees0);
            if (!fit) continue;
            static_cast<void>(profile.assets.create(item, StoredAssetLocation{id, *fit},
                item.ammunitionUse ? item.maxStackSize : 1));
            return true;
        }
    }
    return false;
}
}

std::vector<ItemDefinitionId> developerRangeWeapons()
{ return choices([](const auto &i) { return i.weaponUse.has_value(); }); }
std::vector<ItemDefinitionId> developerRangeAmmunition(const ItemDefinitionId &weapon)
{ return choices([&](const auto &i) { return i.ammunitionUse && publishedContentRegistry().ammunitionFitsWeapon(i.definitionId, weapon); }); }
std::vector<ItemDefinitionId> developerRangeArmor()
{ return choices([](const auto &i) { return i.armorProtection && i.equipmentSlot == EquipmentSlotKind::BodyArmor; }); }
std::vector<ItemDefinitionId> developerRangeSupplies()
{ return choices([](const auto &i) { return i.medicalUse || i.armorProtection || i.weaponComponent || i.weaponMaintenance || i.armorMaintenance || i.magazineUse; }); }

bool GameSession::startDeveloperRange()
{
    if (saveRepository_ || developerRange_ || profile_.pendingRaid) return false;
    RaidWorldConfig config;
    config.developerRange = true;
    config.worldSize = kDeveloperRangeSize;
    config.playerSpawn = kDeveloperRangeConsole;
    config.extractionPoint = {{40, 1050}, {140, 160}};
    config.deferPlayerDamageResolution = true;
    // Side cover leaves the firing lanes and all target pads unobstructed.
    config.ballisticBlockers = {{1, {{440, 750}, {160, 35}}}, {2, {{1300, 780}, {180, 35}}}};
    auto candidateWorld = std::make_unique<GameplayWorld>(std::move(config));
    auto candidate = makeNewAlphaProfile("developer-combat-range", publishedContentRegistry());
    candidate.assets = AssetRegistry{};
    candidate.tutorial = TutorialProgress::Complete;
    candidate.homeFounding.hintsDismissed = true;
    profile_ = std::move(candidate);
    world_ = std::move(candidateWorld);
    developerRange_ = true;
    alphaRaidActive_ = true;
    state_ = GameSessionState::InRaid;
    return true;
}

bool GameSession::developerRangeAtConsole() const noexcept
{
    if (!developerRange_ || !world_->raidSession().isActive()) return false;
    const auto position = world_->player().position();
    return std::hypot(position.x - kDeveloperRangeConsole.x,
                      position.y - kDeveloperRangeConsole.y) <= 180.0F;
}

bool GameSession::grantDeveloperRangeLoadout(const ItemDefinitionId &weaponId,
    const ItemDefinitionId &ammoId, const std::optional<ItemDefinitionId> &armorId)
{
    if (!developerRangeAtConsole()) return false;
    const auto &content = publishedContentRegistry();
    try {
        const auto &weapon = content.item(weaponId);
        const auto &ammo = content.item(ammoId);
        if (!weapon.weaponUse || !weapon.visualAssetsPublished ||
            !ammo.ammunitionUse || !content.ammunitionFitsWeapon(ammoId, weaponId)) return false;
        ProfileState candidate = profile_;
        // Replace the disposable loadout atomically; preserve the session's ID high-water mark.
        const auto nextId = candidate.assets.nextAssetId();
        candidate.assets = AssetRegistry{};
        candidate.assets.setNextAssetIdForLoad(nextId);
        const auto equip = [&](const ItemDefinitionId &id, EquipmentSlotKind slot) {
            return candidate.assets.create(content.item(id), EquippedAssetLocation{slot});
        };
        static_cast<void>(equip(alpha_content::backpack, EquipmentSlotKind::Backpack));
        static_cast<void>(equip(alpha_content::chestRig, EquipmentSlotKind::ChestRig));
        const auto weaponAsset = equip(weaponId, weapon.compatibleEquipmentSlots.front());
        const auto &mag = content.item(weapon.compatibleMagazineDefinitionIds.front());
        const auto magazineAsset = candidate.assets.create(mag, InstalledMagazineLocation{weaponAsset});
        candidate.assets.findMutable(magazineAsset)->magazineRounds.assign(mag.magazineCapacity, {ammoId, std::nullopt});
        candidate.assets.findMutable(weaponAsset)->chamberedRound = MagazineRoundRecord{ammoId, std::nullopt};
        if (armorId) {
            const auto &armor = content.item(*armorId);
            if (!armor.armorProtection || armor.equipmentSlot != EquipmentSlotKind::BodyArmor) return false;
            static_cast<void>(equip(*armorId, EquipmentSlotKind::BodyArmor));
        }
        if (!addSupply(candidate, ammo) || !addSupply(candidate, content.item(alpha_content::medkit))) return false;
        ++candidate.revision;
        if (!validateProfileState(candidate, content).valid) return false;
        raidActionState_.cancel();
        developerWeaponOverrides_.clear();
        configuredWeaponAssetId_.reset();
        profile_ = std::move(candidate);
        activeWeaponSlot_ = weapon.compatibleEquipmentSlots.front();
        synchronizeActiveAlphaWeapon();
        return true;
    } catch (...) { return false; }
}

bool GameSession::grantDeveloperRangeItem(const ItemDefinitionId &itemId)
{
    if (!developerRangeAtConsole()) return false;
    try {
        const auto &item = publishedContentRegistry().item(itemId);
        if (!item.visualAssetsPublished) return false;
        ProfileState candidate = profile_;
        if (!addSupply(candidate, item)) return false;
        ++candidate.revision;
        if (!validateProfileState(candidate, publishedContentRegistry()).valid) return false;
        profile_ = std::move(candidate);
        return true;
    } catch (...) { return false; }
}

bool GameSession::resetDeveloperRangeEnemies(const EnemyCombatDefinitionId &enemy,
    std::size_t pad, std::size_t count)
{
    if (!developerRangeAtConsole()) return false;
    try { return world_->resetDeveloperRangeEnemies(publishedContentRegistry().enemyCombatDefinition(enemy), pad, count); }
    catch (...) { return false; }
}

bool GameFlow::enterDeveloperRange()
{
    if (state_ != GameFlowState::Base || suspendedRangeSession_ || gameSession_.baseDefenseActive() ||
        gameSession_.profile().pendingRaid || !gameSession_.checkpointWorldClock()) return false;
    auto range = std::make_unique<GameSession>();
    if (!range->startDeveloperRange()) return false;
    static_assert(std::is_move_assignable_v<GameSession>);
    suspendedRangeSession_ = std::make_unique<GameSession>(std::move(gameSession_));
    gameSession_ = std::move(*range);
    activeBaseFacility_.reset();
    state_ = GameFlowState::Raid;
    return true;
}

bool GameFlow::leaveDeveloperRange() noexcept
{
    if (!suspendedRangeSession_) return false;
    gameSession_ = std::move(*suspendedRangeSession_);
    suspendedRangeSession_.reset();
    state_ = GameFlowState::Base;
    return true;
}
