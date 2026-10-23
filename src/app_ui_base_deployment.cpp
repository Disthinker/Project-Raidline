#include "app.h"

#include <algorithm>
#include <fmt/format.h>

#include "first_raid_guidance.h"

namespace {
using C = UiDeploymentControl;
using K = UiDeploymentEventKind;
using T = UiDeploymentTab;

std::string deploymentSelectionKey(
    const MapDefinitionId &map, const RaidIntelligenceLoadout &intelligence,
    const std::optional<std::string> &recovery, std::size_t loadout) {
    return fmt::format("{}|{}|{}|{}|{}|{}", map.value(),
        intelligence.has(RaidIntelligenceCategory::Transport),
        intelligence.has(RaidIntelligenceCategory::Resource),
        intelligence.has(RaidIntelligenceCategory::Enemy),
        recovery.value_or(""), loadout);
}

const char *readinessIssueName(LoadoutReadinessIssue issue) {
    switch (issue) {
    case LoadoutReadinessIssue::Weapon: return "WEAPON";
    case LoadoutReadinessIssue::CompatibleAmmunition: return "CARRIED AMMO";
    case LoadoutReadinessIssue::BodyArmor: return "BODY ARMOR";
    case LoadoutReadinessIssue::ChestRig: return "CHEST RIG";
    case LoadoutReadinessIssue::Backpack: return "BACKPACK";
    }
    return "UNKNOWN";
}
}

void App::resetBaseDeploymentUi() {
    // The shared warning also belongs to regional missions. Only a token made
    // by this page gives this adapter ownership of that shared warning.
    if (deploymentUi_.confirmationKey) deploymentWarningArmed_ = false;
    deploymentUi_ = {};
    deploymentProjection_ = {};
    deploymentProjectionRevision_.reset();
    deploymentViewKey_.clear();
    deploymentFeedback_.clear();
    pendingDeploymentEvents_.clear();
}

void App::refreshBaseDeploymentUi(bool force) {
    const auto &profile = gameSession_.profile();
    const auto &content = publishedContentRegistry();
    const auto language = uiTextRenderer_.language();
    if (deploymentProjectionRevision_ && deploymentProjection_.profileId != profile.profileId)
        resetBaseDeploymentUi();
    const MapDefinitionId mapId = content.maps().empty() ? MapDefinitionId{} : selectedRaidMap().id;
    auto &s = deploymentUi_;
    // Returning from an old subpage starts with an empty projection. Locate
    // its real selected map once, while retaining an explicitly preset page.
    // Later list paging must never pull the view back to the selected map.
    const bool locateInitialSelection = !deploymentProjectionRevision_ && s.mapPage == 0;
    const bool mapChanged = deploymentProjectionRevision_ && deploymentProjection_.selectedMapId != mapId;
    if (locateInitialSelection || mapChanged) {
        const auto map = std::find_if(content.maps().begin(), content.maps().end(),
            [&](const auto &value) { return value.id == mapId; });
        if (map != content.maps().end()) s.mapPage =
            static_cast<std::size_t>(map - content.maps().begin()) / uiDeploymentMapPageSize();
    }
    if (mapChanged) {
        s.interiorPage = 0;
        s.selectedInterior.reset();
        s.briefingScroll = s.readinessScroll = 0;
        s.press.reset();
    }
    if (deploymentProjectionRevision_ && deploymentProjection_.language != language) {
        s.press.reset();
        s.briefingScroll = s.readinessScroll = s.feedbackScroll = 0;
        deploymentFeedback_.clear();
    }
    const auto viewKey = uiDeploymentViewKey(s) + deploymentSelectionKey(
        mapId, selectedRaidIntelligence_, selectedRaidSelfRecoveryRecordId_, selectedLoadoutArchetypeIndex_);
    if (!force && deploymentProjectionRevision_ &&
        *deploymentProjectionRevision_ == profile.revision &&
        deploymentProjection_.worldMinute == profile.worldClock.elapsedWorldMinutes &&
        deploymentProjection_.language == language && deploymentViewKey_ == viewKey) return;

    deploymentProjection_ = projectUiDeployment(profile, content, language, mapId,
        selectedRaidIntelligence_, selectedRaidSelfRecoveryRecordId_, selectedLoadoutArchetypeIndex_, s);
    const auto &p = deploymentProjection_;
    if (s.confirmationKey && *s.confirmationKey != p.contextKey) {
        s.confirmationKey.reset();
        deploymentWarningArmed_ = false;
    }
    s.mapPage = p.mapPage;
    s.interiorPage = p.interiorPage;
    s.selectedIntelligence = p.selectedIntelligence;
    s.selectedInterior = p.selectedInterior;
    selectedLoadoutArchetypeIndex_ = p.selectedLoadoutIndex;
    deploymentViewKey_ = uiDeploymentViewKey(s) + deploymentSelectionKey(
        mapId, selectedRaidIntelligence_, selectedRaidSelfRecoveryRecordId_, selectedLoadoutArchetypeIndex_);
    deploymentProjectionRevision_ = profile.revision;
    ++deploymentProjectionGeneration_;
}

