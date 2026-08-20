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

    friend bool operator==(
        const MedicalStatusState &,
        const MedicalStatusState &) = default;
};

[[nodiscard]] constexpr bool hasPain(
    const MedicalStatusState &status) noexcept
{
    return status.bleeding != BleedingSeverity::None;
}

[[nodiscard]] constexpr bool painIsSuppressed(
    const MedicalStatusState &status) noexcept
{
    return hasPain(status) && status.painkillerRemainingMs > 0;
}

