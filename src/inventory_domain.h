#pragma once

#include <string>
#include <variant>

#include "profile_state.h"
#include "raid_visibility.h"

struct CommandContext
{
    ProfileRevision expectedRevision{};
    std::string transactionId;
};

enum class DomainErrorCode
{
    None,
    StaleRevision,
    InvalidTransaction,
    MissingAsset,
    InvalidQuantity,
    IllegalDestination,
    Capacity,
    IncompatibleEquipment,
    InvalidProfile,
    RevisionOverflow
};

struct InventoryMoveCommand
{
    AssetInstanceId instanceId{};
    std::uint32_t quantity{}; // zero means the complete current stack
    StoredAssetLocation destination;
    ItemOrientation destinationOrientation{ItemOrientation::Degrees0};
};

struct InventoryEquipCommand
{
    AssetInstanceId instanceId{};
    EquipmentSlotKind slot{EquipmentSlotKind::PrimaryWeapon};
};

using InventoryCommand = std::variant<
    InventoryMoveCommand,
    InventoryEquipCommand>;

struct InventoryReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
};

struct InventoryPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
};

[[nodiscard]] ProfileValidationResult validateInventoryAssetGraph(
    const AssetRegistry &assets, const ContentRegistry &content,
    std::uint32_t stashLayoutVersion = 1, std::uint32_t chestRigLayoutVersion = 1);

struct RaidSceneContainerAccess
{
    AssetInstanceId containerAssetId{};
    RaidSpaceDefinitionId spaceId;
    Vec2 playerCenter{};
    std::optional<RaidVisibilityProjection> observation;
    std::optional<RaidObservationKey> currentObservation;
};

[[nodiscard]] std::optional<AssetInstanceId> raidSceneContainerRoot(
    const ProfileState &profile, AssetInstanceId assetId);
[[nodiscard]] bool raidSceneContainerAccessible(
    const ProfileState &profile, const RaidSceneContainerAccess &access);

[[nodiscard]] InventoryPlan queryInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command,
    const RaidSceneContainerAccess *sceneAccess = nullptr);

[[nodiscard]] InventoryReceipt executeInventory(
    ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command,
    const CommandContext &context,
    const RaidSceneContainerAccess *sceneAccess = nullptr);
