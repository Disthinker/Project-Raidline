#include "base_construction_domain.h"

#include <algorithm>
#include <limits>

namespace {
ConstructionMaterialPlan materialFailure(DomainErrorCode error,
                                         std::string message,
                                         ProfileRevision revision) {
  return {false, error, std::move(message), revision, 0};
}

BaseConstructionPlan constructionFailure(DomainErrorCode error,
                                         std::string message,
                                         ProfileRevision revision) {
  return {false, error, std::move(message), revision};
}

bool hasChildren(const ProfileState &profile, AssetInstanceId parent) noexcept {
  return std::any_of(
      profile.assets.records().begin(), profile.assets.records().end(),
      [parent](const auto &entry) {
        const auto *stored =
            std::get_if<StoredAssetLocation>(&entry.second.location);
        return stored != nullptr &&
               stored->container.kind ==
                   ProfileContainerKind::AssetCompartment &&
               stored->container.ownerAssetId == parent;
      });
}

BaseConstructionReceipt receiptFailure(const BaseConstructionPlan &plan,
                                       const ProfileState &profile) {
  return {false,
          false,
          plan.error,
          plan.message,
          profile.revision,
          profile.baseConstruction.materialUnits,
          profile.baseConstruction.dormitoryLevel,
          profile.basePopulation.bedCapacity};
}
} // namespace

BaseConstructionProjection
projectBaseConstruction(const ProfileState &profile,
                        const ContentRegistry &content) noexcept {
  BaseConstructionProjection projection{
      profile.baseConstruction.materialUnits,
      content.maximumBaseConstructionMaterials(),
      profile.baseConstruction.dormitoryLevel,
      profile.basePopulation.bedCapacity,
      profile.basePopulation.ordinaryResidents};
  if (profile.baseConstruction.activeProject.has_value()) {
    const ActiveBaseConstructionProject &active =
        *profile.baseConstruction.activeProject;
    projection.committedWorkers = active.committedWorkers;
    projection.activeProjectId = active.definitionId;
    if (active.completionWorldMinute > profile.worldClock.elapsedWorldMinutes) {
      projection.remainingMinutes =
          active.completionWorldMinute - profile.worldClock.elapsedWorldMinutes;
    }
  }
  projection.availableWorkers =
      projection.totalWorkers > projection.committedWorkers
          ? projection.totalWorkers - projection.committedWorkers
          : 0U;
  return projection;
}

ConstructionMaterialPlan queryConstructionMaterialContribution(
    const ProfileState &profile, const ContentRegistry &content,
    const ContributeConstructionMaterialCommand &command) {
  const AssetRecord *asset = profile.assets.find(command.assetId);
  if (asset == nullptr) {
    return materialFailure(DomainErrorCode::MissingAsset,
                           "allocation item does not exist", profile.revision);
  }
  const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
  if (stored == nullptr ||
      stored->container != ProfileContainerId::baseIntake()) {
    return materialFailure(
        DomainErrorCode::IllegalDestination,
        "only pending allocation items can become construction material",
        profile.revision);
  }
  if (hasChildren(profile, asset->instanceId)) {
    return materialFailure(
        DomainErrorCode::IllegalDestination,
        "a non-empty container cannot become construction material",
        profile.revision);
  }
  const ItemDefinition &definition = content.item(asset->definitionId);
  const std::uint64_t total =
      static_cast<std::uint64_t>(definition.baseConstructionMaterialValue) *
      asset->quantity;
  if (total == 0U || total > std::numeric_limits<std::uint32_t>::max()) {
    return materialFailure(DomainErrorCode::IllegalDestination,
                           "item has no Base construction material value",
                           profile.revision);
  }
  const std::uint32_t amount = static_cast<std::uint32_t>(total);
  const std::uint32_t maximum = content.maximumBaseConstructionMaterials();
  if (profile.baseConstruction.materialUnits > maximum ||
      amount > maximum - profile.baseConstruction.materialUnits) {
    return materialFailure(DomainErrorCode::Capacity,
                           "Base construction material storage is full",
                           profile.revision);
  }
  return {true, DomainErrorCode::None, {}, profile.revision, amount};
}

