#pragma once

#include <optional>
#include <variant>

#include "weapon_ammo_domain.h"
#include "medical_domain.h"

struct ReloadRaidAction
{
    AssetInstanceId weaponAssetId{};
    AssetInstanceId magazineAssetId{};
    float elapsedSeconds{};
    float durationSeconds{2.0F};
};

struct LoadMagazineRaidAction
{
    AssetInstanceId magazineAssetId{};
    AssetInstanceId ammunitionAssetId{};
    std::uint32_t quantity{};
    float elapsedSeconds{};
    float durationSeconds{0.5F};
};

struct HealRaidAction
{
    AssetInstanceId medkitAssetId{};
    float elapsedSeconds{};
    float durationSeconds{5.0F};
};

struct MedicalRaidAction
{
    AssetInstanceId medicalAssetId{};
    MedicalUseEffect effect{MedicalUseEffect::RestoreHealth};
    bool slowMovement{};
    bool chargeConsumed{};
    int healedAmount{};
    int maximumHealing{};
    float elapsedSeconds{};
    float durationSeconds{};
};

struct UnloadMagazineRaidAction
{
    AssetInstanceId magazineAssetId{};
    ProfileContainerId destination;
    float elapsedSeconds{};
    float durationSeconds{3.0F};
};

struct ExtractRaidAction
{
    float elapsedSeconds{};
    float durationSeconds{3.0F};
};

using RaidAction = std::variant<
    ReloadRaidAction,
    LoadMagazineRaidAction,
    HealRaidAction,
    MedicalRaidAction,
    UnloadMagazineRaidAction,
    ExtractRaidAction>;

enum class RaidActionAdvance
{
    Idle,
    Running,
    Interrupted,
    Completed
};

class RaidActionState
{
public:
    [[nodiscard]] bool start(RaidAction action) noexcept;
    [[nodiscard]] RaidActionAdvance update(
        float deltaTime,
        bool interrupted) noexcept;
    void cancel() noexcept;

    [[nodiscard]] const std::optional<RaidAction> &active() const noexcept;
    [[nodiscard]] RaidAction *activeMutable() noexcept;
    [[nodiscard]] std::optional<RaidAction> takeCompleted() noexcept;
    [[nodiscard]] float progress() const noexcept;

private:
    std::optional<RaidAction> active_;
    std::optional<RaidAction> completed_;
};

struct HealReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    int healedAmount{};
};

enum class HealAccess
{
    AnyOwned,
    CarriedOnly
};

[[nodiscard]] std::optional<AssetInstanceId> selectRaidReloadMagazine(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId weaponAssetId) noexcept;

[[nodiscard]] std::optional<AssetInstanceId> selectQuickMedkit(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;

[[nodiscard]] std::optional<ProfileContainerId>
selectRaidMagazineUnloadDestination(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId magazineAssetId) noexcept;

[[nodiscard]] HealReceipt executeHeal(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medkitAssetId,
    HealAccess access,
    const CommandContext &context);
