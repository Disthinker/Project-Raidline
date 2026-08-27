#include "base_construction_domain.h"

#include "base_workforce_domain.h"

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
          profile.baseConstruction.workshopLevel,
          profile.baseConstruction.medicalLevel,
          profile.basePopulation.bedCapacity};
}

} // namespace

BaseFacilityDefinitionId baseFacilityDefinitionId(
    BaseFacilityUpgradeTarget target) {
  switch (target) {
  case BaseFacilityUpgradeTarget::Dormitory:
    return BaseFacilityDefinitionId{"base_facility.dormitory"};
  case BaseFacilityUpgradeTarget::KitchenWater:
    return BaseFacilityDefinitionId{"base_facility.kitchen_water"};
  case BaseFacilityUpgradeTarget::Workshop:
    return BaseFacilityDefinitionId{"base_facility.workshop"};
  case BaseFacilityUpgradeTarget::Medical:
    return BaseFacilityDefinitionId{"base_facility.medical"};
  }
  return {};
}

bool baseFacilityOwned(
    const ProfileState &profile,
    const BaseFacilityDefinitionId &definitionId) noexcept {
  return profile.baseConstruction.facilities.contains(definitionId);
}

bool baseFacilityInstalled(
    const ProfileState &profile,
    const BaseFacilityDefinitionId &definitionId) noexcept {
  const auto found = profile.baseConstruction.facilities.find(definitionId);
  return found != profile.baseConstruction.facilities.end() &&
      found->second == BaseConstructionState::FacilityPlacement::Installed;
}

std::optional<std::uint64_t> baseFacilityReserveStartedWorldMinute(
    const ProfileState &profile,
    const BaseFacilityDefinitionId &definitionId) noexcept {
  const auto found = profile.baseConstruction
      .facilityReserveStartedWorldMinutes.find(definitionId);
  return found == profile.baseConstruction
      .facilityReserveStartedWorldMinutes.end()
      ? std::nullopt
      : std::optional<std::uint64_t>{found->second};
}

namespace {
bool canShiftDeadline(std::uint64_t deadline, std::uint64_t minutes) noexcept {
  return deadline <= std::numeric_limits<std::uint64_t>::max() - minutes;
}

bool constructionUsesFacility(
    const ProfileState &profile, const ContentRegistry &content,
    const BaseFacilityDefinitionId &definitionId) {
  return profile.baseConstruction.activeProject.has_value() &&
      baseFacilityDefinitionId(content.baseConstructionProject(
          profile.baseConstruction.activeProject->definitionId).target) ==
      definitionId;
}
} // namespace

