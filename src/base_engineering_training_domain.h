#pragma once

#include "base_manufacturing_domain.h"

#include <array>
#include <string_view>

inline constexpr std::uint32_t kBaseEngineeringTrainingMinutes = 720U;
inline const std::array kBaseEngineeringTrainingInputs{
    BaseManufacturingInputDefinition{ItemDefinitionId{"item.loot.scrap_parts"}, 2U},
    BaseManufacturingInputDefinition{ItemDefinitionId{"item.loot.copper_wire_spool"}, 1U}};

struct BaseEngineeringTrainingProjection
{
    bool orderPresent{};
    bool paused{};
    BaseServiceJobId jobId{};
    std::uint32_t remainingMinutes{};
    std::string_view pauseReason;
};

struct BaseEngineeringTrainingStartPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::vector<BaseManufacturingInputSelection> inputs;
};

[[nodiscard]] BaseEngineeringTrainingProjection projectBaseEngineeringTraining(
    const ProfileState &profile) noexcept;
[[nodiscard]] BaseEngineeringTrainingStartPlan queryStartBaseEngineeringTraining(
    const ProfileState &profile, const ContentRegistry &content);
[[nodiscard]] BaseManufacturingReturnPlan queryCancelBaseEngineeringTraining(
    const ProfileState &profile, const ContentRegistry &content);
[[nodiscard]] BaseManufacturingReceipt executeStartBaseEngineeringTraining(
    ProfileState &profile, const ContentRegistry &content, const CommandContext &context);
[[nodiscard]] BaseManufacturingReceipt executeCancelBaseEngineeringTraining(
    ProfileState &profile, const ContentRegistry &content, const CommandContext &context);
[[nodiscard]] ProfileValidationResult validateBaseEngineeringTrainingOrder(
    const ProfileState &profile);

// A pending Raid may advance the clock. Account for that time when normal
// settlement closes it, leaving the out-of-raid recovery snapshot untouched.
[[nodiscard]] bool applyBaseEngineeringTrainingThrough(
    ProfileState &profile, const ContentRegistry &content);

// Rebase after a period that must stay suspended even when work resumes.
void pauseBaseEngineeringTrainingThrough(ProfileState &profile) noexcept;
