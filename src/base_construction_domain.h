#pragma once

#include "inventory_domain.h"

struct BaseConstructionProjection {
  std::uint32_t materialUnits{};
  std::uint32_t maximumMaterialUnits{};
  std::uint32_t dormitoryLevel{};
  std::uint32_t bedCapacity{};
  std::uint32_t totalWorkers{};
  std::uint32_t committedWorkers{};
  std::uint32_t availableWorkers{};
  std::optional<BaseConstructionProjectDefinitionId> activeProjectId;
  std::uint64_t remainingMinutes{};
};

[[nodiscard]] BaseConstructionProjection
projectBaseConstruction(const ProfileState &profile,
                        const ContentRegistry &content) noexcept;

struct ContributeConstructionMaterialCommand {
  AssetInstanceId assetId{};
};

struct ConstructionMaterialPlan {
  bool canCommit{};
  DomainErrorCode error{DomainErrorCode::None};
  std::string message;
  ProfileRevision revision{};
  std::uint32_t materialUnits{};
};

struct ConstructionMaterialReceipt {
  bool succeeded{};
  bool alreadyCommitted{};
  DomainErrorCode error{DomainErrorCode::None};
  std::string message;
  ProfileRevision revision{};
  std::uint32_t materialUnits{};
};

[[nodiscard]] ConstructionMaterialPlan queryConstructionMaterialContribution(
    const ProfileState &profile, const ContentRegistry &content,
    const ContributeConstructionMaterialCommand &command);

[[nodiscard]] ConstructionMaterialReceipt
executeConstructionMaterialContribution(
    ProfileState &profile, const ContentRegistry &content,
    const ContributeConstructionMaterialCommand &command,
    const CommandContext &context);

struct StartBaseConstructionCommand {
  BaseConstructionProjectDefinitionId definitionId;
};

struct CancelBaseConstructionCommand {
  BaseConstructionProjectDefinitionId definitionId;
};

struct BaseConstructionPlan {
  bool canCommit{};
  DomainErrorCode error{DomainErrorCode::None};
  std::string message;
  ProfileRevision revision{};
  std::uint32_t materialCost{};
  std::uint32_t workerCount{};
  std::uint32_t durationMinutes{};
  std::uint32_t bedCapacityAfter{};
};

struct BaseConstructionReceipt {
  bool succeeded{};
  bool alreadyCommitted{};
  DomainErrorCode error{DomainErrorCode::None};
  std::string message;
  ProfileRevision revision{};
  std::uint32_t materialUnits{};
  std::uint32_t dormitoryLevel{};
  std::uint32_t bedCapacity{};
};

[[nodiscard]] BaseConstructionPlan
queryStartBaseConstruction(const ProfileState &profile,
                           const ContentRegistry &content,
                           const StartBaseConstructionCommand &command);

[[nodiscard]] BaseConstructionReceipt executeStartBaseConstruction(
    ProfileState &profile, const ContentRegistry &content,
    const StartBaseConstructionCommand &command, const CommandContext &context);

[[nodiscard]] BaseConstructionPlan
queryCancelBaseConstruction(const ProfileState &profile,
                            const ContentRegistry &content,
                            const CancelBaseConstructionCommand &command);

[[nodiscard]] BaseConstructionReceipt
executeCancelBaseConstruction(ProfileState &profile,
                              const ContentRegistry &content,
                              const CancelBaseConstructionCommand &command,
                              const CommandContext &context);

struct BaseConstructionAdvanceResult {
  bool completed{};
  BaseConstructionProjectDefinitionId definitionId;
  std::uint32_t bedCapacityBefore{};
  std::uint32_t bedCapacityAfter{};
  std::uint32_t releasedWorkers{};
};

// Applies a due project to an already-owned candidate Profile. The enclosing
// time command owns revision advancement and persistence.
[[nodiscard]] BaseConstructionAdvanceResult
applyBaseConstructionThrough(ProfileState &profile,
                             const ContentRegistry &content);
