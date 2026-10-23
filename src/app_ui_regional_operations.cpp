#include "app.h"

#include <algorithm>
#include <fmt/format.h>
#include <utility>

namespace {
using C = UiRegionalOperationsControl;
using K = UiRegionalOperationsEventKind;
using T = UiRegionalOperationsTab;

constexpr SDL_FRect kRegionalTitle{454, 180, 748, 48};
constexpr SDL_FRect kRegionalFirstNumbers{454, 234, 748, 24};
constexpr SDL_FRect kRegionalSecondNumbers{454, 260, 748, 24};
constexpr SDL_FRect kRegionalBody{454, 292, 748, 178};

Vec2 center(SDL_FRect bounds) {
    return {bounds.x + bounds.w / 2, bounds.y + bounds.h / 2};
}
}

void App::resetRegionalOperationsUi() {
    // The same legacy flag is also used by ordinary deployment. Only release
    // a warning token owned by this page.
    if (regionalUi_.confirmationKey) deploymentWarningArmed_ = false;
    regionalUi_ = {};
    regionalProjection_ = {};
    regionalProjectionRevision_.reset();
    regionalViewKey_.clear();
    regionalFeedback_.clear();
    pendingRegionalEvents_.clear();
}

void App::refreshRegionalOperationsUi(bool force) {
    const auto &profile = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    if (regionalProjectionRevision_ && regionalProjection_.profileId != profile.profileId)
        resetRegionalOperationsUi();
    auto &state = regionalUi_;
    if (regionalProjectionRevision_ && regionalProjection_.language != language) {
        state.press.reset();
        if (state.confirmationKey) deploymentWarningArmed_ = false;
        state.confirmationKey.reset();
        state.detailsScroll = state.feedbackScroll = 0;
        regionalFeedback_.clear();
    }
    const auto view = uiRegionalOperationsViewKey(state);
    if (!force && regionalProjectionRevision_ &&
        *regionalProjectionRevision_ == profile.revision &&
        regionalProjection_.worldMinute == profile.worldClock.elapsedWorldMinutes &&
        regionalProjection_.language == language && regionalViewKey_ == view) return;

    const auto previousOutpost = state.selectedOutpostId;
    const auto previousSite = state.selectedSiteId;
    const auto oldContext = regionalProjection_.contextKey;
    regionalProjection_ = projectUiRegionalOperations(profile, publishedContentRegistry(), language, state);
    // Keep an explicitly stale identity until the user selects a real row.
    // Clearing it here would turn the next refresh into a default selection,
    // silently retargeting a later action to another published location.
    if (regionalProjection_.selectedOutpostId || !state.selectedOutpostId)
        state.selectedOutpostId = regionalProjection_.selectedOutpostId;
    if (regionalProjection_.selectedSiteId || !state.selectedSiteId)
        state.selectedSiteId = regionalProjection_.selectedSiteId;
    state.listPage = regionalProjection_.listPage;
    if (previousOutpost != state.selectedOutpostId || previousSite != state.selectedSiteId)
        state.detailsScroll = 0;
    if (state.confirmationKey && oldContext != regionalProjection_.contextKey) {
        state.confirmationKey.reset();
        deploymentWarningArmed_ = false;
    }
    regionalViewKey_ = uiRegionalOperationsViewKey(state);
    regionalProjectionRevision_ = profile.revision;
    ++regionalProjectionGeneration_;
}

void App::updateRegionalOperationsUi() {
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        !regionalOperationsOpen_ || lostRaidRecordsOpen_) {
        if (regionalProjectionRevision_ || regionalUi_.press ||
            regionalUi_.confirmationKey || !pendingRegionalEvents_.empty()) resetRegionalOperationsUi();
        return;
    }
    refreshRegionalOperationsUi();
    auto events = std::move(pendingRegionalEvents_);
    pendingRegionalEvents_.clear();
    for (const auto &event : events) {
        handleRegionalOperationsEvent(event);
        if (!regionalOperationsOpen_ || gameFlow_.state() != GameFlowState::Base ||
            gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate) break;
    }
}

