#pragma once

#include <optional>
#include <string>
#include <vector>

#include "inventory_domain.h"

enum class RecoveryTendency
{
    Low,
    High
};

struct RecoveryTaskQuote
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::string recordId;
    std::uint32_t serviceFee{};
    std::uint32_t durationMinutes{};
    std::uint32_t highTendencyAssets{};
    std::uint32_t lowTendencyAssets{};
};

struct RecoveryTaskProjection
{
    RecoveryTaskId taskId{};
    std::string recordId;
    MapDefinitionId mapDefinitionId;
    std::string mapDisplayName;
    std::uint32_t paidCurrency{};
    std::uint64_t startedWorldMinute{};
    std::uint64_t completionWorldMinute{};
    std::uint64_t remainingWorldMinutes{};
    bool readyForCollection{};
    std::uint32_t recoveredAssetCount{};
    std::uint32_t totalAssetCount{};
};

struct RecoveryTaskReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RecoveryTaskId taskId{};
    std::uint32_t currencyDelta{};
    std::uint32_t recoveredAssetCount{};
    std::uint32_t lostAssetCount{};
};

struct RecoveryTaskAdvanceResult
{
    bool becameReady{};
};

[[nodiscard]] std::optional<RecoveryTaskId> recoveryTaskForAsset(
    const ProfileState &profile,
    AssetInstanceId assetId) noexcept;

[[nodiscard]] RecoveryTendency recoveryTendency(
    const ProfileState &profile,
    const ContentRegistry &content,
    const LostRaidRecord &record,
    AssetInstanceId assetId);

[[nodiscard]] RecoveryTaskQuote queryStartRecoveryTask(
    const ProfileState &profile,
    const ContentRegistry &content,
    const std::string &recordId);

[[nodiscard]] RecoveryTaskReceipt executeStartRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const std::string &recordId,
    const CommandContext &context);

[[nodiscard]] RecoveryTaskReceipt executeCancelRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);

[[nodiscard]] RecoveryTaskReceipt executeCollectRecoveryTask(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);

[[nodiscard]] RecoveryTaskAdvanceResult applyRecoveryTaskThrough(
    ProfileState &profile) noexcept;

[[nodiscard]] std::optional<RecoveryTaskProjection> queryRecoveryTask(
    const ProfileState &profile,
    const ContentRegistry &content);
