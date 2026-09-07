#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "weapon_ammo_domain.h"

enum class WeaponSupplySuggestionKind {
  LoadMagazine,
  InstallMagazine,
  ChamberWeapon,
  MoveSpareToChestRig,
  ClearMalfunction,
  MaintainWeapon
};

// Suggestions describe one currently legal operation, not a stored command.
// Input handlers must query again before committing after a later revision.
struct WeaponSupplySuggestion {
  WeaponSupplySuggestionKind kind{WeaponSupplySuggestionKind::LoadMagazine};
  AssetInstanceId sourceAssetId{};
  AssetInstanceId targetAssetId{};
  std::optional<StoredAssetLocation> destination;
  ItemOrientation destinationOrientation{ItemOrientation::Degrees0};
};

struct WeaponSupplyReadiness {
  EquipmentSlotKind slot{EquipmentSlotKind::PrimaryWeapon};
  AssetInstanceId weaponAssetId{};
  ItemDefinitionId weaponDefinitionId;
  bool definitionKnown{};
  WeaponAmmoResult fireResult{WeaponAmmoResult::Dry};
  bool canFireNow{};
  std::uint32_t chamberedRounds{};
  std::optional<AssetInstanceId> installedMagazineId;
  std::uint64_t installedMagazineRounds{};
  std::uint64_t carriedLooseRounds{};
  // This is the exact R-key candidate, including a possible empty magazine.
  std::optional<AssetInstanceId> quickReloadMagazineId;
  std::uint64_t quickReloadMagazineRounds{};
  bool quickReloadCanInstall{};
  std::uint32_t quickReloadLoadedMagazineCount{};
  std::uint64_t quickReloadLoadedRounds{};
  // Loaded spare magazines outside the equipped rig's magazine-only pockets.
  // Installed magazines, including those in another weapon, are not spares.
  std::uint32_t otherLoadedSpareMagazineCount{};
  std::uint64_t otherLoadedSpareMagazineRounds{};
  std::vector<WeaponSupplySuggestion> suggestions;
};

struct WeaponSupplyProjection {
  ProfileRevision revision{};
  std::vector<WeaponSupplyReadiness> weapons;
  bool anyWeaponCanFire{};
  // Preparation recommendations, never deploy prerequisites. A healthy
  // player still has medical supplies even when no use applies right now.
  bool hasCarriedMedical{};
  bool hasBodyArmor{};
  bool hasChestRig{};
  bool hasBackpack{};
};

// Counts always describe carried assets. includeStash only broadens the
// sources for preparation suggestions while the UI has a real Stash link.
// Pure, SDL-free and independent of the size of pendingRaid's frozen layout.
// Clients should cache this inventory projection by revision and access mode.
[[nodiscard]] WeaponSupplyProjection
projectWeaponSupply(const ProfileState &profile, const ContentRegistry &content,
                    bool includeStash = false);
