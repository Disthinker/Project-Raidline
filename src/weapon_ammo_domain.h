#pragma once

#include <optional>
#include <variant>

#include "inventory_domain.h"

struct LoadMagazineCommand
{
    AssetInstanceId magazineAssetId{};
    AssetInstanceId ammunitionAssetId{};
    std::uint32_t quantity{}; // zero fills available capacity
};

struct UnloadMagazineCommand
{
    AssetInstanceId magazineAssetId{};
    ProfileContainerId destination{ProfileContainerId::stash()};
};

struct InstallMagazineCommand
{
    AssetInstanceId weaponAssetId{};
    AssetInstanceId magazineAssetId{};
};

struct UninstallMagazineCommand
{
    AssetInstanceId weaponAssetId{};
    ProfileContainerId destination{ProfileContainerId::stash()};
};

struct FireWeaponCommand
{
    AssetInstanceId weaponAssetId{};
};

using WeaponAmmoCommand = std::variant<
    LoadMagazineCommand,
    UnloadMagazineCommand,
    InstallMagazineCommand,
    UninstallMagazineCommand,
    FireWeaponCommand>;

enum class WeaponAmmoResult
{
    Loaded,
    Unloaded,
    Installed,
    Uninstalled,
    Chambered,
    Fired,
    Dry
};

struct WeaponAmmoReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    WeaponAmmoResult result{WeaponAmmoResult::Dry};
    std::optional<ItemDefinitionId> firedAmmunitionDefinitionId;
};

[[nodiscard]] WeaponAmmoReceipt executeWeaponAmmo(
    ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command,
    const CommandContext &context);

[[nodiscard]] std::size_t magazineRoundCount(
    const ProfileState &profile,
    AssetInstanceId magazineAssetId) noexcept;

