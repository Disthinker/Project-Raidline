#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "content_registry.h"
#include "grid_inventory.h"

using AssetInstanceId = std::uint64_t;
using ProfileRevision = std::uint64_t;

enum class ProfileContainerKind
{
    Stash,
    AssetCompartment
};

struct ProfileContainerId
{
    ProfileContainerKind kind{ProfileContainerKind::Stash};
    AssetInstanceId ownerAssetId{};
    std::uint32_t compartmentIndex{};

    [[nodiscard]] static ProfileContainerId stash() noexcept;
    [[nodiscard]] static ProfileContainerId compartment(
        AssetInstanceId ownerAssetId,
        std::uint32_t compartmentIndex) noexcept;

    friend auto operator<=>(
        const ProfileContainerId &,
        const ProfileContainerId &) = default;
};

struct StoredAssetLocation
{
    ProfileContainerId container;
    GridPosition origin;

    friend bool operator==(
        const StoredAssetLocation &,
        const StoredAssetLocation &) = default;
};

struct EquippedAssetLocation
{
    EquipmentSlotKind slot{EquipmentSlotKind::PrimaryWeapon};

    friend bool operator==(
        const EquippedAssetLocation &,
        const EquippedAssetLocation &) = default;
};

using AssetLocation = std::variant<
    StoredAssetLocation,
    EquippedAssetLocation>;

struct AssetRecord
{
    AssetInstanceId instanceId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{1};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    std::uint32_t remainingCharges{};
    std::optional<std::string> reliefBatchId;
    AssetLocation location{StoredAssetLocation{
        ProfileContainerId::stash(),
        GridPosition{}}};
};

class AssetRegistry
{
public:
    [[nodiscard]] AssetInstanceId nextAssetId() const noexcept;
    void setNextAssetIdForLoad(AssetInstanceId nextAssetId);

    [[nodiscard]] AssetInstanceId create(
        const ItemDefinition &definition,
        AssetLocation location,
        std::uint32_t quantity = 1,
        std::optional<std::string> reliefBatchId = std::nullopt);

    [[nodiscard]] bool insertLoaded(AssetRecord record);
    [[nodiscard]] bool erase(AssetInstanceId instanceId) noexcept;

    [[nodiscard]] const AssetRecord *find(
        AssetInstanceId instanceId) const noexcept;
    [[nodiscard]] AssetRecord *findMutable(
        AssetInstanceId instanceId) noexcept;

    [[nodiscard]] const std::map<AssetInstanceId, AssetRecord> &
    records() const noexcept;

private:
    AssetInstanceId nextAssetId_{1};
    std::map<AssetInstanceId, AssetRecord> records_;
};

enum class TutorialProgress
{
    FindStorage,
    PrepareLoadout,
    FindRaidGate,
    Complete
};

struct ProfileState
{
    std::string profileId;
    ProfileRevision revision{1};
    std::uint32_t currency{};
    TutorialProgress tutorial{TutorialProgress::FindStorage};
    AssetRegistry assets;
    std::set<std::string> committedTransactions;
};

struct ProfileValidationResult
{
    bool valid{};
    std::string message;
};

[[nodiscard]] ProfileState makeNewAlphaProfile(
    std::string profileId,
    const ContentRegistry &content);

[[nodiscard]] ProfileValidationResult validateProfileState(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] std::uint64_t profileStateFingerprint(
    const ProfileState &profile) noexcept;

[[nodiscard]] InventoryGridSize profileContainerSize(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container);

[[nodiscard]] std::vector<const AssetRecord *> assetsInContainer(
    const ProfileState &profile,
    ProfileContainerId container);

[[nodiscard]] std::optional<AssetInstanceId> equippedAsset(
    const ProfileState &profile,
    EquipmentSlotKind slot) noexcept;

[[nodiscard]] std::optional<AssetInstanceId> profileAssetAtCell(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition cell) noexcept;

[[nodiscard]] std::optional<GridPosition> findFirstProfileFit(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset = std::nullopt) noexcept;
