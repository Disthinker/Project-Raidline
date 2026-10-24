#include "app.h"
#include "base_siege_warning_layout.h"
#include "developer_range_panel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>

namespace {
bool transientBaseMessage(std::string_view message) {
  // Only known ordinary acknowledgements expire. Rejections, recovery notices,
  // persistence errors and messages from future callers stay readable.
  constexpr std::array ordinary{
      "PROFILE LOADED", "NEW PROFILE CREATED", "BASE BUILD PANEL CLOSED",
      "INVENTORY UPDATED", "EQUIPMENT UPDATED", "MAGAZINE UNLOADED",
      "WEAPON CHAMBERED", "BATTERY CHANGE COMPLETE", "COMPONENT CHANGE SAVED",
      "BASE FACILITY PLACEMENT CANCELLED", "BASE PLACEMENT CANCELLED",
      "HOME REGION MAP CLOSED", "LANGUAGE UPDATED", "DEVELOPER RUNTIME PANEL CLOSED",
      "RETURNED FROM TEST RANGE | ORIGINAL PROFILE RESTORED",
      "BASE FACILITY FUNCTION OPENED", "BASE GROUND CONTAINER OPENED",
      "WEAPON MALFUNCTION CLEARED", "MEDICAL SELECTION CANCELLED",
      "CARRIED ASSET SELECTED", "ROTATED"};
  return std::find(ordinary.begin(), ordinary.end(), message) != ordinary.end();
}
constexpr SDL_FRect kBaseFeedback{24,590,400,110};
}

bool App::baseWorldHudVisible() const {
  return gameFlow_.state() == GameFlowState::Base &&
      !baseConstructionPanelOpen_ && !basePlacementState_ &&
      !baseFixedFacilityPlacementState_ && !inventoryOverlayState_.isOpen() &&
      !gameFlow_.activeBaseFacility() && !tacticalMapOpen_ &&
      !baseSiegeWarningVisible() && !baseDefenseResultVisible_ && !homeFoundingPrompt_ &&
      !developerWeaponPanelOpen_ && !developerRangePanelOpen_;
}

bool App::baseCombatStatusVisible() const {
  if (gameFlow_.state() != GameFlowState::Base) return false;
  const auto &profile = gameSession_.profile();
  return profile.currentHealth < 100 ||
      profile.medicalStatus.bleeding != BleedingSeverity::None ||
      hasPain(profile.medicalStatus) || gameSession_.baseDefenseActive() ||
      gameFlow_.baseWorld().playerSafetyZone() != HomeRegionSafetyZone::SafeCore ||
      profile.homePerimeter.activeOuting.has_value();
}

void App::updateBasePresentation(float deltaTime) {
  if (gameFlow_.state() != GameFlowState::Base) {
    baseToastInBase_ = false;
    baseToastSeconds_ = 0;
    return;
  }
  const auto &profileId = gameSession_.profile().profileId;
  if (!baseToastInBase_ || baseToastProfileId_ != profileId || baseToastMessage_ != uiMessage_) {
    baseToastInBase_ = true;
    baseToastProfileId_ = profileId;
    baseToastMessage_ = uiMessage_;
    baseToastSeconds_ = 0;
    return;
  }
  if (baseWorldHudVisible() && !pauseMenu_.isOpen() && windowHasInputFocus_ &&
      !gameSession_.baseDefenseActive() && !gameSession_.baseDailySaveBlocked() &&
      !gameSession_.baseDefenseSaveBlocked() && std::isfinite(deltaTime) && deltaTime > 0)
    baseToastSeconds_ = std::min(3.0F, baseToastSeconds_ + deltaTime);
}

int App::finalDiscreteActionAt(Vec2 point) const {
  // Accepted menu adapters intentionally resume on press, suppressing the held
  // pointer until release. Their timing is part of window-aim continuity.
  if (pauseMenu_.isOpen() || gameFlow_.state() == GameFlowState::MainMenu) return 0;
  if (homeFoundingPrompt_) {
    if (uiContains({370,405,240,42}, point)) return 401;
    if (uiContains({660,405,240,42}, point)) return 402;
    return 0;
  }
  if (developerRangePanelOpen_) {
    const auto action = rangePanelActionAt({point.x, point.y});
    return action ? 500 + static_cast<int>(*action) : 0;
  }
  if (gameFlow_.state() != GameFlowState::Base) return 0;
  if (gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked())
    return uiContains({978,197,282,32}, point) ? 701 : 0;
  if (baseSiegeWarningVisible()) {
    namespace siege = base_siege_warning_layout;
    if (uiContains(siege::close, point)) return 601;
    if (uiContains(siege::manual, point)) return 602;
    if (uiContains(siege::automatic, point)) return 603;
    return 0;
  }
  if (gameSession_.profile().baseSiege.warningActive && !relativeMouseModeActive_ &&
      uiContains(base_siege_warning_layout::banner, point)) return 604;
  if (baseDefenseResultVisible_)
    return uiContains({510,440,260,44}, point) ? 702 : 0;
  if (baseConstructionPanelOpen_ && !basePlacementState_ && !baseFixedFacilityPlacementState_)
    return baseManagementActionAt({point.x, point.y});
  return 0;
}