bool App::handleBaseDeploymentConfirm() {
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        regionalOperationsOpen_ || lostRaidRecordsOpen_ || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() ||
        baseDefenseResultVisible_ || deploymentConfirmConsumedThisFrame_) return false;

    deploymentConfirmConsumedThisFrame_ = true;
    refreshBaseDeploymentUi(true);
    auto &s = deploymentUi_;
    s.press.reset();
    const std::string context = deploymentProjection_.contextKey;
    // The old shared deploy path consults this cache. Force-querying above
    // must also refresh it when facts change without a revision bump.
    weaponSupplyCache_ = deploymentProjection_.weaponSupply;
    weaponSupplyCacheProfileId_ = deploymentProjection_.profileId;
    weaponSupplyCacheIncludesStash_ = false;
    // Consult the original carried-only readiness query for the actual gate.
    const bool risk = !weaponSupply(false).anyWeaponCanFire ||
        gameSession_.lostRaidAgingPreview().recordsExpiringOnNextSettlement > 0U;
    const bool confirmed = s.confirmationKey && *s.confirmationKey == context;
    deploymentWarningArmed_ = risk && confirmed;
    const bool transitioned = tryDeployFromBase();
    if (transitioned) {
        resetBaseDeploymentUi();
        profileAssetSelection_.reset();
        input_.suppressPrimaryPointerUntilRelease();
        return true;
    }
    if (risk && deploymentWarningArmed_) s.confirmationKey = context;
    else s.confirmationKey.reset();
    deploymentFeedback_ = uiMessage_;
    s.feedbackScroll = 0;
    refreshBaseDeploymentUi(true);
    return false;
}

