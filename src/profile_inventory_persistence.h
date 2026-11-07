#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"

// Acceptance is a provisional, read-only UI projection. The authoritative
// Profile changes only when a checkpoint generation containing this request
// is durable. It is intentionally distinct from InventoryReceipt::succeeded.
struct InventoryUiSubmission
{
    std::uint64_t requestId{};
    bool accepted{};
    bool pending{};
    InventoryReceipt receipt;
};

struct InventoryUiCompletion
{
    std::uint64_t requestId{};
    bool succeeded{};
    std::string message;
};

struct ProfileInventoryPersistenceState;
