#pragma once

#include "inventory_domain.h"

[[nodiscard]] bool thrownIlluminationInChestRig(
    const ProfileState &, const ContentRegistry &, AssetInstanceId);
[[nodiscard]] InventoryReceipt consumeThrownIllumination(
    ProfileState &, const ContentRegistry &, AssetInstanceId, const CommandContext &);
