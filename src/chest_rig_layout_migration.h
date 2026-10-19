#pragma once

#include "profile_state.h"

// Version zero is only for reading/validating historical save DTOs. Runtime
// profiles use one continuous general compartment per published chest rig.
[[nodiscard]] std::vector<ContainerCompartmentDefinition>
legacyChestRigCompartments(const ItemDefinition &definition);
[[nodiscard]] ContainerCompartmentDefinition profileCompartmentDefinition(
    const ProfileState &, const ItemDefinition &, std::uint32_t index);
[[nodiscard]] ProfileValidationResult migrateLegacyChestRigLayout(
    ProfileState &, const ContentRegistry &);
// Explicit legacy-schema serialization either encodes a lossless old layout
// or rejects it. A new item spanning old pocket boundaries cannot downgrade.
[[nodiscard]] ProfileState legacyChestRigSnapshot(const ProfileState &);
