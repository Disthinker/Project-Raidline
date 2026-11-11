#pragma once

#include "weapon_action_types.h"
#include "medical_types.h"
#include "throwable_types.h"

struct ProfileState;
struct ProfileValidationResult;
class ContentRegistry;

struct BaseMedicalActionRecovery
{
    std::uint64_t medicalAssetId{};
    MedicalUseEffect effect{MedicalUseEffect::RestoreHealth};
    bool chargeConsumed{};
    int healedAmount{}, maximumHealing{};
    float elapsedSeconds{}, durationSeconds{};
};

// Base physics and already-consumed effects join the same Profile/checkpoint.
// Recovery cancels a medical timer without refunding or repeating its applied
// portion; armed throwables resume their frozen physical state exactly once.
struct BasePlayerEffectsRecovery
{
    WeaponActionScope scope;
    Vec2 worldSize{}, playerPosition{}, facingDirection{1, 0};
    float controlRemainingSeconds{}, impactSlowRemainingSeconds{};
    float medicalTickAccumulatorSeconds{};
    float dailyDamageProtectionSeconds{};
    std::optional<BaseMedicalActionRecovery> medical;
    GrenadeRuntimeCheckpoint grenade;
    NoiseDecoyRuntimeCheckpoint noiseDecoy;
    ThrownIlluminationCheckpoint thrownIllumination;
    // Defense already has these in its coherent checkpoint; these fields are
    // Daily-only and zero during Defense, so there is never a second truth.
    std::uint64_t dailyCommandSequence{}, dailyWeaponFaultSequence{};
    std::uint64_t dailyMedicalRandomSequence{}, dailyWoundRandomSequence{};
    EquipmentSlotKind dailyActiveWeaponSlot{EquipmentSlotKind::PrimaryWeapon};
};

[[nodiscard]] ProfileValidationResult validateBasePlayerEffectsRecovery(
    const BasePlayerEffectsRecovery &, const ProfileState &, const ContentRegistry &);
[[nodiscard]] std::uint64_t basePlayerEffectsFingerprint(
    const BasePlayerEffectsRecovery &) noexcept;
