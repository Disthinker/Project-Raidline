#include "weapon_supply_projection.h"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

#include "maintenance_domain.h"
#include "raid_action.h"

namespace {
const ItemDefinition *knownItem(const ContentRegistry &content,
                                const ItemDefinitionId &id) {
  try {
    return &content.item(id);
  } catch (const std::out_of_range &) {
    return nullptr;
  }
}

struct EquippedRig {
  AssetInstanceId id{};
  const ItemDefinition *definition{};
};

EquippedRig equippedRig(const ProfileState &profile,
                        const ContentRegistry &content) {
  const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
  const AssetRecord *rig =
      chest.has_value() ? profile.assets.find(*chest) : nullptr;
  return {chest.value_or(0U),
          rig == nullptr ? nullptr : knownItem(content, rig->definitionId)};
}

bool quickPocket(const EquippedRig &rig, const AssetRecord &asset) {
  const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
  if (rig.definition == nullptr || stored == nullptr ||
      stored->container.kind != ProfileContainerKind::AssetCompartment ||
      stored->container.ownerAssetId != rig.id) {
    return false;
  }
  return stored->container.compartmentIndex <
             rig.definition->containerCompartments.size() &&
         rig.definition
                 ->containerCompartments[stored->container.compartmentIndex]
                 .pocketKind == ContainerPocketKind::MagazineOnly;
}

bool hasSuggestion(const WeaponSupplyReadiness &readiness,
                   WeaponSupplySuggestionKind kind) {
  return std::any_of(readiness.suggestions.begin(), readiness.suggestions.end(),
                     [kind](const WeaponSupplySuggestion &suggestion) {
                       return suggestion.kind == kind;
                     });
}

struct RigMoveSearch {
  std::optional<WeaponSupplySuggestion> suggestion;
  bool anyGeometricFit{};
};

RigMoveSearch moveToRig(const ProfileState &profile,
                        const ContentRegistry &content, const EquippedRig &rig,
                        const AssetRecord &magazine) {
  const ItemDefinition *magazineDefinition =
      knownItem(content, magazine.definitionId);
  if (rig.definition == nullptr || magazineDefinition == nullptr)
    return {};
  RigMoveSearch result;

  constexpr std::array orientations{
      ItemOrientation::Degrees0, ItemOrientation::Degrees90,
      ItemOrientation::Degrees180, ItemOrientation::Degrees270};
  for (std::size_t index{};
       index < rig.definition->containerCompartments.size(); ++index) {
    if (rig.definition->containerCompartments[index].pocketKind !=
        ContainerPocketKind::MagazineOnly)
      continue;
    const ProfileContainerId pocket = ProfileContainerId::compartment(
        rig.id, static_cast<std::uint32_t>(index));
    for (const ItemOrientation orientation : orientations) {
      if (!canUseItemOrientation(*magazineDefinition, orientation))
        continue;
      const auto origin =
          findFirstProfileFit(profile, content, pocket, *magazineDefinition,
                              orientation, magazine.instanceId);
      if (!origin.has_value())
        continue;
      result.anyGeometricFit = true;
      const StoredAssetLocation destination{pocket, *origin};
      if (queryInventory(profile, content,
                         InventoryMoveCommand{magazine.instanceId, 0U,
                                              destination, orientation})
              .canCommit) {
        result.suggestion = WeaponSupplySuggestion{
            WeaponSupplySuggestionKind::MoveSpareToChestRig,
            magazine.instanceId, rig.id, destination, orientation};
        return result;
      }
    }
  }
  return result;
}

WeaponSupplyReadiness projectWeapon(const ProfileState &profile,
                                    const ContentRegistry &content,
                                    EquipmentSlotKind slot,
                                    AssetInstanceId weaponId,
                                    bool includeStash) {
  WeaponSupplyReadiness result;
  result.slot = slot;
  result.weaponAssetId = weaponId;
  const AssetRecord *weapon = profile.assets.find(weaponId);
  if (weapon == nullptr)
    return result;
  result.weaponDefinitionId = weapon->definitionId;
  const ItemDefinition *definition = knownItem(content, weapon->definitionId);
  if (definition == nullptr || definition->category != ItemCategory::Weapon)
    return result;
  result.definitionKnown = true;
  const WeaponAmmoPlan fire =
      queryFireWeapon(profile, content, FireWeaponCommand{weaponId});
  result.fireResult = fire.result;
  result.canFireNow = fire.canCommit && fire.result == WeaponAmmoResult::Fired;
  result.chamberedRounds = weapon->chamberedRound.has_value() ? 1U : 0U;
  result.installedMagazineId = installedMagazine(profile, weaponId);
  if (result.installedMagazineId.has_value())
    result.installedMagazineRounds =
        magazineRoundCount(profile, *result.installedMagazineId);
  result.quickReloadMagazineId =
      selectRaidReloadMagazine(profile, content, weaponId);
  if (result.quickReloadMagazineId.has_value()) {
    result.quickReloadMagazineRounds =
        magazineRoundCount(profile, *result.quickReloadMagazineId);
    result.quickReloadCanInstall =
        queryWeaponAmmo(profile, content,
                        InstallMagazineAndChamberCommand{
                            weaponId, *result.quickReloadMagazineId})
            .canCommit;
  }

  if (fire.result == WeaponAmmoResult::BlockedByMalfunction &&
      queryWeaponAmmo(profile, content, ClearWeaponMalfunctionCommand{weaponId})
          .canCommit) {
    result.suggestions.push_back(
        {WeaponSupplySuggestionKind::ClearMalfunction, weaponId, weaponId});
  } else if (fire.result == WeaponAmmoResult::Chambered &&
             queryWeaponAmmo(profile, content, ChamberWeaponCommand{weaponId})
                 .canCommit) {
    result.suggestions.push_back(
        {WeaponSupplySuggestionKind::ChamberWeapon, weaponId, weaponId});
  }

  std::vector<const AssetRecord *> magazines;
  std::vector<const AssetRecord *> ammunition;
  const EquippedRig rig = equippedRig(profile, content);
  for (const auto &[id, asset] : profile.assets.records()) {
    const ItemDefinition *item = knownItem(content, asset.definitionId);
    if (item == nullptr)
      continue;
    const bool carried = assetIsCarried(profile, id);
    if (!carried && !(includeStash && assetIsBaseAccessible(profile, id)))
      continue;
    if (item->category == ItemCategory::Ammunition &&
        content.ammunitionFitsWeapon(asset.definitionId,
                                     weapon->definitionId)) {
      ammunition.push_back(&asset);
      if (carried)
        result.carriedLooseRounds += asset.quantity;
    } else if (item->category == ItemCategory::Magazine &&
               content.magazineFitsWeapon(asset.definitionId,
                                          weapon->definitionId)) {
      // Only stored magazines are spare/drag targets. Do not offer
      // loading or stealing a magazine installed in another weapon.
      const bool stored =
          std::holds_alternative<StoredAssetLocation>(asset.location);
      if (stored || result.installedMagazineId == id)
        magazines.push_back(&asset);
      if (stored && carried && !asset.magazineRounds.empty()) {
        if (quickPocket(rig, asset)) {
          ++result.quickReloadLoadedMagazineCount;
          result.quickReloadLoadedRounds += asset.magazineRounds.size();
        } else {
          ++result.otherLoadedSpareMagazineCount;
          result.otherLoadedSpareMagazineRounds += asset.magazineRounds.size();
        }
      }
    }
    if ((fire.result == WeaponAmmoResult::Broken ||
         fire.result == WeaponAmmoResult::BlockedByMalfunction) &&
        item->weaponMaintenance.has_value() &&
        !hasSuggestion(result, WeaponSupplySuggestionKind::MaintainWeapon) &&
        queryWeaponMaintenance(
            profile, content,
            WeaponMaintenanceCommand{
                id, weaponId,
                includeStash ? MaintenanceAccess::AnyOwned
                             : MaintenanceAccess::CarriedOnly,
                profile.pendingRaid.has_value() ? MaintenanceLocation::Raid
                                                : MaintenanceLocation::Base})
            .canCommit) {
      result.suggestions.push_back(
          {WeaponSupplySuggestionKind::MaintainWeapon, id, weaponId});
    }
  }

  // Prefer the installed magazine, then loaded spare magazines, then
  // stable ID. The projection never advances a random stream.
  std::stable_sort(
      magazines.begin(), magazines.end(),
      [&result](const AssetRecord *left, const AssetRecord *right) {
        const bool leftInstalled =
            result.installedMagazineId == left->instanceId;
        const bool rightInstalled =
            result.installedMagazineId == right->instanceId;
        if (leftInstalled != rightInstalled)
          return leftInstalled;
        if (left->magazineRounds.size() != right->magazineRounds.size())
          return left->magazineRounds.size() > right->magazineRounds.size();
        return left->instanceId < right->instanceId;
      });
  std::set<ItemDefinitionId> noRigSpace;
  for (const AssetRecord *magazine : magazines) {
    // Exclude obviously full magazines before the authoritative query:
    // queryWeaponAmmo copies the registry, so a full stash must never
    // cause one copy per magazine x ammunition-stack pair.
    const ItemDefinition &magazineDefinition =
        content.item(magazine->definitionId);
    if (magazine->magazineRounds.size() < magazineDefinition.magazineCapacity &&
        !hasSuggestion(result, WeaponSupplySuggestionKind::LoadMagazine)) {
      for (const AssetRecord *rounds : ammunition) {
        if (rounds->quantity == 0U ||
            !content.ammunitionFitsMagazine(rounds->definitionId,
                                            magazine->definitionId))
          continue;
        if (queryWeaponAmmo(profile, content,
                            LoadMagazineCommand{magazine->instanceId,
                                                rounds->instanceId, 0U})
                .canCommit) {
          result.suggestions.push_back(
              {WeaponSupplySuggestionKind::LoadMagazine, rounds->instanceId,
               magazine->instanceId});
          break;
        }
      }
    }
    if (magazine->magazineRounds.empty() ||
        !std::holds_alternative<StoredAssetLocation>(magazine->location))
      continue;
    if (!hasSuggestion(result, WeaponSupplySuggestionKind::InstallMagazine) &&
        (result.installedMagazineRounds == 0U ||
         !result.installedMagazineId.has_value()) &&
        queryWeaponAmmo(
            profile, content,
            InstallMagazineAndChamberCommand{weaponId, magazine->instanceId})
            .canCommit) {
      result.suggestions.push_back({WeaponSupplySuggestionKind::InstallMagazine,
                                    magazine->instanceId, weaponId});
    }
    if (result.quickReloadLoadedMagazineCount == 0U &&
        !quickPocket(rig, *magazine) &&
        !noRigSpace.contains(magazine->definitionId) &&
        !hasSuggestion(result,
                       WeaponSupplySuggestionKind::MoveSpareToChestRig)) {
      const RigMoveSearch search = moveToRig(profile, content, rig, *magazine);
      if (search.suggestion.has_value())
        result.suggestions.push_back(*search.suggestion);
      else if (!search.anyGeometricFit)
        noRigSpace.insert(magazine->definitionId);
    }
  }
  return result;
}
} // namespace