bool canShiftBaseFacilityTasks(
    const ProfileState &profile, const ContentRegistry &content,
    const BaseFacilityDefinitionId &definitionId,
    std::uint64_t minutes) noexcept {
  try {
    if (constructionUsesFacility(profile, content, definitionId) &&
        (!canShiftDeadline(
             profile.baseConstruction.activeProject->startedWorldMinute,
             minutes) ||
         !canShiftDeadline(
             profile.baseConstruction.activeProject->completionWorldMinute,
             minutes))) {
      return false;
    }
    if (definitionId ==
            BaseFacilityDefinitionId{"base_facility.workshop"} &&
        ((profile.baseManufacturing.activeOrder.has_value() &&
          !profile.baseManufacturing.activeOrder->outputReady &&
          (!canShiftDeadline(
               profile.baseManufacturing.activeOrder->startedWorldMinute,
               minutes) ||
           !canShiftDeadline(
               profile.baseManufacturing.activeOrder->completionWorldMinute,
               minutes))) ||
         (profile.gunsmithMaintenanceJob.has_value() &&
          (!canShiftDeadline(
               profile.gunsmithMaintenanceJob->startedWorldMinute,
               minutes) ||
           !canShiftDeadline(
               profile.gunsmithMaintenanceJob->completionWorldMinute,
               minutes))))) {
      return false;
    }
    if (definitionId ==
            BaseFacilityDefinitionId{"base_facility.medical"} &&
        profile.residentMedical.activeTreatment.has_value() &&
        (!canShiftDeadline(
             profile.residentMedical.activeTreatment->startedWorldMinute,
             minutes) ||
         !canShiftDeadline(
             profile.residentMedical.activeTreatment->completionWorldMinute,
             minutes))) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

void shiftBaseFacilityTasks(
    ProfileState &profile, const ContentRegistry &content,
    const BaseFacilityDefinitionId &definitionId,
    std::uint64_t minutes) {
  if (minutes == 0U) {
    return;
  }
  if (constructionUsesFacility(profile, content, definitionId)) {
    profile.baseConstruction.activeProject->startedWorldMinute += minutes;
    profile.baseConstruction.activeProject->completionWorldMinute += minutes;
  }
  if (definitionId == BaseFacilityDefinitionId{"base_facility.workshop"}) {
    if (profile.baseManufacturing.activeOrder.has_value() &&
        !profile.baseManufacturing.activeOrder->outputReady) {
      profile.baseManufacturing.activeOrder->startedWorldMinute += minutes;
      profile.baseManufacturing.activeOrder->completionWorldMinute += minutes;
    }
    if (profile.gunsmithMaintenanceJob.has_value()) {
      profile.gunsmithMaintenanceJob->startedWorldMinute += minutes;
      profile.gunsmithMaintenanceJob->completionWorldMinute += minutes;
    }
  }
  if (definitionId == BaseFacilityDefinitionId{"base_facility.medical"} &&
      profile.residentMedical.activeTreatment.has_value()) {
    profile.residentMedical.activeTreatment->startedWorldMinute += minutes;
    profile.residentMedical.activeTreatment->completionWorldMinute += minutes;
  }
}

std::uint32_t baseFacilityLevel(const BaseConstructionState &state,
                                BaseFacilityUpgradeTarget target) noexcept {
  switch (target) {
  case BaseFacilityUpgradeTarget::Dormitory:
    return state.dormitoryLevel;
  case BaseFacilityUpgradeTarget::KitchenWater:
    return state.kitchenWaterLevel;
  case BaseFacilityUpgradeTarget::Workshop:
    return state.workshopLevel;
  case BaseFacilityUpgradeTarget::Medical:
    return state.medicalLevel;
  }
  return 0U;
}

void setFacilityLevel(BaseConstructionState &state,
                      BaseFacilityUpgradeTarget target,
                      std::uint32_t level) noexcept {
  switch (target) {
  case BaseFacilityUpgradeTarget::Dormitory:
    state.dormitoryLevel = level;
    break;
  case BaseFacilityUpgradeTarget::KitchenWater:
    state.kitchenWaterLevel = level;
    break;
  case BaseFacilityUpgradeTarget::Workshop:
    state.workshopLevel = level;
    break;
  case BaseFacilityUpgradeTarget::Medical:
    state.medicalLevel = level;
    break;
  }
}

BaseConstructionProjection
projectBaseConstruction(const ProfileState &profile,
                        const ContentRegistry &content) noexcept {
  BaseConstructionProjection projection{
      profile.baseConstruction.materialUnits,
      content.maximumBaseConstructionMaterials(),
      profile.baseConstruction.dormitoryLevel,
      profile.baseConstruction.workshopLevel,
      profile.baseConstruction.medicalLevel,
      profile.basePopulation.bedCapacity,
      profile.basePopulation.ordinaryResidents >
              profile.basePopulation.injuredResidents
          ? profile.basePopulation.ordinaryResidents -
                profile.basePopulation.injuredResidents
          : 0U};
  if (profile.baseConstruction.activeProject.has_value()) {
    const ActiveBaseConstructionProject &active =
        *profile.baseConstruction.activeProject;
    projection.committedWorkers = active.committedWorkers;
    projection.activeProjectId = active.definitionId;
    std::uint64_t progressWorldMinute =
        profile.worldClock.elapsedWorldMinutes;
    const auto definition = std::find_if(
        content.baseConstructionProjects().begin(),
        content.baseConstructionProjects().end(),
        [&](const BaseConstructionProjectDefinition &candidate) {
          return candidate.id == active.definitionId;
        });
    if (definition != content.baseConstructionProjects().end()) {
      if (const std::optional<std::uint64_t> reserveStarted =
              baseFacilityReserveStartedWorldMinute(
                  profile, baseFacilityDefinitionId(definition->target));
          reserveStarted.has_value()) {
        progressWorldMinute = *reserveStarted;
      }
    }
    if (active.completionWorldMinute > progressWorldMinute) {
      projection.remainingMinutes =
          active.completionWorldMinute - progressWorldMinute;
    }
  }
  projection.availableWorkers = availableBaseWorkers(profile);
  return projection;
}

ConstructionMaterialPlan queryConstructionMaterialContribution(
    const ProfileState &profile, const ContentRegistry &content,
    const ContributeConstructionMaterialCommand &command) {
  if (profile.pendingRaid.has_value()) {
    return materialFailure(DomainErrorCode::IllegalDestination,
                           "Base allocation is unavailable during a Raid",
                           profile.revision);
  }
  const AssetRecord *asset = profile.assets.find(command.assetId);
  if (asset == nullptr) {
    return materialFailure(DomainErrorCode::MissingAsset,
                           "allocation item does not exist", profile.revision);
  }
  if (!assetIsBaseAccessible(profile, asset->instanceId)) {
    return materialFailure(
        DomainErrorCode::IllegalDestination,
        "only Base-accessible personal assets can become construction material",
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
  const std::uint32_t currentLevel = baseFacilityLevel(
      profile.baseConstruction, definition->target);
  if (currentLevel != definition->requiredLevel) {
    return constructionFailure(DomainErrorCode::IllegalDestination,
                               "facility level does not match this project",
                               profile.revision);
  }
  if (currentLevel > 0U && !baseFacilityInstalled(
          profile, baseFacilityDefinitionId(definition->target))) {
    return constructionFailure(
        DomainErrorCode::IllegalDestination,
        "facility must be installed before it can be upgraded",
        profile.revision);
  }
  if (profile.baseConstruction.materialUnits < definition->materialCost) {
    return constructionFailure(DomainErrorCode::Capacity,
                               "insufficient Base construction material",
                               profile.revision);
  }
  const std::uint32_t availableWorkers = availableBaseWorkers(profile);
  if (availableWorkers < definition->workerCount) {
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
          definition->target,
          currentLevel,
          definition->targetLevel,
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
            profile.baseConstruction.workshopLevel,
            profile.baseConstruction.medicalLevel,
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
          profile.baseConstruction.workshopLevel,
          profile.baseConstruction.medicalLevel,
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
            profile.baseConstruction.workshopLevel,
            profile.baseConstruction.medicalLevel,
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
          profile.baseConstruction.workshopLevel,
          profile.baseConstruction.medicalLevel,
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
  const std::uint32_t beforeLevel = baseFacilityLevel(
      profile.baseConstruction, definition.target);
  if (beforeLevel > 0U && !baseFacilityInstalled(
          profile, baseFacilityDefinitionId(definition.target))) {
    return {};
  }
  if (beforeLevel != definition.requiredLevel ||
      active.lockedMaterialUnits != definition.materialCost ||
      active.committedWorkers != definition.workerCount) {
    return {};
  }
  const std::uint32_t before = profile.basePopulation.bedCapacity;
  setFacilityLevel(
      profile.baseConstruction, definition.target, definition.targetLevel);
  if (beforeLevel == 0U && definition.targetLevel > 0U) {
    profile.baseConstruction.facilities[
        baseFacilityDefinitionId(definition.target)] =
        BaseConstructionState::FacilityPlacement::Installed;
  }
  if (definition.target == BaseFacilityUpgradeTarget::Dormitory) {
    profile.basePopulation.bedCapacity = definition.bedCapacityAfter;
  }
  profile.baseConstruction.activeProject.reset();
  return {true,
          definition.id,
          definition.target,
          beforeLevel,
          definition.targetLevel,
          before,
          profile.basePopulation.bedCapacity,
          active.committedWorkers};
}

InstallBaseFacilityPlan queryInstallBaseFacility(
    const ProfileState &profile, const ContentRegistry &content,
    const InstallBaseFacilityCommand &command) noexcept {
  try {
    static_cast<void>(content.baseFacility(command.definitionId));
  } catch (...) {
    return {false, DomainErrorCode::IllegalDestination,
            "Base facility definition does not exist", profile.revision,
            command.definitionId};
  }
  if (profile.pendingRaid.has_value()) {
    return {false, DomainErrorCode::IllegalDestination,
            "Base facility installation is unavailable during a Raid",
            profile.revision, command.definitionId};
  }
  const auto found = profile.baseConstruction.facilities.find(
      command.definitionId);
  if (found == profile.baseConstruction.facilities.end()) {
    return {false, DomainErrorCode::MissingAsset,
            "Base facility is not owned", profile.revision,
            command.definitionId};
  }
  if (found->second != BaseConstructionState::FacilityPlacement::Reserve) {
    return {false, DomainErrorCode::IllegalDestination,
            "Base facility is already installed", profile.revision,
            command.definitionId};
  }
  const std::optional<std::uint64_t> reserveStarted =
      baseFacilityReserveStartedWorldMinute(profile, command.definitionId);
  if (!reserveStarted.has_value() ||
      *reserveStarted > profile.worldClock.elapsedWorldMinutes ||
      !canShiftBaseFacilityTasks(
          profile, content, command.definitionId,
          profile.worldClock.elapsedWorldMinutes - *reserveStarted)) {
    return {false, DomainErrorCode::InvalidProfile,
            "Base facility reserve timing is invalid", profile.revision,
            command.definitionId};
  }
  return {true, DomainErrorCode::None, {}, profile.revision,
          command.definitionId};
}

InstallBaseFacilityReceipt executeInstallBaseFacility(
    ProfileState &profile, const ContentRegistry &content,
    const InstallBaseFacilityCommand &command,
    const CommandContext &context) {
  if (context.transactionId.empty()) {
    return {false, false, DomainErrorCode::InvalidTransaction,
            "transaction ID is empty", profile.revision,
            command.definitionId};
  }
  if (profile.committedTransactions.contains(context.transactionId)) {
    return {true, true, DomainErrorCode::None, {}, profile.revision,
            command.definitionId};
  }
  if (context.expectedRevision != profile.revision) {
    return {false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision,
            command.definitionId};
  }
  if (profile.revision == std::numeric_limits<ProfileRevision>::max()) {
    return {false, false, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance", profile.revision,
            command.definitionId};
  }
  const InstallBaseFacilityPlan plan = queryInstallBaseFacility(
      profile, content, command);
  if (!plan.canCommit) {
    return {false, false, plan.error, plan.message, profile.revision,
            command.definitionId};
  }
  ProfileState candidate = profile;
  const std::uint64_t reserveStarted = candidate.baseConstruction
      .facilityReserveStartedWorldMinutes.at(command.definitionId);
  shiftBaseFacilityTasks(
      candidate, content, command.definitionId,
      candidate.worldClock.elapsedWorldMinutes - reserveStarted);
  candidate.baseConstruction.facilities[command.definitionId] =
      BaseConstructionState::FacilityPlacement::Installed;
  candidate.baseConstruction.facilityReserveStartedWorldMinutes.erase(
      command.definitionId);
  candidate.committedTransactions.insert(context.transactionId);
  ++candidate.revision;
  const ProfileValidationResult validation = validateProfileState(
      candidate, content);
  if (!validation.valid) {
    return {false, false, DomainErrorCode::InvalidProfile,
            validation.message, profile.revision, command.definitionId};
  }
  profile = std::move(candidate);
  return {true, false, DomainErrorCode::None, {}, profile.revision,
          command.definitionId};
}
