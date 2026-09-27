#pragma once
#include "inventory_domain.h"

struct WeaponComponentCommand
{
    AssetInstanceId weaponAssetId{};
    WeaponComponentSlot slot{WeaponComponentSlot::Barrel};
    AssetInstanceId componentAssetId{}; // zero restores the default structure
    friend bool operator==(const WeaponComponentCommand &, const WeaponComponentCommand &) = default;
};
struct WeaponComponentReturn
{
    AssetInstanceId assetId{};
    StoredAssetLocation destination;
};
struct WeaponComponentPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    WeaponUseDefinition before;
    WeaponUseDefinition after;
    std::vector<WeaponComponentReturn> returned;
};

[[nodiscard]] ProfileValidationResult validateWeaponComponents(const ProfileState &, const ContentRegistry &);
[[nodiscard]] WeaponUseDefinition effectiveWeaponUse(const ProfileState &, const ContentRegistry &, AssetInstanceId);
[[nodiscard]] WeaponComponentPlan queryWeaponComponentChange(const ProfileState &, const ContentRegistry &, const WeaponComponentCommand &);
[[nodiscard]] InventoryReceipt executeWeaponComponentChange(ProfileState &, const ContentRegistry &, const WeaponComponentCommand &, const CommandContext &);

[[nodiscard]] std::uint64_t installedWeaponComponentRecycleValue(const ProfileState &, const ContentRegistry &, AssetInstanceId);
