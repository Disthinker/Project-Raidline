#pragma once

#include <cstdint>
#include <string>
#include "item_definition.h"

struct BaseWishInstanceId
{
    std::uint64_t cycleIndex{};
    BasePriorityDefinitionId definitionId;
    friend bool operator==(const BaseWishInstanceId &, const BaseWishInstanceId &) = default;
};

struct BaseWishExpeditionSnapshot
{
    BaseWishInstanceId wish;
    BaseSupplyCategory category{BaseSupplyCategory::Food};
    std::uint32_t requiredContribution{};
    std::string assessmentVersion;
    friend bool operator==(const BaseWishExpeditionSnapshot &, const BaseWishExpeditionSnapshot &) = default;
};

struct BaseWishReturnSummary
{
    BaseWishExpeditionSnapshot focus;
    std::uint64_t itemCount{};
    std::uint64_t contribution{};
    bool expired{};
    friend bool operator==(const BaseWishReturnSummary &, const BaseWishReturnSummary &) = default;
};
