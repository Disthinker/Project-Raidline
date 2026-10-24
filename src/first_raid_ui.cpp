#include "app.h"

#include <algorithm>
#include <fmt/core.h>

#include "first_raid_guidance.h"
#include "ui_raid_hud_presentation.h"

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
  if (lines.empty()) return;
  const auto card=uiAuxiliaryHintRect(lines.size(),x,y,width);
  uiFinalPanel(renderer_,card);
  for(std::size_t i=0;i<lines.size() && i<6;++i)
    uiFinalText(renderer_,uiTextRenderer_,{card.x+10,card.y+5+22.0F*static_cast<float>(i),card.w-20,22},lines[i],12,1);
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
      developerWeaponPanelOpen_ || developerRangePanelOpen_ || developerPerformanceOverlayOpen_ ||
      tacticalMapOpen_ || medicalWheelOpen_ || throwableWheelOpen_ ||
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
    renderFirstRaidHintCard({"HOVER WEAPON FOR STATUS | RIGHT-CLICK ACTIONS | H HIDE HINTS"},20.0F,2.0F,1240.0F);
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
        lines, gameFlow_.state() == GameFlowState::RaidResult ? 260.0F : 640.0F,
        gameFlow_.state() == GameFlowState::RaidResult ? 568.0F : 132.0F,
        gameFlow_.state() == GameFlowState::RaidResult ? 760.0F : 620.0F);
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
    renderFirstRaidHintCard(lines, 800.0F, 200.0F, 464.0F);
  }
}
