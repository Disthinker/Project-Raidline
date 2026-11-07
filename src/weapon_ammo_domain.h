#pragma once

#include <optional>
#include <string_view>
#include <variant>
#include <vector>

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

enum class EmptyChamberPolicy { ChamberOnTrigger, RequireExplicitChamber };

struct FireWeaponCommand
{
    AssetInstanceId weaponAssetId{};
    // Stable rolls supplied by the session. Values are interpreted modulo the
    // applicable bound so tests and save-compatible simulations never depend
    // on a platform RNG implementation.
    std::uint32_t malfunctionRollBasisPoints{9999U};
    std::uint32_t malfunctionTypeRoll{};
    EmptyChamberPolicy emptyChamberPolicy{EmptyChamberPolicy::ChamberOnTrigger};
};

struct SetWeaponFireModeCommand
{
    AssetInstanceId weaponAssetId{};
    WeaponFireMode fireMode{WeaponFireMode::SemiAutomatic};
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
    ClearWeaponMalfunctionCommand,
    SetWeaponFireModeCommand>;

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
    Dry,
    NeedsChamber,
    FireModeChanged
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

[[nodiscard]] std::optional<WeaponFireMode> queryWeaponFireMode(
    const ProfileState &, const ContentRegistry &, AssetInstanceId) noexcept;

struct RaidReloadBinding
{
    std::string raidId;
    RaidSpaceDefinitionId spaceId;
    AssetInstanceId weaponAssetId{};
    std::string actionId;
    AssetInstanceId newMagazineAssetId{};
    std::optional<AssetInstanceId> oldMagazineAssetId;
    std::optional<WeaponActionScope> activity;
};

struct RaidMagazinePlacement
{
    StoredAssetLocation location;
    ItemOrientation orientation{ItemOrientation::Degrees0};
};

// Borrowed current-world facts, supplied by the session at the actual commit.
struct RaidReloadWorldAccess
{
    RaidSpaceDefinitionId spaceId;
    Vec2 playerCenter{};
    const RaidVisibilityProjection *observation{};
    const RaidObservationKey *currentObservation{};
    std::optional<WeaponActionScope> activity;
    Vec2 worldSize{};
    bool stashAccessible{};
};

struct RaidReloadRequest
{
    RaidReloadBinding binding;
    RaidReloadKind kind{RaidReloadKind::Normal};
    RaidReloadSource source{RaidReloadSource::AutomaticChestRig};
    bool stashAccessible{};
};

struct RaidReloadPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RaidReloadBinding binding;
    RaidReloadKind kind{RaidReloadKind::Normal};
    RaidReloadSource source{RaidReloadSource::AutomaticChestRig};
    RaidMagazinePlacement newSource;
    std::optional<RaidMagazinePlacement> oldReturn;
};

struct RaidReloadMagazineChoice
{
    AssetInstanceId magazineAssetId{};
    std::uint32_t roundCount{};
    bool eligible{};
    DomainErrorCode error{DomainErrorCode::None};
};

struct RaidReloadStageCommand
{
    RaidReloadBinding binding;
    RaidReloadStage stage{RaidReloadStage::TakeNew};
    RaidReloadKind kind{RaidReloadKind::Normal};
    RaidReloadSource source{RaidReloadSource::AutomaticChestRig};
    RaidMagazinePlacement newSource;
    std::optional<RaidMagazinePlacement> oldReturn;
    std::optional<RaidDroppedMagazineLocation> drop;
    std::optional<AssetLocation> worldDrop;
};

struct RaidReloadReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::optional<RaidReloadStage> committedStage;
    bool magazineRemoved{}, magazineInserted{}, chamberedRound{};
    std::vector<AssetInstanceId> droppedMagazineAssetIds;
};

struct RaidReloadCancelCommand
{
    RaidReloadBinding binding;
    RaidMagazinePlacement newSource;
    std::optional<RaidDroppedMagazineLocation> newFallbackDrop;
    std::optional<RaidDroppedMagazineLocation> oldFallbackDrop;
    std::optional<AssetLocation> newWorldFallbackDrop;
    std::optional<AssetLocation> oldWorldFallbackDrop;
};

[[nodiscard]] RaidReloadPlan queryRaidReload(
    const ProfileState &, const ContentRegistry &, const RaidReloadRequest &);
[[nodiscard]] std::vector<RaidReloadMagazineChoice> queryRaidReloadMagazines(
    const ProfileState &, const ContentRegistry &, AssetInstanceId);
[[nodiscard]] InventoryPlan queryRaidReloadStage(
    const ProfileState &, const ContentRegistry &, const RaidReloadStageCommand &,
    const RaidReloadWorldAccess &);
[[nodiscard]] RaidReloadReceipt executeRaidReloadStage(
    ProfileState &, const ContentRegistry &, const RaidReloadStageCommand &,
    const CommandContext &, const RaidReloadWorldAccess &);
[[nodiscard]] InventoryPlan queryRaidReloadCancel(
    const ProfileState &, const ContentRegistry &, const RaidReloadCancelCommand &,
    const RaidReloadWorldAccess &);
[[nodiscard]] RaidReloadReceipt executeRaidReloadCancel(
    ProfileState &, const ContentRegistry &, const RaidReloadCancelCommand &,
    const CommandContext &, const RaidReloadWorldAccess &);
[[nodiscard]] InventoryReceipt pickupDroppedMagazine(
    ProfileState &, const ContentRegistry &, AssetInstanceId,
    const CommandContext &, const RaidReloadWorldAccess &);
[[nodiscard]] ProfileValidationResult resolveRetainedRaidMagazinesInCandidate(
    ProfileState &, const ContentRegistry &, AssetInstanceId,
    bool raidCarriedDestination);
[[nodiscard]] ProfileValidationResult retainRaidReloadMagazinesForLossInCandidate(
    ProfileState &, const ContentRegistry &, std::string_view raidId);
[[nodiscard]] ProfileValidationResult validateRaidMagazineLocations(
    const ProfileState &, const ContentRegistry &);
[[nodiscard]] ProfileValidationResult validateBaseMagazineRecovery(
    const ProfileState &, const ContentRegistry &);
[[nodiscard]] RaidReloadReceipt recoverBaseReloadInCandidate(
    ProfileState &, const ContentRegistry &, const RaidReloadWorldAccess &);