std::string App::finalDiscreteContext() const {
  const auto &profile = gameSession_.profile();
  std::string context = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
      static_cast<int>(gameFlow_.state()), profile.profileId, profile.revision,
      static_cast<int>(uiTextRenderer_.language()), pauseMenu_.isOpen(),
      pauseMenu_.settingsOpen(), settingsOpen_, newGameOverwriteArmed_,
      homeFoundingPrompt_.value_or(""), developerRangePanelOpen_,
      baseConstructionPanelOpen_, static_cast<int>(baseConstructionPage_),
      baseConstructionCatalogPage_, baseConstructionZoomIndex_,
      static_cast<int>(baseOperationsOverviewFilter_), baseOperationsOverviewPage_,
      selectedBasePlacedAssetId_.value_or(0),
      selectedBaseFixedFacility_ ? static_cast<int>(*selectedBaseFixedFacility_) : -1);
  context += fmt::format("|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
      fortificationBuildUi_.category, fortificationBuildUi_.placing.value_or(FortificationInstanceId{}).value,
      siegeWarningDismissed_, profile.baseSiege.siegeSequence,
      profile.baseSiege.warningActive, gameSession_.baseDefenseActive(),
      gameSession_.baseDefenseSaveBlocked(), gameSession_.baseDailySaveBlocked(),
      baseDefenseResultVisible_, baseDefenseAbandonArmed_, developerWeaponPanelOpen_,
      developerWeaponPanelBlocksGameplayThisFrame_, windowHasInputFocus_);
  if (baseFacilityContextMenu_) {
    const auto &m = *baseFacilityContextMenu_;
    context += fmt::format("|menu:{}:{}:{}:{}:{}", static_cast<int>(m.mode),
        m.assetId.value_or(0), m.fixedFacility ? static_cast<int>(*m.fixedFacility) : -1,
        m.position.x, m.position.y);
  }
  if (fortificationBuildUi_.menu) {
    context += fmt::format("|fort-menu:{}:{}:{}", fortificationBuildUi_.menu->value,
        fortificationBuildUi_.menuAt.x, fortificationBuildUi_.menuAt.y);
  }
  for (const auto selection : developerRangeSelection_) context += fmt::format("|{}", selection);
  context += fmt::format("|details:{}",baseFinalDetailsOpen_);
  return context;
}

void App::resetBaseFinalDetailsUi() {
  baseFinalDetailsOpen_=false;
  baseFinalFeedbackKey_.clear();
  baseFinalFeedbackScroll_=0;
  finalDiscretePress_.cancel();
}

bool App::routeFinalDiscretePointer(SDL_Event &event) {
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
      event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
      event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL) {
    finalDiscretePress_.cancel();
    return false;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION && finalDiscretePress_.action) {
    if (finalDiscreteActionAt({event.motion.x, event.motion.y}) != finalDiscretePress_.action ||
        finalDiscreteContext() != finalDiscretePress_.context) finalDiscretePress_.cancel();
    return false;
  }
  if ((event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP) ||
      event.button.button != SDL_BUTTON_LEFT) return false;
  if (!windowHasInputFocus_ || !uiPointerInside_) {
    finalDiscretePress_.cancel();
    input_.suppressPrimaryPointerUntilRelease();
    return true;
  }
  const int action = finalDiscreteActionAt({event.button.x, event.button.y});
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (!action) { finalDiscretePress_.cancel(); return false; }
    finalDiscretePress_.begin(finalDiscreteSubmitted_ ? 0 : action, finalDiscreteContext());
    input_.suppressPrimaryPointerUntilRelease();
    return true;
  }
  const bool hadPress = finalDiscretePress_.action != 0;
  if (hadPress && !finalDiscreteSubmitted_ && finalDiscretePress_.release(action, finalDiscreteContext())) {
    finalDiscreteSubmitted_ = true;
    // Existing discrete adapters consume one click; InputSystem has already
    // consumed the real release. No synthetic event enters SDL or gameplay.
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    input_.suppressPrimaryPointerUntilRelease();
    return false;
  }
  finalDiscretePress_.cancel();
  return hadPress || action != 0;
}

