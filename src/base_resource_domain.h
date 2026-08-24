#pragma once

#include "inventory_domain.h"

inline constexpr std::uint32_t kMaximumBaseResource = 100;
inline constexpr BaseResourceBundle kBaseActivityDemand{8, 6, 5, 4};

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

// Settlement-only operation: every resolved Raid consumes the same small
// Alpha activity demand. Shortages are recorded for presentation but do not
// block play or mutate assets.
void applyBaseActivityDemand(ProfileState &profile) noexcept;
