#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "inventory_domain.h"

struct BaseGroundAccess
{
    RegionalBaseSiteDefinitionId baseSiteDefinitionId;
    Vec2 playerCenter;
    Vec2 dropPosition;
    bool stashAccessible{};
    float interactionRange{72.0F};
};

struct DropBaseGroundAssetCommand
{
    AssetInstanceId assetId{};
    std::uint32_t quantity{}; // zero means the complete stack
    ItemOrientation orientation{ItemOrientation::Degrees0};
    BaseGroundAccess access;
};

struct PickupBaseGroundAssetCommand
{
    AssetInstanceId assetId{};
    BaseGroundAccess access;
};

using BaseGroundCommand = std::variant<
    DropBaseGroundAssetCommand,
    PickupBaseGroundAssetCommand>;

struct BaseGroundPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    AssetInstanceId affectedAssetId{};
};

struct BaseGroundReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    AssetInstanceId affectedAssetId{};
};

struct BaseGroundAssetProjection
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    Vec2 position;
};

[[nodiscard]] BaseGroundPlan queryBaseGround(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseGroundCommand &command);

[[nodiscard]] BaseGroundReceipt executeBaseGround(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseGroundCommand &command,
    const CommandContext &context);

[[nodiscard]] BaseGroundPlan queryBaseGroundContainerAccess(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access);

[[nodiscard]] InventoryPlan queryBaseGroundContainerInventory(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access,
    const InventoryCommand &command);

[[nodiscard]] InventoryReceipt executeBaseGroundContainerInventory(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access,
    const InventoryCommand &command,
    const CommandContext &context);

[[nodiscard]] std::vector<BaseGroundAssetProjection>
projectBaseGroundAssets(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId);

[[nodiscard]] std::optional<BaseGroundAssetProjection>
nearestBaseGroundAsset(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    Vec2 point,
    float maximumDistance) noexcept;
