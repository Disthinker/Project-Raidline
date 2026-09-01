#pragma once

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_resident_medical_domain.h"
#include "base_resource_domain.h"

inline constexpr std::uint32_t kMaximumOrdinaryResidents = 10000;
inline constexpr std::uint32_t kMaximumBedCapacity = 10000;
inline constexpr std::uint32_t kRationsPerResidentPerDay = 1;
inline constexpr std::uint32_t kMaximumBaseRestHours = 12;

struct BasePopulationProjection
{
    std::uint32_t ordinaryResidents{};
    std::uint32_t bedCapacity{};
    std::uint32_t bedShortfall{};
    std::uint32_t dailyRationDemand{};
    std::uint32_t injuredResidents{};
    std::uint32_t healthyResidents{};
};

[[nodiscard]] BasePopulationProjection projectBasePopulation(
    const BasePopulationState &state) noexcept;

[[nodiscard]] BaseResourceBundle populationAdjustedDailyDemand(
    const BasePopulationState &state) noexcept;

struct BaseRestCommand
{
    std::uint32_t hours{};
};

struct BaseRestPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t hours{};
    std::uint64_t worldMinutes{};
    WorldClockProjection arrival;
    std::uint64_t dailyCyclesCrossed{};
    BasePopulationProjection population;
    BaseResourceBundle dailyDemand;
};

struct BaseRestReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint64_t worldMinutesApplied{};
    std::uint64_t dailyCyclesResolved{};
    BaseResourceBundle latestShortfall;
    BaseConstructionAdvanceResult construction;
    BaseManufacturingAdvanceResult manufacturing;
    ResidentTreatmentAdvanceResult residentTreatment;
};

[[nodiscard]] BaseRestPlan queryBaseRest(
    const ProfileState &profile,
    const BaseRestCommand &command);

[[nodiscard]] BaseRestReceipt executeBaseRest(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseRestCommand &command,
    const CommandContext &context);
