#include "app.h"

#include <algorithm>
#include <fmt/core.h>

#include "first_raid_guidance.h"

const WeaponSupplyProjection &App::weaponSupply(bool includeStash) {
  const auto &profile = gameSession_.profile();
  if (weaponSupplyCacheProfileId_ != profile.profileId ||
      weaponSupplyCache_.revision != profile.revision ||
      weaponSupplyCacheIncludesStash_ != includeStash) {
    weaponSupplyCache_ =
        projectWeaponSupply(profile, publishedContentRegistry(), includeStash);
    weaponSupplyCacheProfileId_ = profile.profileId;
    weaponSupplyCacheIncludesStash_ = includeStash;
  }
  return weaponSupplyCache_;
}

void App::renderFirstRaidHintCard(const std::vector<std::string> &lines,
                                  float x, float y, float width) {
  if (lines.empty())
    return;
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  const SDL_FRect card{x, y, width,
                       12.0F + 18.0F * static_cast<float>(lines.size())};
  SDL_SetRenderDrawColor(renderer_, 15, 27, 29, 242);
  SDL_RenderFillRect(renderer_, &card);
  SDL_SetRenderDrawColor(renderer_, 105, 158, 146, 255);
  SDL_RenderRect(renderer_, &card);
  SDL_SetRenderDrawColor(renderer_, 232, 237, 218, 255);
  for (const auto &line : lines) {
    uiTextRenderer_.render(renderer_, x + 10.0F, y + 6.0F, line.c_str());
    y += 18.0F;
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

std::vector<std::string> App::weaponSupplyTooltipLines(AssetInstanceId weaponId,
                                                     bool includeStash,
                                                     bool inRaid) {
  const auto &supply = weaponSupply(includeStash);
  const auto found = std::find_if(supply.weapons.begin(), supply.weapons.end(),
                                  [weaponId](const auto &weapon) {
                                    return weapon.weaponAssetId == weaponId;
                                  });
  if (found == supply.weapons.end())
    return {};
  return projectUiWeaponSupplyLines(*found, supply, inRaid, gameSession_.activeAlphaWeaponSlot());
}

void App::renderFirstRaidHints() {
  // Alpha results place the unchanged guidance in their scrollable detail
  // column; do not cover the result controls with the old floating card.
  if (gameFlow_.state() == GameFlowState::RaidResult &&
      gameSession_.world().isAlphaRaidWorld())
    return;
  if (gameFlow_.state() == GameFlowState::MainMenu || pauseMenu_.isOpen() ||
      developerWeaponPanelOpen_ || tacticalMapOpen_ || medicalWheelOpen_ ||
      baseConstructionPanelOpen_ || basePlacementState_ ||
      baseFixedFacilityPlacementState_)
    return;
  const auto stage = firstRaidGuidanceStage(gameSession_.profile());
  if (stage == FirstRaidGuidanceStage::Hidden ||
      stage == FirstRaidGuidanceStage::Survey)
    return;
  if (firstRaidHintsHiddenForRun_ && stage == FirstRaidGuidanceStage::Raid)
    return;
  const bool inventory =
      inventoryOverlayState_.isOpen() ||
      (gameFlow_.state() == GameFlowState::Base &&
       gameFlow_.activeBaseFacility() == BaseFacilityKind::Storage);
  if (inventory) {
    renderFirstRaidHintCard({"HOVER EQUIPPED WEAPON FOR FIRE STATUS, SPARES "
                             "AND NEXT ACTION | H HIDE HINTS",
                             "DRAG TO EQUIP | AMMO TO MAGAZINE | MAGAZINE TO "
                             "WEAPON | RIGHT-CLICK FOR ACTIONS"},
                            20.0F, 2.0F, 1240.0F);
    return;
  }
  std::vector<std::string> lines;
  if (stage == FirstRaidGuidanceStage::Return) {
    if (gameFlow_.state() == GameFlowState::Base &&
        gameFlow_.activeBaseFacility())
      return;
    lines = projectFirstRaidReturnGuidance(gameSession_.profile());
    lines.emplace_back("FIRST RAID GUIDANCE | H HIDE HINTS");
    renderFirstRaidHintCard(
        lines, 260.0F,
        gameFlow_.state() == GameFlowState::RaidResult ? 568.0F : 92.0F,
        760.0F);
    return;
  }
  if (gameFlow_.state() == GameFlowState::Base) {
    if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
        !lostRaidRecordsOpen_ && !regionalOperationsOpen_) {
      // Map difficulty and warnings are already in the deployment briefing.
      // Keep the existing hide action visible without covering its controls.
      renderFirstRaidHintCard({"FIRST RAID GUIDANCE | H HIDE HINTS"},
                             40.0F, 12.0F, 1200.0F);
      return;
    }
    if (gameFlow_.activeBaseFacility())
      return;
    lines = {"FIRST RAID | TAB PREPARE | H HIDE HINTS",
             "DRAG TO EQUIP; HOVER YOUR WEAPON TO CHECK FIRE READINESS",
             "ARMOR AND MEDICINE ARE OPTIONAL; EMPTY-HAND DEPLOY IS ALLOWED",
             "USE THE RAID GATE WHEN READY; BUILDING IS NOT REQUIRED"};
    renderFirstRaidHintCard(lines, 640.0F, 92.0F, 620.0F);
  } else if (stage == FirstRaidGuidanceStage::Raid) {
    lines = projectFirstRaidRiskGuidance(gameSession_).lines;
    if (!lines.empty())
      lines.emplace_back("FIRST RAID | H HIDE HINTS FOR THIS RUN");
    renderFirstRaidHintCard(lines, 640.0F, 590.0F, 620.0F);
  }
}
