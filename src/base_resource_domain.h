#pragma once

#include "inventory_domain.h"

inline constexpr std::uint32_t kMaximumBaseResource = 100;
inline constexpr BaseResourceBundle kBaseDailyDemand{8, 6, 5, 4};

struct ContributeBaseAssetCommand
{
    AssetInstanceId assetId{};
};

struct BaseResourcePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseResourceBundle contribution;
};

struct BaseResourceReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseResourceBundle contribution;
};

[[nodiscard]] BaseResourcePlan queryBaseResourceContribution(
    const ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command);

[[nodiscard]] BaseResourceReceipt executeBaseResourceContribution(
    ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command,
    const CommandContext &context);

struct BaseDailyDemandResult
{
    std::uint64_t cyclesResolved{};
    BaseResourceBundle latestShortfall;
};

// Resolves every unprocessed daily boundary through completedWorldDays.
// The implementation is constant-time even when future sleep/travel commands
// advance across many days. Shortages remain non-blocking and never mutate
// assets.
[[nodiscard]] BaseDailyDemandResult applyBaseDailyDemandThrough(
    BaseResourceState &state,
    std::uint64_t completedWorldDays) noexcept;