void App::renderFinalFeedback(SDL_FRect bounds, std::string_view message, int size) {
  const auto key = fmt::format("{}|{}|{}|{}|{}|{}", static_cast<int>(gameFlow_.state()),
      static_cast<int>(uiTextRenderer_.language()), bounds.x, bounds.y, bounds.w, message);
  if (key != finalFeedbackKey_) { finalFeedbackKey_ = key; finalFeedbackScroll_ = 0; }
  uiFinalScrollableText(renderer_, uiTextRenderer_, bounds, message, finalFeedbackScroll_, size);
}

bool App::routeFinalFeedbackWheel(const SDL_Event &event) {
  if (event.type != SDL_EVENT_MOUSE_WHEEL) return false;
  SDL_FRect box{};
  if (pauseMenu_.isOpen()) box = pauseMenu_.settingsOpen()
      ? SDL_FRect{320,458,640,82} : SDL_FRect{352,574,576,70};
  else if (gameFlow_.state() == GameFlowState::MainMenu) box = settingsOpen_
      ? SDL_FRect{320,458,640,82} : SDL_FRect{352,230,576,70};
  else if (gameFlow_.state() == GameFlowState::Base &&
      gameFlow_.activeBaseFacility() == BaseFacilityKind::Supply && !weaponComponentsOpen_)
    box = {540,608,676,48};
  else if (baseWorldHudVisible() && !uiMessage_.empty() &&
      !transientBaseMessage(uiMessage_) && !gameSession_.baseDailySaveBlocked() &&
      !gameSession_.baseDefenseSaveBlocked()) box = kBaseFeedback;
  if (!uiContains(box, windowUiViewport().toLogical({event.wheel.mouse_x,event.wheel.mouse_y}))) return false;
  supplyUi_.press.reset();
  const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
  finalFeedbackScroll_ = std::max(0.0F, finalFeedbackScroll_ - event.wheel.y * direction * 2);
  return true;
}

void App::renderBaseDailyHud() {
  if (!baseWorldHudVisible() || gameSession_.baseDefenseActive()) return;
  const bool zh = uiTextRenderer_.language() == UiLanguage::SimplifiedChinese;
  const auto &profile = gameSession_.profile();
  const auto clock = gameSession_.worldClockProjection();
  const auto threat = gameSession_.baseThreatProjection();
  const auto &pool = profile.baseResources.pool;
  uiFinalPanel(renderer_, {24,22,286,94});
  uiFinalTextAt(renderer_, uiTextRenderer_, 40,32,
      zh ? "基地" : "BASE", 112,18);
  uiFinalTextAt(renderer_, uiTextRenderer_, 154,36,
      fmt::format(fmt::runtime(zh ? "第{}天 {:02}:{:02}" : "DAY {} {:02}:{:02}"),
          clock.day, clock.hour, clock.minute),140,13);
  uiFinalTextAt(renderer_, uiTextRenderer_, 40,64,
      fmt::format(fmt::runtime(zh ? "资金 {}   威胁 {}/{}" : "CREDITS {}   THREAT {}/{}"),
          profile.currency, threat.totalThreatUnits, kBaseSiegeThreatThreshold),254,13);
  uiFinalTextAt(renderer_, uiTextRenderer_, 40,88,
      fmt::format(fmt::runtime(zh ? "口粮{}  卫生{}  士气{}  安保{}" : "FOOD {}  HYG {}  MOR {}  SEC {}"),
          pool.food, pool.hygiene, pool.morale, pool.security),254,12);
  if (!profile.homeFounding.established && !profile.homeFounding.hintsDismissed) {
    uiFinalPanel(renderer_, {330,22,528,58});
    uiFinalTextAt(renderer_, uiTextRenderer_, 346,32,
        zh ? "踏勘三处选址，确定主基地" : "Survey three sites and choose your home",492,14);
    uiFinalTextAt(renderer_, uiTextRenderer_, 346,54,
        zh ? "M 地图 · E 查看 · H 隐藏" : "M map · E inspect · H hide",492,12);
  }
  if (!uiMessage_.empty() && !gameSession_.baseDailySaveBlocked() &&
      !gameSession_.baseDefenseSaveBlocked()) {
    if (transientBaseMessage(uiMessage_)) {
      const float elapsed = baseToastInBase_ && baseToastProfileId_ == profile.profileId &&
          baseToastMessage_ == uiMessage_ ? baseToastSeconds_ : 0.0F;
      const float opacity = std::clamp((3.0F - elapsed) / .6F, 0.0F, 1.0F);
      if (opacity > 0)
        uiFinalText(renderer_, uiTextRenderer_, {24,646,360,24}, uiMessage_,13,1,
            {222,231,226,static_cast<Uint8>(std::lround(255 * opacity))});
    } else {
      // Persistent errors retain their full text and wheel access. Never
      // misclassify a rejection or a future message as a success by substring.
      uiFinalPanel(renderer_, kBaseFeedback, true);
      renderFinalFeedback({36,600,376,88}, uiMessage_,12);
    }
  }
}

