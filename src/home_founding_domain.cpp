#include "home_founding_domain.h"
#include "base_facility_layout_domain.h"
#include <cmath>
#include <limits>
#include <stdexcept>

const std::array<HomePlotDefinition, 3> &homePlotDefinitions() {
  static const std::array<HomePlotDefinition, 3> plots{
      {{"home_plot.open_ground",
        "OPEN GROUND",
        "Open layout - few fixed obstacles",
        {{800, 560}, {1600, 1120}},
        {1600, 1560},
        {}},
       {"home_plot.civic_courtyard",
        "CIVIC COURTYARD",
        "Compact courtyard - fixed side buildings",
        {{3360, 560}, {1600, 1120}},
        {4160, 1560},
        {{{3780, 650}, {160, 120}}, {{4530, 1140}, {190, 130}}}},
       {"home_plot.freight_courtyard",
        "FREIGHT COURTYARD",
        "Long aisles - permanent loading shelters",
        {{5920, 560}, {1600, 1120}},
        {6720, 1560},
        {{{6330, 650}, {200, 150}},
         {{5960, 1140}, {180, 140}},
         {{7100, 1140}, {200, 120}}}}}};
  return plots;
}

const HomePlotDefinition *homePlotDefinition(std::string_view id) {
  for (const auto &plot : homePlotDefinitions())
    if (plot.id == id)
      return &plot;
  return nullptr;
}

ProfileState makeNewHomeProfile(std::string profileId,
                                const ContentRegistry &content) {
  auto profile = makeNewAlphaProfile(std::move(profileId), content);
  profile.homeFounding = {false, false, {}, 2};
  profile.regionalOperations.technologyCore.baseSiteDefinitionId = {};
  profile.basePopulation = BasePopulationState{0U, 0U};
  profile.baseWorkforce = {std::nullopt, std::nullopt};
  profile.baseResources.pool = {};
  profile.baseConstruction.facilities.clear();
  profile.baseConstruction.facilityReserveStartedWorldMinutes.clear();
  profile.baseConstruction.dormitoryLevel = 0;
  profile.baseConstruction.medicalLevel = 0;
  profile.baseConstruction.workshopLevel = 0;
  profile.baseConstruction.kitchenWaterLevel = 0;
  for (auto &[site, placements] : profile.baseFacilityLayout.placements) {
    static_cast<void>(site);
    placements.clear();
  }
  profile.basePriority = {};
  const auto validation = validateProfileState(profile, content);
  if (!validation.valid)
    throw std::logic_error(validation.message);
  return profile;
}

HomeFoundingPlan queryHomeFounding(const ProfileState &profile,
                                   std::string_view plotId,
                                   const RegionalBaseSiteDefinitionId &region,
                                   Vec2 playerCenter) {
  if (profile.homeFounding.established || profile.pendingRaid)
    return {false, "A main base is already established"};
  if (!validHomeFoundingState(profile))
    return {false, "Invalid pre-base state"};
  const auto *plot = homePlotDefinition(plotId);
  if (region != kFoundingRegion || !plot)
    return {false, "Invalid founding plot"};
  const float dx = playerCenter.x - plot->corePosition.x;
  const float dy = playerCenter.y - plot->corePosition.y;
  if (!std::isfinite(dx) || !std::isfinite(dy) ||
      dx * dx + dy * dy > 140.0F * 140.0F)
    return {false, "Approach the plot core marker"};
  for (const auto &[id, asset] : profile.assets.records()) {
    static_cast<void>(id);
    const auto *ground = std::get_if<BaseGroundAssetLocation>(&asset.location);
    if (ground && ground->baseSiteDefinitionId == region &&
        ground->position.x >= plot->bounds.position.x &&
        ground->position.y >= plot->bounds.position.y &&
        ground->position.x <= plot->bounds.position.x + plot->bounds.size.x &&
        ground->position.y <= plot->bounds.position.y + plot->bounds.size.y)
      return {false, "Retrieve ground items from this plot before founding"};
  }
  return {true, {}};
}

HomeFoundingReceipt
executeHomeFounding(ProfileState &profile, const ContentRegistry &content,
                    std::string_view plotId,
                    const RegionalBaseSiteDefinitionId &region,
                    Vec2 playerCenter, const CommandContext &context) {
  if (context.transactionId.empty())
    return {false, false, "transaction ID must not be empty"};
  if (profile.committedTransactions.contains(context.transactionId))
    return {true, true, {}};
  if (context.expectedRevision != profile.revision)
    return {false, false, "profile revision is stale"};
  if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    return {false, false, "profile revision cannot advance"};
  const auto plan = queryHomeFounding(profile, plotId, region, playerCenter);
  if (!plan.canCommit)
    return {false, false, plan.message};
  ProfileState candidate = profile;
  // Copy only the existing finite Base budget, never the template's assets.
  const auto budget = makeNewAlphaProfile(profile.profileId, content);
  candidate.basePopulation = budget.basePopulation;
  candidate.baseWorkforce = budget.baseWorkforce;
  candidate.baseConstruction = budget.baseConstruction;
  candidate.baseResources = budget.baseResources;
  candidate.basePriority = budget.basePriority;
  candidate.baseMorale = budget.baseMorale;
  candidate.baseCommunityEvent = budget.baseCommunityEvent;
  candidate.homeFounding.established = true;
  candidate.homeFounding.plots.emplace(region, std::string{plotId});
  candidate.regionalOperations.technologyCore.baseSiteDefinitionId = region;
  initializeBaseFacilityLayouts(candidate, content);
  candidate.tutorial = TutorialProgress::FindStorage;
  candidate.committedTransactions.insert(context.transactionId);
  ++candidate.revision;
  const auto validation = validateProfileState(candidate, content);
  if (!validation.valid)
    return {false, false, validation.message};
  profile = std::move(candidate);
  return {true, false, {}};
}

bool validHomeFoundingState(const ProfileState &profile) {
  const auto version = profile.homeFounding.layoutVersion;
  if (version != 1U && version != 2U)
    return false;
  if (version == 1U && (!profile.homeFounding.established ||
                        !profile.homeFounding.plots.empty()))
    return false;
  if (version == 2U && profile.homeFounding.established &&
      profile.homeFounding.plots.empty())
    return false;
  for (const auto &[region, plot] : profile.homeFounding.plots)
    if (region != kFoundingRegion || !homePlotDefinition(plot))
      return false;
  if (profile.homeFounding.established)
    return true;
  return profile.homeFounding.plots.empty() && !profile.pendingRaid &&
         !profile.regionalOperations.technologyCore.baseSiteDefinitionId
              .valid() &&
         profile.baseConstruction.facilities.empty() &&
         !profile.baseConstruction.activeProject &&
         profile.basePopulation == BasePopulationState{0U, 0U} &&
         profile.worldClock == WorldClockState{} &&
         profile.baseResources.pool == BaseResourceBundle{} &&
         profile.baseResources.resolvedDemandCycleCount == 0 &&
         profile.basePriority == BasePriorityState{} &&
         profile.baseConstruction.materialUnits == 0 &&
         profile.baseConstruction.dormitoryLevel == 0 &&
         profile.baseConstruction.medicalLevel == 0 &&
         profile.baseConstruction.workshopLevel == 0 &&
         profile.baseConstruction.kitchenWaterLevel == 0 &&
         !profile.baseWorkforce.workshopWorker &&
         !profile.baseWorkforce.medicalWorker &&
         profile.homePerimeter.sites.empty() &&
         !profile.homePerimeter.activeOuting &&
         profile.baseSiege == BaseSiegeState{};
}
