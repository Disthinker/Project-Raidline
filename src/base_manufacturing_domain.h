#pragma once

#include "inventory_domain.h"

struct BaseManufacturingProjection
{
    bool orderPresent{};
    bool outputReady{};
    BaseManufacturingRecipeDefinitionId recipeDefinitionId;
    BaseServiceJobId jobId{};
    std::uint32_t committedWorkers{};
    std::uint64_t remainingMinutes{};
    std::optional<AssetInstanceId> outputAssetId;
};

struct StartBaseManufacturingCommand
{
    BaseManufacturingRecipeDefinitionId recipeDefinitionId;
};

struct BaseManufacturingInputSelection
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
};

struct BaseManufacturingStartPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t workerCount{};
    BaseResidentProfession workerProfession{BaseResidentProfession::General};
    std::uint32_t durationMinutes{};
    std::vector<BaseManufacturingInputSelection> inputs;
};

struct BaseManufacturingReturnSelection
{
    AssetInstanceId assetId{};
    StoredAssetLocation destination;
};

struct BaseManufacturingReturnPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::vector<BaseManufacturingReturnSelection> returns;
};

struct BaseManufacturingReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseServiceJobId jobId{};
    std::optional<AssetInstanceId> outputAssetId;
};

struct BaseManufacturingAdvanceResult
{
    bool completed{};
    bool outputBlocked{};
    BaseServiceJobId jobId{};
    std::optional<AssetInstanceId> outputAssetId;
};

[[nodiscard]] BaseManufacturingProjection projectBaseManufacturing(
    const ProfileState &profile) noexcept;

[[nodiscard]] BaseManufacturingStartPlan queryStartBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartBaseManufacturingCommand &command);

[[nodiscard]] BaseManufacturingReceipt executeStartBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartBaseManufacturingCommand &command,
    const CommandContext &context);

[[nodiscard]] BaseManufacturingReturnPlan queryCancelBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] BaseManufacturingReceipt executeCancelBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);

[[nodiscard]] BaseManufacturingReturnPlan queryCollectBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] BaseManufacturingReceipt executeCollectBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);

// Applies a due order to an already-owned candidate Profile. While a Raid is
// pending, materialization is deliberately deferred until settlement closes
// the activity so abnormal rollback never needs to clone the asset registry.
[[nodiscard]] BaseManufacturingAdvanceResult applyBaseManufacturingThrough(
    ProfileState &profile,
    const ContentRegistry &content);
