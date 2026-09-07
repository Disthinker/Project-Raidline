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

void App::renderWeaponSupplyTooltip(AssetInstanceId weaponId,
                                    MousePosition pointer, bool includeStash,
                                    bool inRaid) {
  const auto &supply = weaponSupply(includeStash);
  const auto found = std::find_if(supply.weapons.begin(), supply.weapons.end(),
                                  [weaponId](const auto &weapon) {
                                    return weapon.weaponAssetId == weaponId;
                                  });
  if (found == supply.weapons.end())
    return;
  const auto &weapon = *found;
  std::vector<std::string> lines;
  if (!weapon.definitionKnown)
    lines.emplace_back("UNKNOWN WEAPON DEFINITION - CHECK CONTENT");
  else if (weapon.canFireNow)
    lines.emplace_back("FIRE READY NOW | NEXT SHOT USES THE CHAMBERED ROUND");
  else if (weapon.fireResult == WeaponAmmoResult::Broken)
    lines.emplace_back("CANNOT FIRE: BROKEN WEAPON | MAINTENANCE REQUIRED");
  else if (weapon.fireResult == WeaponAmmoResult::BlockedByMalfunction)
    lines.emplace_back("CANNOT FIRE: JAMMED | CLEAR THE MALFUNCTION");
  else if (weapon.fireResult == WeaponAmmoResult::Chambered)
    lines.emplace_back("CHAMBER EMPTY | NEXT FIRE INPUT ONLY CHAMBERS A ROUND");
  else
    lines.emplace_back(
        "CANNOT FIRE: NO FEED | LOOSE OR SPARE AMMO IS NOT LOADED");
  lines.push_back(
      fmt::format("CURRENT CHAMBER {} | INSTALLED MAG {} | CARRIED LOOSE {}",
                  weapon.chamberedRounds, weapon.installedMagazineRounds,
                  weapon.carriedLooseRounds));
  lines.push_back(
      weapon.quickReloadMagazineId
          ? fmt::format(
                "R CANDIDATE: {} ROUNDS | {}", weapon.quickReloadMagazineRounds,
                weapon.quickReloadCanInstall ? "CAN RELOAD" : "CANNOT INSTALL")
          : "R CANDIDATE: NONE | USE CHEST RIG MAGAZINE POCKETS");
  lines.push_back(
      fmt::format("OTHER LOADED SPARES {} / {} ROUNDS | NOT R-READY",
                  weapon.otherLoadedSpareMagazineCount,
                  weapon.otherLoadedSpareMagazineRounds));
  if (inRaid && gameSession_.activeAlphaWeaponSlot() != weapon.slot)
    lines.emplace_back(
        "CLOSE TAB; SELECT THIS WEAPON BEFORE RELOADING OR CLEARING");
  for (const auto &suggestion : weapon.suggestions) {
    switch (suggestion.kind) {
    case WeaponSupplySuggestionKind::LoadMagazine:
      lines.emplace_back("DRAG COMPATIBLE AMMO TO A MAGAZINE TO LOAD IT");
      break;
    case WeaponSupplySuggestionKind::InstallMagazine:
      lines.emplace_back(
          "DRAG THE MAGAZINE TO THE WEAPON TO INSTALL AND CHAMBER");
      break;
    case WeaponSupplySuggestionKind::ChamberWeapon:
      lines.emplace_back(
          inRaid ? "CLOSE TAB; USE FIRE ONCE TO CHAMBER, AGAIN TO SHOOT"
                 : "RIGHT-CLICK THE WEAPON AND CHOOSE CHAMBER");
      break;
    case WeaponSupplySuggestionKind::MoveSpareToChestRig:
      lines.emplace_back("MOVE A LOADED SPARE TO A CHEST RIG MAGAZINE POCKET");
      break;
    case WeaponSupplySuggestionKind::ClearMalfunction:
      lines.emplace_back("CLOSE TAB; TRY FIRE, THEN SHAKE THE MOUSE TO CLEAR");
      break;
    case WeaponSupplySuggestionKind::MaintainWeapon:
      lines.emplace_back("USE A MAINTENANCE KIT OR THE BASE GUNSMITH");
      break;
    }
  }
  if (!supply.hasCarriedMedical)
    lines.emplace_back(
        "OPTIONAL: CARRY MEDICINE; RIG MEDICINE IS AVAILABLE ON 5");
  if (!supply.hasBodyArmor)
    lines.emplace_back(
        "OPTIONAL: EQUIP BODY ARMOR; THIS DOES NOT BLOCK DEPLOY");
  const float height = 12.0F + 18.0F * static_cast<float>(lines.size());
  renderFirstRaidHintCard(lines, std::clamp(pointer.x + 18.0F, 20.0F, 660.0F),
                          std::clamp(pointer.y + 24.0F, 54.0F, 700.0F - height),
                          600.0F);
}

void App::renderFirstRaidHints() {
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
      lines = projectFirstRaidMapBriefing(selectedRaidMap());
      lines.emplace_back(
          "GEAR WARNINGS ARE ADVISORY; CONFIRM AGAIN TO DEPLOY | H HIDE HINTS");
      renderFirstRaidHintCard(lines, 320.0F, 38.0F, 850.0F);
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