ConstructionMaterialReceipt executeConstructionMaterialContribution(
    ProfileState &profile, const ContentRegistry &content,
    const ContributeConstructionMaterialCommand &command,
    const CommandContext &context) {
  if (context.transactionId.empty()) {
    return {false, false, DomainErrorCode::InvalidTransaction,
            "transaction ID is empty", profile.revision};
  }
  if (profile.committedTransactions.contains(context.transactionId)) {
    return {true, true, DomainErrorCode::None, {}, profile.revision, 0U};
  }
  if (context.expectedRevision != profile.revision) {
    return {false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision};
  }
  if (profile.pendingRaid.has_value()) {
    return {false, false, DomainErrorCode::IllegalDestination,
            "construction material can only be processed in Base",
            profile.revision};
  }
  if (profile.revision == std::numeric_limits<ProfileRevision>::max()) {
    return {false, false, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance", profile.revision};
  }
  const ConstructionMaterialPlan plan =
      queryConstructionMaterialContribution(profile, content, command);
  if (!plan.canCommit) {
    return {false, false, plan.error, plan.message, profile.revision};
  }

  ProfileState candidate = profile;
  if (!candidate.assets.erase(command.assetId)) {
    return {false, false, DomainErrorCode::MissingAsset,
            "allocation item no longer exists", profile.revision};
  }
  candidate.baseConstruction.materialUnits += plan.materialUnits;
  candidate.committedTransactions.insert(context.transactionId);
  ++candidate.revision;
  const ProfileValidationResult validation =
      validateProfileState(candidate, content);
  if (!validation.valid) {
    return {false, false, DomainErrorCode::InvalidProfile, validation.message,
            profile.revision};
  }
  profile = std::move(candidate);
  return {true,
          false,
          DomainErrorCode::None,
          {},
          profile.revision,
          plan.materialUnits};
}

BaseConstructionPlan
queryStartBaseConstruction(const ProfileState &profile,
                           const ContentRegistry &content,
                           const StartBaseConstructionCommand &command) {
  const BaseConstructionProjectDefinition *definition{};
  try {
    definition = &content.baseConstructionProject(command.definitionId);
  } catch (...) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "construction project does not exist",
                               profile.revision);
  }
  if (profile.pendingRaid.has_value()) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "construction can only start in Base",
                               profile.revision);
  }
  if (profile.baseConstruction.activeProject.has_value()) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "another Base construction project is active",
                               profile.revision);
  }
  if (profile.baseConstruction.dormitoryLevel !=
      definition->requiredDormitoryLevel) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "dormitory level does not match this project",
                               profile.revision);
  }
  if (profile.baseConstruction.materialUnits < definition->materialCost) {
    return constructionFailure(DomainErrorCode::Capacity,
                               "insufficient Base construction material",
                               profile.revision);
  }
  if (profile.basePopulation.ordinaryResidents < definition->workerCount) {
    return constructionFailure(DomainErrorCode::Capacity,
                               "insufficient available Base workers",
                               profile.revision);
  }
  if (profile.worldClock.elapsedWorldMinutes >
      std::numeric_limits<std::uint64_t>::max() - definition->durationMinutes) {
    return constructionFailure(DomainErrorCode::RevisionOverflow,
                               "construction completion time would overflow",
                               profile.revision);
  }
  return {true,
          DomainErrorCode::None,
          {},
          profile.revision,
          definition->materialCost,
          definition->workerCount,
          definition->durationMinutes,
          definition->bedCapacityAfter};
}

BaseConstructionReceipt
executeStartBaseConstruction(ProfileState &profile,
                             const ContentRegistry &content,
                             const StartBaseConstructionCommand &command,
                             const CommandContext &context) {
  if (context.transactionId.empty()) {
    return {false, false, DomainErrorCode::InvalidTransaction,
            "transaction ID is empty", profile.revision};
  }
  if (profile.committedTransactions.contains(context.transactionId)) {
    return {true,
            true,
            DomainErrorCode::None,
            {},
            profile.revision,
            profile.baseConstruction.materialUnits,
            profile.baseConstruction.dormitoryLevel,
            profile.basePopulation.bedCapacity};
  }
  if (context.expectedRevision != profile.revision) {
    return {false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision};
  }
  if (profile.revision == std::numeric_limits<ProfileRevision>::max()) {
    return {false, false, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance", profile.revision};
  }
  const BaseConstructionPlan plan =
      queryStartBaseConstruction(profile, content, command);
  if (!plan.canCommit) {
    return receiptFailure(plan, profile);
  }
  ProfileState candidate = profile;
  candidate.baseConstruction.materialUnits -= plan.materialCost;
  candidate.baseConstruction.activeProject = ActiveBaseConstructionProject{
      command.definitionId, plan.materialCost, plan.workerCount,
      candidate.worldClock.elapsedWorldMinutes,
      candidate.worldClock.elapsedWorldMinutes + plan.durationMinutes};
  candidate.committedTransactions.insert(context.transactionId);
  ++candidate.revision;
  const ProfileValidationResult validation =
      validateProfileState(candidate, content);
  if (!validation.valid) {
    return {false, false, DomainErrorCode::InvalidProfile, validation.message,
            profile.revision};
  }
  profile = std::move(candidate);
  return {true,
          false,
          DomainErrorCode::None,
          {},
          profile.revision,
          profile.baseConstruction.materialUnits,
          profile.baseConstruction.dormitoryLevel,
          profile.basePopulation.bedCapacity};
}

BaseConstructionPlan
queryCancelBaseConstruction(const ProfileState &profile,
                            const ContentRegistry &content,
                            const CancelBaseConstructionCommand &command) {
  if (!profile.baseConstruction.activeProject.has_value() ||
      profile.baseConstruction.activeProject->definitionId !=
          command.definitionId) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "construction project is not active",
                               profile.revision);
  }
  const ActiveBaseConstructionProject &active =
      *profile.baseConstruction.activeProject;
  const std::uint32_t maximum = content.maximumBaseConstructionMaterials();
  if (active.lockedMaterialUnits > maximum ||
      profile.baseConstruction.materialUnits >
          maximum - active.lockedMaterialUnits) {
    return constructionFailure(DomainErrorCode::Capacity,
                               "construction material refund has no capacity",
                               profile.revision);
  }
  return {true,
          DomainErrorCode::None,
          {},
          profile.revision,
          active.lockedMaterialUnits,
          active.committedWorkers};
}

