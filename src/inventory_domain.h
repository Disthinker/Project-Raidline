#pragma once

#include <string>
#include <variant>

#include "profile_state.h"

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

[[nodiscard]] InventoryPlan queryInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command);

[[nodiscard]] InventoryReceipt executeInventory(
    ProfileState &profile,
    const ContentRegistry &content,
    const InventoryCommand &command,
    const CommandContext &context);
