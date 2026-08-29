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

struct InstallMagazineAndChamberCommand
{
    AssetInstanceId weaponAssetId{};
    AssetInstanceId magazineAssetId{};
};

struct UninstallMagazineCommand
{
    AssetInstanceId weaponAssetId{};
    StoredAssetLocation destination;
    ItemOrientation destinationOrientation{ItemOrientation::Degrees0};
};

struct ChamberWeaponCommand
{
    AssetInstanceId weaponAssetId{};
};

struct FireWeaponCommand
{
    AssetInstanceId weaponAssetId{};
    // Stable rolls supplied by the session. Values are interpreted modulo the
    // applicable bound so tests and save-compatible simulations never depend
    // on a platform RNG implementation.
    std::uint32_t malfunctionRollBasisPoints{9999U};
    std::uint32_t malfunctionTypeRoll{};
};

struct ClearWeaponMalfunctionCommand
{
    AssetInstanceId weaponAssetId{};
};

using WeaponAmmoCommand = std::variant<
    LoadMagazineCommand,
    UnloadMagazineCommand,
    InstallMagazineCommand,
    InstallMagazineAndChamberCommand,
    UninstallMagazineCommand,
    ChamberWeaponCommand,
    FireWeaponCommand,
    ClearWeaponMalfunctionCommand>;

enum class WeaponAmmoResult
{
    Loaded,
    Unloaded,
    Installed,
    InstalledAndChambered,
    Uninstalled,
    Chambered,
    Fired,
    FiredAndMalfunctioned,
    BlockedByMalfunction,
    Broken,
    MalfunctionCleared,
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

struct WeaponAmmoPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    WeaponAmmoResult result{WeaponAmmoResult::Dry};
};

[[nodiscard]] WeaponAmmoPlan queryWeaponAmmo(
    const ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command);

[[nodiscard]] WeaponAmmoReceipt executeWeaponAmmo(
    ProfileState &profile,
    const ContentRegistry &content,
    const WeaponAmmoCommand &command,
    const CommandContext &context);

// Fire is a simulation hot path. These narrow operations inspect and mutate
// only the weapon and its installed magazine, so firing cost is independent
// of the size of the frozen Raid snapshot stored in ProfileState.
[[nodiscard]] WeaponAmmoPlan queryFireWeapon(
    const ProfileState &profile,
    const ContentRegistry &content,
    const FireWeaponCommand &command);

[[nodiscard]] WeaponAmmoReceipt executeFireWeapon(
    ProfileState &profile,
    const ContentRegistry &content,
    const FireWeaponCommand &command,
    const CommandContext &context);

[[nodiscard]] std::size_t magazineRoundCount(
    const ProfileState &profile,
    AssetInstanceId magazineAssetId) noexcept;
