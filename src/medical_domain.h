#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "inventory_domain.h"
#include "medical_types.h"
#include "profile_state.h"

struct WoundRollCommand
{
    WoundSource source{WoundSource::None};
    std::uint32_t rollBasisPoints{};
    std::uint32_t initialScreamDelayMs{15000};
};

struct WoundRollResult
{
    bool applied{};
    BleedingSeverity previous{BleedingSeverity::None};
    BleedingSeverity current{BleedingSeverity::None};
};

[[nodiscard]] WoundRollResult applyWoundRoll(
    MedicalStatusState &status,
    const WoundRollCommand &command) noexcept;

struct MedicalAdvanceResult
{
    int healthLost{};
    bool screamed{};
    bool lightBleedingEnded{};
};

[[nodiscard]] MedicalAdvanceResult advanceMedicalStatus(
    MedicalStatusState &status,
    int &currentHealth,
    std::uint32_t elapsedMs,
    std::uint32_t nextScreamDelayMs) noexcept;

enum class MedicalUseEffect
{
    RestoreHealth,
    StopLightBleeding,
    StopAnyBleeding,
    SuppressPain
};

struct MedicalUsePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    MedicalUseEffect effect{MedicalUseEffect::RestoreHealth};
    std::uint32_t durationMs{};
    bool slowMovement{};
};

struct MedicalUseReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    MedicalUseEffect effect{MedicalUseEffect::RestoreHealth};
    int healedAmount{};
    BleedingSeverity bleedingBefore{BleedingSeverity::None};
    BleedingSeverity bleedingAfter{BleedingSeverity::None};
};

enum class MedicalAccess
{
    AnyOwned,
    CarriedOnly
};

[[nodiscard]] MedicalUsePlan queryMedicalUse(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access);

[[nodiscard]] MedicalUseReceipt executeMedicalUse(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access,
    const CommandContext &context);

// Medkit healing is continuous in Raid. The first real point of healing
// consumes one charge atomically; later points are applied by the session
// timeline and survive interruption.
[[nodiscard]] MedicalUseReceipt beginContinuousHealing(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access,
    const CommandContext &context);

[[nodiscard]] std::optional<AssetInstanceId> selectQuickMedicalAsset(
    const ProfileState &profile,
    const ContentRegistry &content,
    MedicalUseEffect preferredEffect) noexcept;
