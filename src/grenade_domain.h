#pragma once

#include "inventory_domain.h"
#include <vector>

// The equipped rig directly owns every selectable stack. A backpack or an
// unequipped rig never becomes a shortcut to combat supplies.
[[nodiscard]] bool grenadeInChestRig(const ProfileState &, const ContentRegistry &, AssetInstanceId);
[[nodiscard]] std::vector<AssetInstanceId> chestRigGrenades(const ProfileState &, const ContentRegistry &);
[[nodiscard]] InventoryReceipt consumeGrenade(ProfileState &, const ContentRegistry &,
    AssetInstanceId, const CommandContext &);
