#pragma once
#include "inventory_domain.h"
#include <vector>

// Only directly stored assets in an equipped rig's General pocket qualify.
[[nodiscard]] bool noiseDecoyInChestRig(const ProfileState &, const ContentRegistry &, AssetInstanceId);
[[nodiscard]] std::vector<AssetInstanceId> chestRigNoiseDecoys(const ProfileState &, const ContentRegistry &);
[[nodiscard]] InventoryReceipt consumeNoiseDecoy(ProfileState &, const ContentRegistry &,
    AssetInstanceId, const CommandContext &);