BaseConstructionReceipt
executeCancelBaseConstruction(ProfileState &profile,
                              const ContentRegistry &content,
                              const CancelBaseConstructionCommand &command,
                              const CommandContext &context) {
  if (context.transactionId.empty()) {
    return {false, false, DomainErrorCode::InvalidTransaction,
            "transaction ID is empty", profile.revision};
  }
  if (profile.committedTransactions.contains(context.transactionId)) {
    return {true,
            true,
            DomainErrorCode::None,
            {},
            profile.revision,
            profile.baseConstruction.materialUnits,
            profile.baseConstruction.dormitoryLevel,
            profile.basePopulation.bedCapacity};
  }
  if (context.expectedRevision != profile.revision) {
    return {false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision};
  }
  if (profile.pendingRaid.has_value()) {
    return {false, false, DomainErrorCode::IllegalDestination,
            "construction can only be cancelled in Base", profile.revision};
  }
  if (profile.revision == std::numeric_limits<ProfileRevision>::max()) {
    return {false, false, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance", profile.revision};
  }
  const BaseConstructionPlan plan =
      queryCancelBaseConstruction(profile, content, command);
  if (!plan.canCommit) {
    return receiptFailure(plan, profile);
  }
  ProfileState candidate = profile;
  candidate.baseConstruction.materialUnits += plan.materialCost;
  candidate.baseConstruction.activeProject.reset();
  candidate.committedTransactions.insert(context.transactionId);
  ++candidate.revision;
  const ProfileValidationResult validation =
      validateProfileState(candidate, content);
  if (!validation.valid) {
    return {false, false, DomainErrorCode::InvalidProfile, validation.message,
            profile.revision};
  }
  profile = std::move(candidate);
  return {true,
          false,
          DomainErrorCode::None,
          {},
          profile.revision,
          profile.baseConstruction.materialUnits,
          profile.baseConstruction.dormitoryLevel,
          profile.basePopulation.bedCapacity};
}

BaseConstructionAdvanceResult
applyBaseConstructionThrough(ProfileState &profile,
                             const ContentRegistry &content) {
  if (!profile.baseConstruction.activeProject.has_value() ||
      profile.worldClock.elapsedWorldMinutes <
          profile.baseConstruction.activeProject->completionWorldMinute) {
    return {};
  }
  const ActiveBaseConstructionProject active =
      *profile.baseConstruction.activeProject;
  const BaseConstructionProjectDefinition &definition =
      content.baseConstructionProject(active.definitionId);
  if (profile.baseConstruction.dormitoryLevel !=
          definition.requiredDormitoryLevel ||
      active.lockedMaterialUnits != definition.materialCost ||
      active.committedWorkers != definition.workerCount) {
    return {};
  }
  const std::uint32_t before = profile.basePopulation.bedCapacity;
  profile.baseConstruction.dormitoryLevel = definition.targetDormitoryLevel;
  profile.basePopulation.bedCapacity = definition.bedCapacityAfter;
  profile.baseConstruction.activeProject.reset();
  return {true, definition.id, before, definition.bedCapacityAfter,
          active.committedWorkers};
}
