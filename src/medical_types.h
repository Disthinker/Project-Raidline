#pragma once

#include <cstdint>

enum class BleedingSeverity
{
    None,
    Light,
    Heavy
};

enum class WoundSource
{
    None,
    Scratch,
    Bite
};

struct MedicalStatusState
{
    BleedingSeverity bleeding{BleedingSeverity::None};
    std::uint32_t lightBleedingRemainingMs{};
    std::uint32_t bleedingDamageRemainingMs{};
    std::uint32_t painkillerRemainingMs{};
    std::uint32_t painScreamRemainingMs{};

    friend bool operator==(const MedicalStatusState &, const MedicalStatusState &) = default;
};

[[nodiscard]] inline bool validMedicalStatus(const MedicalStatusState &status) noexcept
{
    if (status.painkillerRemainingMs > 180000)
    {
        return false;
    }
    switch (status.bleeding)
    {
    case BleedingSeverity::None:
        return status.lightBleedingRemainingMs == 0 && status.bleedingDamageRemainingMs == 0 &&
               status.painScreamRemainingMs == 0;
    case BleedingSeverity::Light:
        return status.lightBleedingRemainingMs > 0 && status.lightBleedingRemainingMs <= 40000 &&
               status.bleedingDamageRemainingMs > 0 && status.bleedingDamageRemainingMs <= 1000 &&
               status.painScreamRemainingMs > 0 && status.painScreamRemainingMs <= 25000;
    case BleedingSeverity::Heavy:
        return status.lightBleedingRemainingMs == 0 && status.bleedingDamageRemainingMs > 0 &&
               status.bleedingDamageRemainingMs <= 500 && status.painScreamRemainingMs > 0 &&
               status.painScreamRemainingMs <= 25000;
    }
    return false;
}

[[nodiscard]] constexpr bool hasPain(const MedicalStatusState &status) noexcept
{
    return status.bleeding != BleedingSeverity::None;
}

[[nodiscard]] constexpr bool painIsSuppressed(const MedicalStatusState &status) noexcept
{
    return hasPain(status) && status.painkillerRemainingMs > 0;
}