void App::handleRegionalOperationsEvent(const UiRegionalOperationsEvent &event) {
    // These are input-priority boundaries for existing modal screens, not
    // extra domain eligibility rules for outpost staffing or establishment.
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        !regionalOperationsOpen_ || lostRaidRecordsOpen_ || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() ||
        baseDefenseResultVisible_) {
        regionalUi_.press.reset();
        if (regionalUi_.confirmationKey) deploymentWarningArmed_ = false;
        regionalUi_.confirmationKey.reset();
        return;
    }
    refreshRegionalOperationsUi(event.kind == K::Down || event.kind == K::Up);
    auto &state = regionalUi_;
    const auto &projection = regionalProjection_;
    const auto tr = [&](std::string_view value) { return localizeUiText(uiTextRenderer_.language(), value); };
    const auto feedback = [&](std::string value) {
        regionalFeedback_ = std::move(value);
        state.feedbackScroll = 0;
        uiMessage_ = regionalFeedback_;
    };
    const auto clearConfirmation = [&] {
        if (state.confirmationKey) deploymentWarningArmed_ = false;
        state.confirmationKey.reset();
    };
    const auto page = [&](int delta) {
        const auto count = uiRegionalOperationsPageCount(uiRegionalOperationsListCount(state, projection));
        state.listPage = static_cast<std::size_t>(std::clamp(
            static_cast<int>(state.listPage) + delta, 0, static_cast<int>(count) - 1));
        state.press.reset();
        clearConfirmation();
    };
    if (event.kind == K::Wheel) {
        state.press.reset();
        clearConfirmation();
        if (uiContains(uiRegionalOperationsFeedbackRect(), event.position))
            state.feedbackScroll = std::clamp(state.feedbackScroll + event.delta * 2, 0, state.feedbackMaximum);
        else if (uiContains(uiRegionalOperationsDetailsRect(), event.position))
            state.detailsScroll = std::clamp(state.detailsScroll + event.delta * 2, 0, state.detailsMaximum);
        else if (uiContains(uiRegionalOperationsListRect(), event.position)) page(event.delta);
        return;
    }
    const auto hit = hitUiRegionalOperations(event.position, state, projection);
    if (event.kind == K::Move) {
        if (state.press && hit != state.press->hit) state.press.reset();
        return;
    }
    if (event.kind == K::Down) {
        state.press.reset();
        if (hit) state.press = UiRegionalOperationsPress{*hit, uiTextRenderer_.language(),
            uiRegionalOperationsActionKey(*hit, state, projection), projection.contextKey};
        return;
    }
    const auto press = std::move(state.press);
    state.press.reset();
    if (!press || !hit || *hit != press->hit || press->language != uiTextRenderer_.language()) return;
    const auto action = uiRegionalOperationsActionKey(*hit, state, projection);
    if (press->contextKey != projection.contextKey || press->action != action) {
        clearConfirmation();
        feedback(tr("REGIONAL PLAN CHANGED - REVIEW AND CLICK AGAIN"));
        return;
    }
    const auto control = hit->control;
    if (control == C::Close) {
        resetRegionalOperationsUi();
        resetBaseDeploymentUi();
        regionalOperationsOpen_ = false;
        uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == C::Routes || control == C::Base || control == C::Sweep) {
        clearConfirmation();
        state.tab = control == C::Routes ? T::Routes : control == C::Base ? T::Base : T::Sweep;
        state.listPage = 0;
        state.detailsScroll = state.feedbackScroll = 0;
        regionalFeedback_.clear();
        refreshRegionalOperationsUi(true);
        return;
    }
    if (control == C::Previous || control == C::Next) {
        page(control == C::Previous ? -1 : 1);
        return;
    }
    if (control == C::Outpost || control == C::Site) {
        clearConfirmation();
        if (control == C::Outpost) state.selectedOutpostId = hit->outpostId;
        else state.selectedSiteId = hit->siteId;
        state.detailsScroll = state.feedbackScroll = 0;
        feedback({});
        refreshRegionalOperationsUi(true);
        return;
    }
    if (!uiRegionalOperationsEnabled(control, state, projection, hit->index)) {
        clearConfirmation();
        feedback(tr(uiRegionalOperationsUnavailable(*hit, state, projection)));
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    if (control == C::OpenConstruction || control == C::OpenReserve) {
        clearConfirmation();
        openRegionalConstruction(control == C::OpenReserve);
        resetRegionalOperationsUi();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    // Copy the immutable action arguments before any service can replace the
    // Profile or reset the UI on a successful transition.
    const auto outpostId = hit->outpostId;
    const auto siteId = hit->siteId;
    if (control == C::Restore || control == C::ClearSite || control == C::DeploySweep) {
        if (deploymentConfirmConsumedThisFrame_) return;
        deploymentConfirmConsumedThisFrame_ = true;
        const auto &profile = gameSession_.profile();
        weaponSupplyCache_ = projectWeaponSupply(profile, publishedContentRegistry(), false);
        weaponSupplyCacheProfileId_ = profile.profileId;
        weaponSupplyCacheIncludesStash_ = false;
        const bool risk = !weaponSupply(false).anyWeaponCanFire ||
            gameSession_.lostRaidAgingPreview().recordsExpiringOnNextSettlement > 0U;
        const bool confirmed = state.confirmationKey && *state.confirmationKey == action;
        deploymentWarningArmed_ = risk && confirmed;
        const bool deployed = control == C::Restore ? tryDeployFromBase(outpostId) :
            control == C::ClearSite ? tryDeployFromBase(std::nullopt, siteId) :
                tryDeployFromBase(std::nullopt, std::nullopt, siteId);
        if (deployed) {
            profileAssetSelection_.reset();
            input_.suppressPrimaryPointerUntilRelease();
            return;
        }
        if (risk && deploymentWarningArmed_) state.confirmationKey = action;
        else state.confirmationKey.reset();
        feedback(uiMessage_);
        gameAudio_.play(SoundEventId::UiDeny);
        refreshRegionalOperationsUi(true);
        return;
    }
    clearConfirmation();
    bool succeeded{};
    std::string message;
    if (control == C::Establish) {
        const auto receipt = gameSession_.executeEstablishRegionalOutpost(outpostId,
            nextProfileTransactionId("establish-outpost"));
        succeeded = receipt.succeeded;
        message = succeeded ? "REGIONAL OUTPOST ESTABLISHED" : receipt.message;
    } else if (control == C::Assign || control == C::Recall) {
        const bool assign = control == C::Assign;
        const auto receipt = gameSession_.executeRegionalOutpostStaffing(outpostId, assign,
            nextProfileTransactionId(assign ? "assign-outpost-garrison" : "clear-outpost-garrison"));
        succeeded = receipt.succeeded;
        message = succeeded ? assign ? "OUTPOST GARRISON ASSIGNED | SHORTCUTS ONLINE" :
            "OUTPOST GARRISON RETURNED | SHORTCUTS OFFLINE" : receipt.message;
    } else if (control == C::Migrate) {
        const auto receipt = gameSession_.executeBaseMigration(siteId,
            nextProfileTransactionId("migrate-main-base"));
        succeeded = receipt.succeeded;
        message = succeeded ? "MAIN BASE MIGRATION COMPLETE | OLD BASE IS AN OUTPOST" : receipt.message;
    } else if (control == C::RepairFeature) {
        const auto receipt = gameSession_.executeBaseSiteFeatureRepair(siteId,
            nextProfileTransactionId("repair-base-site-feature"));
        succeeded = receipt.succeeded;
        message = succeeded ? "BASE SITE FEATURE REPAIRED | OPERATIONAL WHILE THIS IS THE MAIN BASE" : receipt.message;
    } else return;
    feedback(std::move(message));
    gameAudio_.play(succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
    refreshRegionalOperationsUi(true);
}

void App::renderRegionalOperations() {
    if (!renderer_) return;
    refreshRegionalOperationsUi();
    const auto &projection = regionalProjection_;
    auto &state = regionalUi_;
    const auto language = uiTextRenderer_.language();
    const auto &content = publishedContentRegistry();
    const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
    const auto fill = [&](SDL_FRect bounds, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, &bounds);
    };
    const auto text = [&](SDL_FRect bounds, std::string_view value, int size = 14, int lines = 1) {
        SDL_SetRenderDrawColor(renderer_, 222, 231, 226, 255);
        uiTextRenderer_.renderBox(renderer_, bounds, value, {size, UiTextAlign::Left, lines});
    };
    const auto button = [&](C control, std::string_view label, bool selected = false, bool danger = false) {
        renderFoundationButton(9000 + static_cast<int>(control), uiRegionalOperationsControlRect(control), label,
            uiRegionalOperationsEnabled(control, state, projection), danger, selected, {}, 14);
    };
    const auto armed = [&](C control) {
        const auto hit = hitUiRegionalOperations(center(uiRegionalOperationsControlRect(control)), state, projection);
        return hit && state.confirmationKey &&
            *state.confirmationKey == uiRegionalOperationsActionKey(*hit, state, projection);
    };
    const auto duration = [&](std::uint64_t minutes) {
        return language == UiLanguage::English ? fmt::format("{}h {:02}m", minutes / kWorldMinutesPerHour,
            minutes % kWorldMinutesPerHour) : fmt::format("{}小时{:02}分", minutes / kWorldMinutesPerHour, minutes % kWorldMinutesPerHour);
    };
    const auto timestamp = [&](const WorldClockProjection &clock) {
        return language == UiLanguage::English ? fmt::format("DAY {} {:02}:{:02}", clock.day, clock.hour, clock.minute) :
            fmt::format("第{}天 {:02}:{:02}", clock.day, clock.hour, clock.minute);
    };
    const auto scroll = [&](SDL_FRect bounds, const std::string &value, int &offset, int &maximum) {
        const auto lines = wrapUiText(value, bounds.w - 16, 0,
            [&](auto line) { return uiTextRenderer_.measure(line, 14); });
        constexpr float height = 21;
        const auto visible = std::max(1, static_cast<int>(bounds.h / height));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int row = 0; row < visible && row + offset < static_cast<int>(lines.size()); ++row)
            text({bounds.x, bounds.y + row * height, bounds.w - 16, height}, lines[row + offset].text);
        if (maximum) {
            fill({bounds.x + bounds.w - 4, bounds.y, 3, bounds.h}, {51, 69, 69, 255});
            fill({bounds.x + bounds.w - 4, bounds.y + (bounds.h - 12) * offset / maximum, 3, 12}, {113, 191, 179, 255});
        }
    };
    const auto facilities = [&](const auto &ids) {
        std::string names;
        for (const auto &id : ids) {
            if (!names.empty()) names += " / ";
            try { names += tr(content.baseFacility(id).displayName); }
            catch (...) { names += tr("UNKNOWN FACILITY"); }
        }
        return names.empty() ? tr("NONE") : names;
    };
    std::string body;
    const auto line = [&](std::string value) {
        if (!body.empty()) body += '\n';
        body += std::move(value);
    };
    const auto reasonLine = [&](std::string_view label, bool available, std::string_view reason) {
        if (!available && !reason.empty()) line(fmt::format("{}: {}", tr(label), tr(reason)));
    };

    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_, &previous);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(uiRegionalOperationsPanelRect(), {21, 29, 31, 252});
    fill(uiRegionalOperationsListRect(), {27, 38, 40, 255});
    fill(uiRegionalOperationsDetailsRect(), {27, 38, 40, 255});
    fill({50, 580, 1176, 78}, {25, 35, 37, 255});
    text({62, 65, 1000, 34}, "REGIONAL ROUTES & BASE OPERATIONS", 22);
    text({62, 152, 1150, 20}, fmt::format("{} · {}", tr("CURRENT MAIN BASE"), tr(projection.activeBaseName)), 12);
    button(C::Routes, "ROUTES & OUTPOSTS", state.tab == T::Routes);
    button(C::Base, "BASE & MIGRATION", state.tab == T::Base);
    button(C::Sweep, "PERIMETER SWEEP", state.tab == T::Sweep);
    button(C::Close, "BACK");

    const auto first = projection.listPage * uiRegionalOperationsPageSize();
    if (state.tab != T::Sweep) {
        const auto count = uiRegionalOperationsListCount(state, projection);
        for (std::size_t row = 0; row < uiRegionalOperationsPageSize() && first + row < count; ++row) {
            const auto bounds = uiRegionalOperationsRowRect(row);
            std::string name, status;
            bool selected{};
            if (state.tab == T::Routes) {
                const auto &outpost = projection.outposts[first + row];
                name = tr(outpost.name);
                selected = projection.selectedOutpostId == std::optional<RegionalOutpostDefinitionId>{outpost.id};
                status = tr(outpost.activeBase ? "CURRENT MAIN BASE" : !outpost.unlocked ? "LOCKED" :
                    !outpost.established ? "NOT ESTABLISHED" : outpost.disrupted ? "DISRUPTED" : outpost.online ? "ONLINE" : "OFFLINE");
            } else {
                const auto &site = projection.sites[first + row];
                name = tr(site.name);
                selected = projection.selectedSiteId == std::optional<RegionalBaseSiteDefinitionId>{site.id};
                status = fmt::format("{} · {}", tr(site.activeBase ? "CURRENT MAIN BASE" : site.unlocked ? "UNLOCKED" : "LOCKED"),
                    tr(site.featureRepaired ? "FEATURE REPAIRED" : "FEATURE NOT REPAIRED"));
            }
            renderFoundationButton(9100 + static_cast<int>(row), bounds, {}, true, false, selected, name, 14);
            text({bounds.x + 12, bounds.y + 7, bounds.w - 24, 22}, name);
            text({bounds.x + 12, bounds.y + 32, bounds.w - 24, 20}, status, 12);
        }
        if (count == 0) text({78, 202, 328, 64}, state.tab == T::Routes ? "NO REGIONAL OUTPOST LOCATION IS PUBLISHED" :
            "NO BASE SITE IS AVAILABLE", 14, 3);
        text({78, 506, 328, 20}, fmt::format("{} / {} · {} {}", projection.listPage + 1,
            uiRegionalOperationsPageCount(count), count, tr("ENTRIES")), 12);
        button(C::Previous, "<");
        button(C::Next, ">");
    }

    if (state.tab == T::Routes) {
        if (projection.selectedOutpost) {
            const auto &outpost = *projection.selectedOutpost;
            text(kRegionalTitle, tr(outpost.name), 16, 2);
            text(kRegionalFirstNumbers, outpost.activeBase ? fmt::format("{} · {}", tr("CURRENT MAIN BASE"), tr("CORE ACTIVE")) :
                fmt::format("{} {}/{} · {}", tr("GARRISON"), outpost.assignedStaff, outpost.requiredStaff,
                    tr(outpost.disrupted ? "DISRUPTED" : outpost.online ? "ONLINE" : "OFFLINE")));
            text(kRegionalSecondNumbers, outpost.activeBase ? fmt::format("{} —", tr("SHORTCUT SAFE OPERATIONS")) :
                fmt::format("{} {}/{}", tr("SHORTCUT SAFE OPERATIONS"), outpost.remainingSafeOperations, outpost.safeOperations));
            if (outpost.activeBase) line(tr("THE MAIN BASE IS NOT A LIGHT OUTPOST"));
            line(tr("ONE-WAY ROUTES"));
            for (const auto &route : projection.routes) {
                line(fmt::format("{}  {} {}{}", tr(route.mapDisplayName), route.plan.reachable ?
                    std::to_string(route.plan.travelMinutes) : "—", language == UiLanguage::English ? "min" : "分钟",
                    route.plan.reachable && route.plan.usesOnlineOutpost ? " · " + tr("SHORTCUT ONLINE") : ""));
                if (!route.plan.reachable) line(tr(route.plan.message));
            }
            line(tr("ESTABLISHING DOES NOT ASSIGN STAFF OR ENABLE SHORTCUTS"));
            line(tr("OUTPOST ESTABLISHMENT AND STAFFING HAVE NO MATERIAL OR WORLD TIME COST"));
            if (outpost.disrupted) {
                line(tr("RESTORE BY CLEARING INITIAL HOSTILES AND EXTRACTING"));
                try { line(fmt::format("{}: {}", tr("MISSION TARGET"), tr(content.map(outpost.restorationMapDefinitionId).displayName))); }
                catch (...) { line(tr("MISSION TARGET UNAVAILABLE")); }
            }
            reasonLine("ESTABLISH", outpost.establish.canCommit, outpost.establish.message);
            reasonLine("ASSIGN GARRISON", outpost.assign.canCommit, outpost.assign.message);
            reasonLine("RECALL GARRISON", outpost.recall.canCommit, outpost.recall.message);
            if (outpost.disrupted) reasonLine("RESTORE OUTPOST", outpost.canRestore, outpost.restoreMessage);
        } else text(kRegionalTitle, "SELECT AN OUTPOST", 16, 2);
        button(C::Establish, "ESTABLISH");
        button(C::Assign, "ASSIGN GARRISON");
        button(C::Recall, "RECALL GARRISON");
        button(C::Restore, armed(C::Restore) ? "CONFIRM RESTORE" : "RESTORE OUTPOST", false, true);
    } else if (state.tab == T::Base) {
        if (projection.selectedSite) {
            const auto &site = *projection.selectedSite;
            text(kRegionalTitle, tr(site.name), 16, 2);
            text(kRegionalFirstNumbers, fmt::format("{} {} · {} {}", tr("MIGRATION TIME"), duration(site.migrationMinutes),
                tr("ARRIVAL"), site.migration.canCommit ? timestamp(site.migration.arrival) : "—"));
            text(kRegionalSecondNumbers, site.featureRepaired ? fmt::format("{} · {} {}%", tr("FEATURE REPAIRED"),
                tr("MANUFACTURING DURATION"), site.manufacturingDurationPercent) :
                fmt::format("{} {} · {} {} · {} {}", tr("REPAIR MATERIALS"), site.repairMaterialUnits,
                    tr("WORKERS REQUIRED"), site.repairWorkerCount, tr("REPAIR TIME"), duration(site.repairDurationMinutes)));
            line(fmt::format("{}: {}", tr("SITE ADVANTAGE"), tr(site.advantage)));
            line(fmt::format("{}: {}", tr("SITE DRAWBACK"), tr(site.disadvantage)));
            line(fmt::format("{}: {}", tr("SITE FEATURE"), tr(site.uniqueFeature)));
            line(fmt::format("{} · {} · {} {}%", tr(site.featureRepaired ? "FEATURE REPAIRED" : "FEATURE NOT REPAIRED"),
                tr(site.featureActive ? "FEATURE ACTIVE HERE" : "FEATURE NOT ACTIVE HERE"), tr("MANUFACTURING DURATION"),
                site.manufacturingDurationPercent));
            line(tr("MIGRATION AND REPAIR ADVANCE GAME TIME ON EXECUTION"));
            if (site.migration.canCommit || !site.migration.facilitiesEnteringReserve.empty()) {
                line(fmt::format("{}: {}", tr("FACILITIES ENTERING RESERVE"), facilities(site.migration.facilitiesEnteringReserve)));
                line(tr("OLD BASE BECOMES AN OUTPOST; RESERVED QUEUES KEEP THEIR PROGRESS"));
            }
            if (!site.migration.missingRequiredFacilities.empty())
                line(fmt::format("{}: {}", tr("MISSING CORE FACILITIES"), facilities(site.migration.missingRequiredFacilities)));
            line(fmt::format("{} {} · {} {}", tr("AVAILABLE BUILDING MATERIALS"), projection.materialUnits,
                tr("AVAILABLE WORKERS"), projection.availableWorkers));
            if (site.repair.canCommit) line(fmt::format("{}: {}", tr("REPAIR ARRIVAL"), timestamp(site.repair.arrival)));
            reasonLine("CLEAR SITE", site.clearance.canDeploy, site.clearance.message);
            reasonLine("MIGRATE", site.migration.canCommit, site.migration.message);
            reasonLine("REPAIR FEATURE", site.repair.canCommit, site.repair.message);
        } else text(kRegionalTitle, "SELECT A BASE SITE", 16, 2);
        button(C::ClearSite, armed(C::ClearSite) ? "CONFIRM CLEAR SITE" : "CLEAR SITE", false, true);
        button(C::Migrate, "MIGRATE");
        button(C::RepairFeature, "REPAIR FEATURE");
        button(C::OpenConstruction, "KITCHEN & WATER BUILD");
        button(C::OpenReserve, "OPEN FACILITY RESERVE");
    } else {
        text({78, 196, 328, 30}, "CURRENT MAIN BASE", 16);
        text({78, 236, 328, 74}, tr(projection.activeBaseName), 16, 3);
        text({78, 326, 328, 76}, "PERIMETER THREAT IS SEPARATE FROM SHORTCUT SAFE OPERATIONS", 14, 4);
        text(kRegionalTitle, "BASE PERIMETER SWEEP", 16, 2);
        text(kRegionalFirstNumbers, fmt::format("{} {}/{}", tr("BASE THREAT"), projection.threat.totalThreatUnits,
            kBaseSiegeThreatThreshold));
        text(kRegionalSecondNumbers, fmt::format("{} +{} · {} {}", tr("REGULAR RAID THREAT"), kBaseSiegeRaidThreatUnits,
            tr("SUCCESSFUL SWEEP REDUCTION"), projection.sweep.canDeploy ? "-" +
                std::to_string(projection.sweep.threatReductionUnits) : "—"));
        line(fmt::format("{}: {}", tr("AFTER SUCCESSFUL SETTLEMENT"), projection.sweep.canDeploy ?
            std::to_string(projection.sweep.projectedThreatAfterSettlement) : "—"));
        line(tr("CLEAR INITIAL HOSTILES AND EXTRACT TO REDUCE THREAT"));
        line(tr("DEPLOYING DOES NOT REDUCE THREAT; NORMAL EXITS REMAIN AVAILABLE"));
        if (projection.sweep.canDeploy) {
            try { line(fmt::format("{}: {}", tr("MISSION TARGET"), tr(content.map(projection.sweep.mapDefinitionId).displayName))); }
            catch (...) { line(tr("MISSION TARGET UNAVAILABLE")); }
        }
        reasonLine("BASE PERIMETER SWEEP", projection.sweep.canDeploy, projection.sweep.message);
        button(C::DeploySweep, armed(C::DeploySweep) ? "CONFIRM PERIMETER SWEEP" : "DEPLOY PERIMETER SWEEP", false, true);
    }
    scroll(kRegionalBody, body, state.detailsScroll, state.detailsMaximum);

    std::string reason = tr(regionalFeedback_);
    if (reason.empty() && windowHasInputFocus_ && uiPointerInside_ && pointerWorldPosition_) {
        const auto point = *pointerWorldPosition_;
        const auto hit = hitUiRegionalOperations(point, state, projection);
        if (hit && !uiRegionalOperationsEnabled(hit->control, state, projection, hit->index))
            reason = tr(uiRegionalOperationsUnavailable(*hit, state, projection));
        if (reason.empty() && hit && hit->control == C::Outpost && hit->index < projection.outposts.size())
            reason = tr(projection.outposts[hit->index].name);
        if (reason.empty() && hit && hit->control == C::Site && hit->index < projection.sites.size())
            reason = tr(projection.sites[hit->index].name);
        if (!reason.empty()) state.feedbackScroll = 0;
    }
    if (reason.empty()) reason = tr("BROWSING CHANGES NOTHING; SELECT AN ACTION TO EXECUTE");
    scroll(uiRegionalOperationsFeedbackRect(), reason, state.feedbackScroll, state.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_, previous);
}