WeaponSupplyProjection projectWeaponSupply(const ProfileState &profile,
                                           const ContentRegistry &content,
                                           bool includeStash) {
  WeaponSupplyProjection result;
  result.revision = profile.revision;
  result.hasBodyArmor =
      equippedAsset(profile, EquipmentSlotKind::BodyArmor).has_value();
  result.hasChestRig =
      equippedAsset(profile, EquipmentSlotKind::ChestRig).has_value();
  result.hasBackpack =
      equippedAsset(profile, EquipmentSlotKind::Backpack).has_value();
  for (const auto &[id, asset] : profile.assets.records()) {
    const ItemDefinition *definition = knownItem(content, asset.definitionId);
    if (definition != nullptr && definition->medicalUse.has_value() &&
        asset.remainingCharges > 0U && assetIsCarried(profile, id)) {
      result.hasCarriedMedical = true;
      break;
    }
  }
  constexpr std::array slots{EquipmentSlotKind::PrimaryWeapon,
                             EquipmentSlotKind::SecondaryWeapon,
                             EquipmentSlotKind::Sidearm};
  for (const EquipmentSlotKind slot : slots) {
    if (const auto weapon = equippedAsset(profile, slot)) {
      result.weapons.push_back(
          projectWeapon(profile, content, slot, *weapon, includeStash));
      result.anyWeaponCanFire =
          result.anyWeaponCanFire || result.weapons.back().canFireNow;
    }
  }
  return result;
}
