#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class SoundEventId
{
    WeaponPistolFire,
    WeaponRifleFire,
    WeaponFireTailOutdoor,
    WeaponMagazineOut,
    WeaponMagazineIn,
    WeaponChamber,
    WeaponDryFire,
    WeaponMalfunctionClear,
    UiConfirm,
    UiDeny,
    InventoryPickup,
    InventoryEquip,
    InventoryMoveOrPlace,
    MedicalStart,
    MedicalComplete,
    MedicalInterrupt,
    PlayerHurtLight,
    PlayerHurtHeavy,
    InfectedAlert,
    InfectedHit,
    InfectedDeath,
    ImpactEnemy,
    ImpactObstacle,
    ImpactGround,
    AmbienceBaseSafeLow,
    AmbienceRaidUrbanLow,
    Count,
};

[[nodiscard]] constexpr std::size_t soundEventCount() noexcept
{
    return static_cast<std::size_t>(SoundEventId::Count);
}

[[nodiscard]] std::string_view soundEventName(SoundEventId id) noexcept;
[[nodiscard]] std::optional<SoundEventId> parseSoundEventId(
    std::string_view name) noexcept;

struct SoundEventDefinition
{
    SoundEventId id{SoundEventId::WeaponPistolFire};
    std::vector<std::filesystem::path> variants;
    float gain{1.0F};
    std::size_t maxInstances{1U};
    float cooldownSeconds{};
    bool loop{};
};

struct SoundBankDefinition
{
    std::string bankId;
    float masterGain{1.0F};
    std::vector<SoundEventDefinition> events;
};

struct SoundBankParseResult
{
    std::optional<SoundBankDefinition> bank;
    std::string message;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return bank.has_value();
    }
};

[[nodiscard]] SoundBankParseResult parseSoundBankJson(
    std::string_view json);
[[nodiscard]] SoundBankParseResult loadSoundBankDefinition(
    const std::filesystem::path &manifestPath);