void App::handleBaseDeploymentEvent(const UiDeploymentEvent &event) {
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        regionalOperationsOpen_ || lostRaidRecordsOpen_ || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() ||
        baseDefenseResultVisible_) {
        deploymentUi_.press.reset();
        return;
    }
    refreshBaseDeploymentUi(event.kind == K::Down || event.kind == K::Up);
    auto &s = deploymentUi_;
    const auto &p = deploymentProjection_;
    const auto tr = [&](std::string_view value) { return localizeUiText(uiTextRenderer_.language(), value); };
    const auto feedback = [&](std::string value) {
        deploymentFeedback_ = std::move(value);
        s.feedbackScroll = 0;
        uiMessage_ = deploymentFeedback_;
    };
    if (event.kind == K::Wheel) {
        s.press.reset();
        if (uiContains(uiDeploymentFeedbackRect(), event.position))
            s.feedbackScroll = std::clamp(s.feedbackScroll + event.delta * 2, 0, s.feedbackMaximum);
        else if (uiContains(uiDeploymentReadinessRect(), event.position))
            s.readinessScroll = std::clamp(s.readinessScroll + event.delta * 2, 0, s.readinessMaximum);
        else if (uiContains(s.tab == T::Briefing ? uiDeploymentBriefingRect() : uiDeploymentIntelligenceDetailsRect(), event.position))
            s.briefingScroll = std::clamp(s.briefingScroll + event.delta * 2, 0, s.briefingMaximum);
        else if (uiContains({62, 164, 212, 348}, event.position)) {
            const auto count = uiDeploymentPageCount(p.maps.size(), uiDeploymentMapPageSize());
            s.mapPage = static_cast<std::size_t>(std::clamp(static_cast<int>(s.mapPage) + event.delta, 0, static_cast<int>(count) - 1));
        }
        return;
    }
    const auto hit = hitUiDeployment(event.position, s, p);
    if (event.kind == K::Move) {
        if (s.press && hit != s.press->hit) s.press.reset();
        return;
    }
    if (event.kind == K::Down) {
        s.press.reset();
        if (hit) s.press = UiDeploymentPress{*hit, uiTextRenderer_.language(), uiDeploymentActionKey(*hit, s, p), p.contextKey};
        return;
    }
    const auto press = std::move(s.press);
    s.press.reset();
    if (!press || !hit || *hit != press->hit || press->language != uiTextRenderer_.language()) return;
    if (press->action != uiDeploymentActionKey(*hit, s, p) || press->contextKey != p.contextKey) {
        feedback(tr("DEPLOYMENT CHANGED - REVIEW AND CLICK AGAIN"));
        return;
    }
    const auto control = hit->control;
    if (control == C::Close) {
        resetBaseDeploymentUi();
        gameFlow_.closeBaseFacility();
        uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == C::Regional || control == C::LostRecords) {
        resetBaseDeploymentUi();
        deploymentWarningArmed_ = false;
        resetRegionalOperationsUi();
        regionalOperationsOpen_ = control == C::Regional;
        lostRaidRecordsOpen_ = control == C::LostRecords;
        if (lostRaidRecordsOpen_) resetLostRecoveryUi();
        uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == C::Briefing || control == C::Intelligence) {
        s.tab = control == C::Briefing ? T::Briefing : T::Intelligence;
        s.briefingScroll = 0;
        refreshBaseDeploymentUi(true);
        return;
    }
    if (control == C::Map) {
        const auto &maps = publishedContentRegistry().maps();
        const auto found = std::find_if(maps.begin(), maps.end(), [&](const auto &map) { return map.id == hit->mapId; });
        if (found == maps.end()) return;
        const auto next = static_cast<std::size_t>(found - maps.begin());
        if (selectedRaidMapIndex_ % maps.size() != next) {
            selectedRaidMapIndex_ = next;
            selectedRaidIntelligence_ = {};
            selectedRaidSelfRecoveryRecordId_.reset();
            deploymentWarningArmed_ = false;
            s.confirmationKey.reset();
            deploymentFeedback_.clear();
            uiMessage_.clear();
            refreshBaseDeploymentUi(true);
        }
        return;
    }
    if (control == C::MapPrevious || control == C::MapNext) {
        const auto count = uiDeploymentPageCount(p.maps.size(), uiDeploymentMapPageSize());
        if (control == C::MapPrevious && s.mapPage) --s.mapPage;
        else if (control == C::MapNext && s.mapPage + 1 < count) ++s.mapPage;
        return;
    }
    if (control == C::BriefingDetails || control == C::ReadinessDetails) {
        if (control == C::BriefingDetails) { s.briefingDetails = !s.briefingDetails; s.briefingScroll = 0; }
        else { s.readinessDetails = !s.readinessDetails; s.readinessScroll = 0; }
        return;
    }
    if (control == C::IntelligenceRow || control == C::Interior) {
        if (control == C::IntelligenceRow) { s.selectedIntelligence = hit->category; s.selectedInterior.reset(); }
        else { s.selectedInterior = hit->interiorId; s.selectedIntelligence.reset(); }
        s.briefingScroll = 0;
        refreshBaseDeploymentUi(true);
        return;
    }
    if (control == C::InteriorPrevious || control == C::InteriorNext) {
        const auto count = uiDeploymentPageCount(p.interiors.size(), uiDeploymentInteriorPageSize());
        if (control == C::InteriorPrevious && s.interiorPage) --s.interiorPage;
        else if (control == C::InteriorNext && s.interiorPage + 1 < count) ++s.interiorPage;
        s.briefingScroll = 0;
        return;
    }
    if (control == C::LoadoutPrevious || control == C::LoadoutNext) {
        if (!p.loadouts.empty()) {
            const auto count = p.loadouts.size();
            selectedLoadoutArchetypeIndex_ = control == C::LoadoutPrevious
                ? (p.selectedLoadoutIndex + count - 1) % count : (p.selectedLoadoutIndex + 1) % count;
            s.readinessScroll = 0;
            refreshBaseDeploymentUi(true);
        }
        return;
    }
    if (control == C::RecoveryClear) {
        selectedRaidSelfRecoveryRecordId_.reset();
        refreshBaseDeploymentUi(true);
        return;
    }
    if (!uiDeploymentEnabled(control, s, p, hit->index)) {
        feedback(tr(uiDeploymentUnavailable(*hit, s, p)));
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    if (control == C::Deploy) { static_cast<void>(handleBaseDeploymentConfirm()); return; }
    if (control == C::IntelligenceAction) {
        const auto category = hit->category;
        handleRaidIntelligenceSelection(category);
    } else if (control == C::InteriorAction) {
        const RaidSpaceDefinitionId interiorId = hit->interiorId;
        handleRaidInteriorIntelligencePurchase(interiorId);
    } else return;
    // A successful purchase may replace the Profile. No old projection or
    // Profile reference is consulted after the original service returns.
    feedback(uiMessage_);
    refreshBaseDeploymentUi(true);
}