void App::renderBaseCombatVitals() {
  if (!baseCombatStatusVisible()) return;
  const auto &profile=gameSession_.profile();
  const bool zh=uiTextRenderer_.language()==UiLanguage::SimplifiedChinese;
  const auto &content=publishedContentRegistry();
  const bool combat = gameSession_.baseDefenseActive() ||
      gameFlow_.baseWorld().playerSafetyZone() != HomeRegionSafetyZone::SafeCore ||
      profile.homePerimeter.activeOuting.has_value();
  uiFinalPanel(renderer_, {1040,638,216,combat ? 62.0F : 38.0F}, profile.currentHealth<30);
  SDL_SetRenderDrawColor(renderer_,113,191,179,255);
  SDL_RenderLine(renderer_,1052,650,1066,650);
  SDL_RenderLine(renderer_,1059,643,1059,657);
  uiFinalText(renderer_,uiTextRenderer_,{1080,643,160,22},
      fmt::format(fmt::runtime(zh?"生命 {}":"HP {}"),profile.currentHealth),14);
  const SDL_FRect track{1052,665,192,2};
  SDL_SetRenderDrawColor(renderer_,49,65,64,255);SDL_RenderFillRect(renderer_,&track);
  auto fill=track;fill.w*=std::clamp(profile.currentHealth/100.0F,0.0F,1.0F);
  SDL_SetRenderDrawColor(renderer_,profile.currentHealth<30?219:113,profile.currentHealth<30?137:191,profile.currentHealth<30?107:179,255);
  SDL_RenderFillRect(renderer_,&fill);
  std::string condition;
  if(profile.medicalStatus.bleeding!=BleedingSeverity::None)
    condition=profile.medicalStatus.bleeding==BleedingSeverity::Heavy
        ? (zh?"重度出血":"HEAVY BLEED") : (zh?"轻度出血":"LIGHT BLEED");
  if(hasPain(profile.medicalStatus))
    condition+=(condition.empty()?"":" · ")+std::string(painIsSuppressed(profile.medicalStatus)
        ? (zh?"止痛中":"PAIN SUPPRESSED") : (zh?"疼痛":"PAIN"));
  if(combat) {
    std::string weapon=zh?"无枪械":"NO FIREARM";
    if(const auto id=gameSession_.activeAlphaWeapon())
      if(const auto *asset=profile.assets.find(*id)) {
        const auto magazine=installedMagazine(profile,*id);
        weapon=fmt::format("{} · {}+{}",compactUiItemName(content.item(asset->definitionId),uiTextRenderer_.language()),
            magazine?magazineRoundCount(profile,*magazine):0U,asset->chamberedRound?1:0);
        if(asset->weaponMalfunction!=WeaponMalfunctionType::None)
          condition+=(condition.empty()?"":" · ")+std::string(zh?"卡弹 · 移鼠排障":"JAM · SWEEP MOUSE");
        else if(!asset->currentDurability)
          condition+=(condition.empty()?"":" · ")+std::string(zh?"枪械损坏":"WEAPON BROKEN");
      }
    uiFinalText(renderer_,uiTextRenderer_,{1052,674,192,18},weapon,12);
  }
  if(!condition.empty())
    uiFinalText(renderer_,uiTextRenderer_,{980,590,276,42},condition,12,2,{240,178,120,255});
}