void App::renderBaseDeployment() {
    if (regionalOperationsOpen_) { renderRegionalOperations(); return; }
    if (lostRaidRecordsOpen_) { renderLostRaidRecords(); return; }
    refreshBaseDeploymentUi();
    const auto &p = deploymentProjection_;
    auto &s = deploymentUi_;
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
    const auto fill = [&](SDL_FRect rect, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, &rect);
    };
    const auto text = [&](SDL_FRect rect, std::string_view value, int size = 14) {
        SDL_SetRenderDrawColor(renderer_, 222, 231, 226, 255);
        uiTextRenderer_.renderBox(renderer_, rect, value, {size, UiTextAlign::Left, 1});
    };
    const auto button = [&](C control, std::string_view value, bool selected = false) {
        renderFoundationButton(7300 + static_cast<int>(control), uiDeploymentControlRect(control),
            value, uiDeploymentEnabled(control, s, p), false, selected, {}, 14);
    };
    const auto scroll = [&](SDL_FRect rect, const std::string &value, int &offset, int &maximum) {
        const auto lines = wrapUiText(value, rect.w - 15, 0,
            [&](auto line) { return uiTextRenderer_.measure(line, 14); });
        constexpr float lineHeight = 21;
        const int visible = std::max(1, static_cast<int>(rect.h / lineHeight));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int row = 0; row < visible && row + offset < static_cast<int>(lines.size()); ++row)
            text({rect.x, rect.y + row * lineHeight, rect.w - 15, lineHeight}, lines[row + offset].text);
        if (maximum) {
            fill({rect.x + rect.w - 4, rect.y, 3, rect.h}, {51, 69, 69, 255});
            fill({rect.x + rect.w - 4, rect.y + (rect.h - 12) * offset / maximum, 3, 12}, {113, 191, 179, 255});
        }
    };
    const auto clock = [&](const WorldClockProjection &value) {
        return language == UiLanguage::English
            ? fmt::format("DAY {} {:02}:{:02} {}", value.day, value.hour, value.minute, tr(worldTimeOfDayName(value.timeOfDay)))
            : fmt::format("第{}天 {:02}:{:02} {}", value.day, value.hour, value.minute, tr(worldTimeOfDayName(value.timeOfDay)));
    };
    const auto minutes = [&](std::uint32_t value) {
        return fmt::format("{} {}", value, language == UiLanguage::English ? "min" : "分钟");
    };
    const auto append = [](std::string &value, const std::string &line) {
        if (!value.empty()) value += '\n';
        value += line;
    };

    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_, &previous);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(uiDeploymentPanelRect(), {21, 29, 31, 252});
    fill({50, 100, 238, 466}, {27, 38, 40, 255});
    fill({298, 154, 540, 412}, {27, 38, 40, 255});
    fill({860, 180, 366, 386}, {27, 38, 40, 255});
    fill({50, 606, 1176, 54}, {25, 35, 37, 255});
    text({64, 64, 750, 34}, "RAID DEPLOYMENT", 22);
    text({918, 72, 296, 22}, fmt::format("{}  ¥{}", tr("CURRENCY"), p.currency));
    text({62, 112, 212, 24}, "DEPLOYMENT MAPS");
    text({62, 138, 212, 20}, fmt::format("{} / {}", p.mapValid ? p.selectedMapIndex + 1 : 0, p.maps.size()), 12);
    const auto mapFirst = p.mapPage * uiDeploymentMapPageSize();
    for (std::size_t row = 0; row < uiDeploymentMapPageSize() && mapFirst + row < p.maps.size(); ++row) {
        const auto &map = p.maps[mapFirst + row];
        renderFoundationButton(7400 + static_cast<int>(row), uiDeploymentMapRect(row), map.name,
            true, false, map.id == p.selectedMapId, map.name, 14);
    }
    if (p.maps.empty()) text({62, 174, 212, 42}, "NO DEPLOYMENT MAPS");
    button(C::MapPrevious, "<");
    button(C::MapNext, ">");
    button(C::Briefing, "ACTION BRIEFING", s.tab == T::Briefing);
    button(C::Intelligence, "INTELLIGENCE", s.tab == T::Intelligence);
    if (s.tab == T::Briefing) {
        button(C::BriefingDetails, s.briefingDetails ? "COLLAPSE" : "DETAILS", s.briefingDetails);
        std::string briefing = tr(p.mapName);
        append(briefing, tr(fmt::format("DIFFICULTY {}", p.difficulty)));
        append(briefing, tr(p.warning));
        append(briefing, "");
        append(briefing, fmt::format("{}  {}", tr("DEPARTURE"), clock(p.travel.departure)));
        append(briefing, fmt::format("{}  {}", tr("ARRIVAL"), clock(p.travel.arrival)));
        append(briefing, fmt::format("{}  {}", tr("OUTBOUND TRAVEL"), minutes(p.travel.outboundMinutes)));
        if (!p.travel.reachable) append(briefing, tr("ROUTE IS NOT AVAILABLE"));
        if (s.briefingDetails) {
            append(briefing, "");
            append(briefing, tr(p.routeProfile));
            append(briefing, fmt::format("{}  {}{}", tr("ROUTE"), tr(p.routeName), p.travel.usesOnlineOutpost ? " · " + tr("OUTPOST") : ""));
            append(briefing, fmt::format("{}  {}", tr("RETURN TRAVEL"), minutes(p.travel.returnMinutes)));
            append(briefing, fmt::format("{}  {}", tr("FAILURE REGROUP"), minutes(p.travel.failureRegroupMinutes)));
            append(briefing, "");
            append(briefing, tr("EXPEDITION FOCUS"));
            append(briefing, tr(p.wishFocusName));
            append(briefing, tr(baseWishRelevanceName(p.wish.relevance)));
            append(briefing, tr(p.wish.reason));
            const auto guidance = firstRaidGuidanceStage(gameSession_.profile());
            if (p.mapValid && guidance != FirstRaidGuidanceStage::Hidden &&
                guidance != FirstRaidGuidanceStage::Survey && guidance != FirstRaidGuidanceStage::Return) {
                append(briefing, "");
                for (const auto &line : projectFirstRaidMapBriefing(selectedRaidMap()))
                    append(briefing, tr(line));
                append(briefing, tr("GEAR WARNINGS ARE ADVISORY; CONFIRM AGAIN TO DEPLOY | H HIDE HINTS"));
            }
        }
        scroll(uiDeploymentBriefingRect(), briefing, s.briefingScroll, s.briefingMaximum);
    } else {
        for (std::size_t row = 0; row < p.intelligence.size(); ++row) {
            const auto &intel = p.intelligence[row];
            const auto rowRect = uiDeploymentIntelligenceRect(row);
            const auto actionRect = uiDeploymentIntelligenceRect(row, true);
            renderFoundationButton(7500 + static_cast<int>(row), rowRect, intel.name,
                true, false, s.selectedIntelligence == intel.category, {}, 14);
            std::string action = intel.selected ? tr("DESELECT") : intel.owned ? tr("SELECT") :
                intel.price ? fmt::format("{} ¥{}", tr("BUY AND SELECT"), *intel.price) : tr("UNAVAILABLE");
            renderFoundationButton(7510 + static_cast<int>(row), actionRect, action,
                uiDeploymentEnabled(C::IntelligenceAction, s, p, row), false, intel.selected, intel.purchase.message, 14);
            text({rowRect.x + 12, rowRect.y + 33, rowRect.w - 24, 17},
                fmt::format("{} {}", tr("INTEL STOCK"), intel.owned), 12);
        }
        text({310, 328, 336, 22}, "PERMANENT INTERIOR PLANS");
        text({310, 356, 344, 22}, fmt::format("{} / {}", p.interiors.empty() ? 0 : p.interiorPage + 1,
            uiDeploymentPageCount(p.interiors.size(), uiDeploymentInteriorPageSize())), 12);
        button(C::InteriorPrevious, "<");
        button(C::InteriorNext, ">");
        const auto first = p.interiorPage * uiDeploymentInteriorPageSize();
        for (std::size_t row = 0; row < uiDeploymentInteriorPageSize() && first + row < p.interiors.size(); ++row) {
            const auto &interior = p.interiors[first + row];
            renderFoundationButton(7520 + static_cast<int>(row), uiDeploymentInteriorRect(row), interior.name,
                true, false, s.selectedInterior == interior.id, interior.name, 14);
            const auto action = interior.known ? tr("PERMANENTLY KNOWN") : interior.price
                ? fmt::format("{} ¥{}", tr("BUY"), *interior.price) : tr("UNAVAILABLE");
            renderFoundationButton(7530 + static_cast<int>(row), uiDeploymentInteriorRect(row, true), action,
                uiDeploymentEnabled(C::InteriorAction, s, p, first + row), false, interior.known, interior.purchase.message, 14);
        }
        if (p.interiors.empty()) text({310, 402, 516, 42}, "NO PUBLISHED INTERIOR PLANS");
        std::string detail;
        if (s.selectedIntelligence) {
            const auto &intel = p.intelligence[raidIntelligenceCategoryIndex(*s.selectedIntelligence)];
            detail = fmt::format("{} · {} {} · {}", tr(intel.name), tr("INTEL STOCK"), intel.owned,
                intel.selected ? tr("SELECTED") : tr("NOT SELECTED"));
            append(detail, !intel.owned && !intel.purchase.canCommit ? tr(intel.purchase.message) : tr("ONE USE PER SELECTED RAID - PURCHASE IS NOT REFUNDED ON CLOSE"));
        } else if (s.selectedInterior) {
            const auto interior = std::find_if(p.interiors.begin(), p.interiors.end(), [&](const auto &value) { return value.id == *s.selectedInterior; });
            if (interior != p.interiors.end()) {
                detail = tr(interior->name);
                append(detail, interior->known ? tr("INTERIOR LAYOUT PERMANENTLY KNOWN") :
                    !interior->purchase.canCommit ? tr(interior->purchase.message) : tr("PERMANENT PURCHASE - NO RAID SELECTION REQUIRED"));
            }
        }
        if (detail.empty()) detail = tr("VIEWING INTELLIGENCE DOES NOT PURCHASE IT");
        scroll(uiDeploymentIntelligenceDetailsRect(), detail, s.briefingScroll, s.briefingMaximum);
    }

    text({872, 108, 342, 28}, "PREPARATION SUMMARY");
    button(C::LoadoutPrevious, "<");
    button(C::LoadoutNext, ">");
    text({964, 150, 114, 21}, "REFERENCE", 12);
    button(C::ReadinessDetails, s.readinessDetails ? "COLLAPSE" : "DETAILS", s.readinessDetails);
    std::string readiness = tr("ACTUAL FIRE READINESS");
    append(readiness, tr(p.cannotFire ? "CANNOT FIRE NOW | CHECK WEAPON IN TAB INVENTORY" : "FIRE READY NOW | NOT THE SAME AS ROLE READY"));
    if (s.readinessDetails) {
        for (const auto &weapon : p.weaponSupply.weapons) {
            const auto name = weapon.definitionKnown ? publishedContentRegistry().item(weapon.weaponDefinitionId).displayName : tr("UNKNOWN WEAPON");
            append(readiness, tr(name));
            const auto facts = projectUiWeaponSupplyLines(weapon, p.weaponSupply, false, weapon.slot);
            // Reuse the accepted inventory's exact fire, chamber, candidate
            // magazine and spare facts. Its later optional advice stays in Tab.
            for (std::size_t line = 0; line < std::min<std::size_t>(4, facts.size()); ++line)
                append(readiness, tr(facts[line]));
        }
    }
    append(readiness, "");
    append(readiness, tr("LOADOUT REFERENCE"));
    if (p.selectedLoadout) {
        const auto &loadout = *p.selectedLoadout;
        append(readiness, tr(loadout.displayName));
        append(readiness, fmt::format("{} {}/{}", tr("CARRIED AMMO"), loadout.compatibleRoundCount, loadout.minimumCompatibleRounds));
        if (loadout.ready()) append(readiness, tr("ROLE READY | EQUIPMENT AND CARRIED AMMO MATCH"));
        else {
            std::string gaps = tr("MISSING");
            for (const auto issue : loadout.issues) gaps += " · " + tr(readinessIssueName(issue));
            append(readiness, gaps);
        }
        if (s.readinessDetails) {
            append(readiness, tr(loadout.description));
            append(readiness, fmt::format("{} {}{}", tr("ROLE TARGET"), tr(loadout.recommendedMapDisplayName),
                loadout.recommendsHighRisk ? " · " + tr("HIGH-RISK") : ""));
            append(readiness, tr("REFERENCE ONLY - DOES NOT CHANGE EQUIPMENT OR MAP"));
        }
    } else append(readiness, tr("NO LOADOUT REFERENCE"));
    append(readiness, "");
    if (p.recoveryRecordId) {
        append(readiness, tr("RAID RECOVERY"));
        if (p.recovery) {
            append(readiness, tr(p.recovery->mapDisplayName));
            append(readiness, fmt::format("{} {} · {}", p.recovery->assets.size(), tr("ASSETS"), tr("RESERVE SPACE")));
        }
        if (!p.recoveryValid || !p.recoveryMapMatches) append(readiness, tr("RAID RECOVERY TARGET IS NO LONGER AVAILABLE"));
    } else append(readiness, tr("NO SELF RECOVERY TARGET"));
    if (p.expiryRisk) append(readiness, fmt::format("{} {}", tr("RECORDS EXPIRING AFTER THIS RAID"), p.aging.recordsExpiringOnNextSettlement));
    scroll(uiDeploymentReadinessRect(), readiness, s.readinessScroll, s.readinessMaximum);
    if (p.recoveryRecordId) button(C::RecoveryClear, "CLEAR TARGET");
    button(C::Regional, "REGIONS");
    button(C::LostRecords, "LOST ITEMS");
    const bool armed = s.confirmationKey && *s.confirmationKey == p.contextKey && deploymentWarningArmed_;
    button(C::Deploy, armed ? "CONFIRM DEPLOY AGAIN" : "DEPLOY ALPHA RAID", armed);
    button(C::Close, "CLOSE");
    std::string reasons;
    for (const auto &reason : p.riskReasons) append(reasons, tr(reason));
    if (!deploymentFeedback_.empty()) append(reasons, tr(deploymentFeedback_));
    if (reasons.empty()) reasons = tr("ENTER DEPLOY | TAB INVENTORY | ESC CLOSE");
    scroll(uiDeploymentFeedbackRect(), reasons, s.feedbackScroll, s.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_, previous);
}
