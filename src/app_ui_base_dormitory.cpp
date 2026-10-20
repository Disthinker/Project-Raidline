#include "app.h"

#include <algorithm>
#include <fmt/format.h>

namespace {
using Control = UiDormitoryControl;
using Kind = UiDormitoryEventKind;
}

void App::resetBaseDormitoryUi()
{
    dormitoryUi_ = {};
    dormitoryProjection_ = {};
    dormitoryProjectionRevision_.reset();
    pendingDormitoryEvents_.clear();
    dormitoryFeedback_.clear();
}

void App::refreshBaseDormitoryUi(bool force)
{
    const auto &profile = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    if (dormitoryProjectionRevision_ && dormitoryProjection_.profileId != profile.profileId)
        resetBaseDormitoryUi();
    if (dormitoryProjectionRevision_ && dormitoryProjectionLanguage_ != language) {
        dormitoryUi_.press.reset();
        dormitoryUi_.detailsScroll = dormitoryUi_.feedbackScroll = 0;
        dormitoryFeedback_.clear();
    }
    if (!force && dormitoryProjectionRevision_ && *dormitoryProjectionRevision_ == profile.revision &&
        dormitoryProjection_.worldMinute == profile.worldClock.elapsedWorldMinutes &&
        dormitoryProjectionLanguage_ == language && dormitoryProjection_.restHours == dormitoryUi_.restHours)
        return;
    dormitoryProjection_ = projectUiDormitory(profile, publishedContentRegistry(), dormitoryUi_.restHours);
    dormitoryProjectionRevision_ = profile.revision;
    dormitoryProjectionLanguage_ = language;
    ++dormitoryProjectionGeneration_;
}

void App::handleBaseDormitoryEvent(const UiDormitoryEvent &event)
{
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::Dormitory || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        dormitoryUi_.press.reset();
        return;
    }
    refreshBaseDormitoryUi(event.kind == Kind::Down || event.kind == Kind::Up);
    if (event.kind == Kind::Wheel) {
        dormitoryUi_.press.reset();
        if (uiContains(uiDormitoryFeedbackRect(), event.position))
            dormitoryUi_.feedbackScroll = std::clamp(dormitoryUi_.feedbackScroll + event.delta * 2, 0, dormitoryUi_.feedbackMaximum);
        else if (dormitoryUi_.tab == UiDormitoryTab::Residents && uiContains(uiDormitoryDetailsRect(), event.position))
            dormitoryUi_.detailsScroll = std::clamp(dormitoryUi_.detailsScroll + event.delta * 2, 0, dormitoryUi_.detailsMaximum);
        return;
    }
    const auto hit = hitUiDormitory(event.position, dormitoryUi_, dormitoryProjection_);
    if (event.kind == Kind::Move) {
        if (dormitoryUi_.press && hit != dormitoryUi_.press->control) dormitoryUi_.press.reset();
        return;
    }
    const auto language = uiTextRenderer_.language();
    if (event.kind == Kind::Down) {
        dormitoryUi_.press.reset();
        if (hit) dormitoryUi_.press = UiDormitoryPress{*hit, language, uiDormitoryActionKey(*hit, dormitoryProjection_)};
        return;
    }
    const auto press = dormitoryUi_.press;
    dormitoryUi_.press.reset();
    if (!press || !hit || *hit != press->control || press->language != language) return;
    const auto tr = [&](std::string_view text) { return localizeUiText(language, text); };
    const auto feedback = [&](std::string text) {
        dormitoryFeedback_ = std::move(text);
        uiMessage_ = dormitoryFeedback_;
        dormitoryUi_.feedbackScroll = 0;
    };
    if (press->action != uiDormitoryActionKey(*hit, dormitoryProjection_)) {
        feedback(tr("DORMITORY PLAN CHANGED - CHECK AND CLICK AGAIN"));
        return;
    }
    const Control control = *hit;
    if (control == Control::Close) {
        gameFlow_.closeBaseFacility();
        resetBaseDormitoryUi();
        uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == Control::Residents || control == Control::Rest) {
        const auto tab = control == Control::Residents ? UiDormitoryTab::Residents : UiDormitoryTab::Rest;
        if (dormitoryUi_.tab != tab) {
            dormitoryUi_.tab = tab;
            dormitoryUi_.restHours.reset();
            dormitoryUi_.detailsScroll = 0;
            dormitoryFeedback_.clear();
            refreshBaseDormitoryUi();
        }
        return;
    }
    if (control == Control::Rations) {
        dormitoryUi_.rations = !dormitoryUi_.rations;
        return;
    }
    if (control == Control::Hour1 || control == Control::Hour6 || control == Control::Hour12) {
        dormitoryUi_.restHours = control == Control::Hour1 ? 1U : control == Control::Hour6 ? 6U : 12U;
        dormitoryFeedback_.clear();
        refreshBaseDormitoryUi();
        return;
    }
    if (!uiDormitoryEnabled(control, dormitoryProjection_)) {
        feedback(tr(uiDormitoryUnavailable(control, dormitoryProjection_)));
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    bool succeeded = false;
    if (control == Control::StartRest && dormitoryProjection_.restHours) {
        const auto hours = *dormitoryProjection_.restHours;
        const auto receipt = gameSession_.executeBaseRest(hours, nextProfileTransactionId("dormitory-rest"));
        succeeded = receipt.succeeded;
        feedback(succeeded ? fmt::format("{} {} {} · {} {}", tr("REST COMPLETE"), hours, tr("REST HOURS UNIT"),
            tr("DAILY SETTLEMENTS"), receipt.dailyCyclesResolved) : tr(receipt.message));
        if (succeeded) dormitoryUi_.restHours.reset();
    } else if (control == Control::AutoFill) {
        const auto before = gameSession_.profile().baseWorkforce;
        const auto receipt = gameSession_.executeAutoFillBaseWorkers(nextProfileTransactionId("dormitory-autofill"));
        succeeded = receipt.succeeded;
        if (succeeded) {
            const auto &after = gameSession_.profile().baseWorkforce;
            std::string result = tr("VACANT JOBS FILLED");
            if (!before.workshopWorker && after.workshopWorker)
                result += fmt::format(" · {}: {}", tr("WORKSHOP JOB"), tr(baseResidentProfessionName(*after.workshopWorker)));
            if (!before.medicalWorker && after.medicalWorker)
                result += fmt::format(" · {}: {}", tr("MEDICAL JOB"), tr(baseResidentProfessionName(*after.medicalWorker)));
            if (!after.workshopWorker || !after.medicalWorker) result += " · " + tr("SOME JOBS REMAIN VACANT");
            feedback(std::move(result));
        } else feedback(tr(receipt.message));
    } else if (dormitoryProjection_.project &&
        (control == Control::StartExpansion || control == Control::CancelExpansion)) {
        const auto id = dormitoryProjection_.project->id;
        const bool cancel = control == Control::CancelExpansion;
        const auto receipt = cancel ? gameSession_.executeCancelBaseConstruction(id, nextProfileTransactionId("dormitory-cancel-expansion"))
            : gameSession_.executeStartBaseConstruction(id, nextProfileTransactionId("dormitory-start-expansion"));
        succeeded = receipt.succeeded;
        feedback(tr(succeeded ? cancel ? "CONSTRUCTION CANCELLED | MATERIAL REFUNDED" : "DORMITORY EXPANSION STARTED" : receipt.message));
    }
    gameAudio_.play(succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
    refreshBaseDormitoryUi(true);
}

void App::renderBaseDormitory()
{
    refreshBaseDormitoryUi();
    const auto &p = dormitoryProjection_;
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view s) { return localizeUiText(language, s); };
    const auto fill = [&](SDL_FRect r, SDL_Color c) {
        SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer_, &r);
    };
    const auto text = [&](SDL_FRect r, std::string_view s, int size = 14) {
        SDL_SetRenderDrawColor(renderer_, 222, 231, 226, 255);
        uiTextRenderer_.renderBox(renderer_, r, s, {size, UiTextAlign::Left, 0});
    };
    const auto button = [&](Control c, std::string_view label, bool selected = false) {
        renderFoundationButton(6100 + static_cast<int>(c), uiDormitoryControlRect(c), label,
            uiDormitoryEnabled(c, p), c == Control::CancelExpansion, selected, {}, 14);
    };
    const auto duration = [&](std::uint64_t m) {
        return fmt::format("{}{} {}{}", m / 60, language == UiLanguage::English ? "h" : "小时",
            m % 60, language == UiLanguage::English ? "m" : "分");
    };
    const auto clock = [&](const WorldClockProjection &value) {
        const auto date = fmt::format(fmt::runtime(tr("DAY {}")), value.day);
        return fmt::format("{} {:02}:{:02} {}", date, value.hour, value.minute,
            tr(worldTimeOfDayName(value.timeOfDay)));
    };
    const auto scroll = [&](SDL_FRect r, std::string_view value, int &offset, int &maximum) {
        const auto lines = wrapUiText(tr(value), r.w - 14, 0, [&](auto s) { return uiTextRenderer_.measure(s, 14); });
        constexpr float lineHeight = 21;
        const int visible = std::max(1, static_cast<int>(r.h / lineHeight));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int i = 0; i < visible && i + offset < static_cast<int>(lines.size()); ++i)
            text({r.x, r.y + i * lineHeight, r.w - 14, lineHeight}, lines[i + offset].text);
        if (maximum) {
            fill({r.x + r.w - 4, r.y, 3, r.h}, {51, 69, 69, 255});
            fill({r.x + r.w - 4, r.y + (r.h - 12) * offset / maximum, 3, 12}, {113, 191, 179, 255});
        }
    };
    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_, &previous);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill({40, 60, 1200, 620}, {21, 29, 31, 252});
    text({64, 80, 1000, 34}, "DORMITORY & REST", 22);
    button(Control::Close, language == UiLanguage::English ? "CLOSE" : tr("CLOSE DORMITORY"));
    button(Control::Residents, "PEOPLE AND EXPANSION", dormitoryUi_.tab == UiDormitoryTab::Residents);
    button(Control::Rest, language == UiLanguage::English ? "REST" : tr("REST TAB"), dormitoryUi_.tab == UiDormitoryTab::Rest);
    if (dormitoryUi_.tab == UiDormitoryTab::Residents) {
        const std::array<std::pair<const char *, std::uint32_t>, 6> stats{{
            {"RESIDENTS", p.population.ordinaryResidents}, {"HEALTHY", p.population.healthyResidents},
            {"INJURED", p.population.injuredResidents}, {"BED CAPACITY", p.population.bedCapacity},
            {"BED SHORTFALL", p.population.bedShortfall}, {"AVAILABLE WORKERS", p.workforce.availableResidents}}};
        for (std::size_t i = 0; i < stats.size(); ++i) {
            const float x = 64 + static_cast<float>(i) * 194;
            fill({x, 188, 182, 74}, {27, 38, 40, 255});
            text({x + 12, 198, 158, 20}, stats[i].first, 12);
            text({x + 12, 223, 158, 28}, fmt::format("{}", stats[i].second), 20);
        }
        fill({64, 276, 548, 258}, {27, 38, 40, 255});
        fill({632, 276, 584, 258}, {27, 38, 40, 255});
        text({84, 286, 508, 24}, dormitoryUi_.rations ? "FOOD AND DAILY NEEDS" : "RESIDENTS AND JOBS", 16);
        if (dormitoryUi_.rations) {
            text({84, 322, 508, 24}, fmt::format("{}  {}", tr("PUBLIC FOOD STOCK"), p.foodStock));
            text({84, 350, 508, 24}, fmt::format("{}  {}", tr("DAILY RATIONS"), p.dailyDemand.food));
            text({84, 378, 508, 24}, fmt::format("{}  {}", tr("NEXT DAILY SETTLEMENT"), duration(p.clock.minutesUntilNextDay)));
            text({84, 407, 508, 34}, "FOOD SUMMARY IS NOT A SURVIVAL FORECAST", 12);
        } else {
            text({84, 318, 240, 20}, "RESIDENT PROFESSIONS", 12);
            text({354, 318, 100, 20}, language == UiLanguage::English ? "TOTAL" : tr("RESIDENT TOTAL"), 12);
            text({476, 318, 110, 20}, "INJURED", 12);
            for (std::size_t i = 0; i < kBaseResidentProfessionCount; ++i) {
                const float y = 342 + static_cast<float>(i) * 23;
                text({84, y, 250, 22}, baseResidentProfessionName(static_cast<BaseResidentProfession>(i)));
                text({354, y, 100, 22}, fmt::format("{}", p.workforce.residentsByProfession[i]));
                text({476, y, 110, 22}, fmt::format("{}", p.workforce.injuredByProfession[i]));
            }
        }
        text({84, 444, 508, 24}, fmt::format("{}: {}", tr("WORKSHOP JOB"),
            p.workforce.workshopWorker ? tr(baseResidentProfessionName(*p.workforce.workshopWorker)) : tr("EMPTY")));
        text({84, 472, 508, 24}, fmt::format("{}: {}", tr("MEDICAL JOB"),
            p.workforce.medicalWorker ? tr(baseResidentProfessionName(*p.workforce.medicalWorker)) : tr("EMPTY")));
        text({84, 503, 508, 22}, "FILLS ONLY ELIGIBLE VACANT JOBS", 12);
        text({652, 286, 544, 24}, p.ownExpansion ? "EXPANSION IN PROGRESS" : p.expansionComplete ?
            "DORMITORY EXPANSION COMPLETE" : p.activeProject ? "CONSTRUCTION SLOT OCCUPIED" : "DORMITORY EXPANSION", 16);
        std::string details;
        if (p.project) {
            const auto &project = *p.project;
            details = fmt::format("{} {} → {}\n{} {} → {}\n{} {} / {}\n{} {} / {}\n{} {}",
                tr("DORMITORY LEVEL"), p.construction.dormitoryLevel, project.targetLevel,
                tr("BED CAPACITY"), p.population.bedCapacity, p.expansionComplete ? p.population.bedCapacity : project.bedCapacityAfter,
                tr("AVAILABLE / REQUIRED MATERIAL"), p.construction.materialUnits, p.ownExpansion ? p.activeProject->lockedMaterialUnits : project.materialCost,
                tr(p.ownExpansion ? "WORKERS COMMITTED" : "AVAILABLE / REQUIRED WORKERS"),
                p.ownExpansion ? p.activeProject->committedWorkers : p.workforce.availableResidents,
                p.ownExpansion ? p.activeProject->committedWorkers : project.workerCount,
                tr(p.ownExpansion ? "WORLD TIME REMAINING" : "EXPANSION DURATION"),
                duration(p.ownExpansion ? p.construction.remainingMinutes : project.durationMinutes));
            if (p.ownExpansion)
                details = fmt::format("{} {}\n{} {} → {}\n{} {}\n{} {}\n{} {}",
                    tr("DORMITORY LEVEL"), p.construction.dormitoryLevel,
                    tr("BED CAPACITY"), p.population.bedCapacity, project.bedCapacityAfter,
                    tr("COMMITTED MATERIAL"), p.activeProject->lockedMaterialUnits,
                    tr("WORKERS COMMITTED"), p.activeProject->committedWorkers,
                    tr("WORLD TIME REMAINING"), duration(p.construction.remainingMinutes));
            else if (p.expansionComplete)
                details = fmt::format("{} {}\n{} {}\n{}", tr("DORMITORY LEVEL"), p.construction.dormitoryLevel,
                    tr("BED CAPACITY"), p.population.bedCapacity, tr("NO FURTHER PROJECT"));
            if (p.ownExpansion && !p.cancel.canCommit) details += "\n" + tr(p.cancel.message);
            else if (!p.ownExpansion && !p.expansionComplete && !p.start.canCommit) details += "\n" + tr(p.start.message);
            if (p.population.bedShortfall) details += "\n" + tr("BED SHORTFALL DOES NOT BLOCK PLAY");
        } else details = tr("NO DORMITORY PROJECT");
        scroll(uiDormitoryDetailsRect(), details, dormitoryUi_.detailsScroll, dormitoryUi_.detailsMaximum);
        button(Control::AutoFill, "AUTO FILL JOBS");
        button(Control::Rations, dormitoryUi_.rations ? "HIDE FOOD SUMMARY" : "FOOD SUMMARY", dormitoryUi_.rations);
        if (p.ownExpansion) button(Control::CancelExpansion, "CANCEL & REFUND");
        button(Control::StartExpansion, p.ownExpansion ? "EXPANSION IN PROGRESS" :
            p.expansionComplete ? "DORMITORY EXPANSION COMPLETE" : "START DORMITORY EXPANSION");
    } else {
        fill({64, 188, 360, 346}, {27, 38, 40, 255});
        fill({444, 188, 772, 346}, {27, 38, 40, 255});
        text({84, 206, 320, 26}, "REST DURATION", 16);
        button(Control::Hour1, "REST 1 HOUR", p.restHours == 1);
        button(Control::Hour6, "REST 6 HOURS", p.restHours == 6);
        button(Control::Hour12, "REST 12 HOURS", p.restHours == 12);
        text({464, 206, 728, 28}, "REST PREVIEW", 16);
        text({464, 250, 728, 28}, fmt::format("{}  {}", tr("CURRENT TIME"), clock(p.clock)), 16);
        if (p.restHours && p.rest.canCommit) {
            text({464, 300, 728, 28}, fmt::format("{}  {}", tr("ARRIVAL TIME"), clock(p.rest.arrival)), 16);
            text({464, 350, 728, 26}, fmt::format("{}  {}", tr("DAILY SETTLEMENTS CROSSED"), p.rest.dailyCyclesCrossed));
        } else text({464, 300, 728, 56}, p.restHours ? "REST UNAVAILABLE" : "CHOOSE REST DURATION FIRST");
        text({464, 402, 728, 30}, "REST ADVANCES TIME - DOES NOT HEAL PLAYER");
        text({464, 447, 728, 60}, "DAILY NEEDS USE EXISTING AUTHORIZED SUPPLIES", 12);
        button(Control::StartRest, "START REST");
    }
    std::string message = dormitoryFeedback_;
    if (message.empty()) {
        if (dormitoryUi_.tab == UiDormitoryTab::Rest && p.restHours)
            message = uiDormitoryUnavailable(Control::StartRest, p);
        else if (dormitoryUi_.tab == UiDormitoryTab::Residents &&
            (!p.workforce.workshopWorker || !p.workforce.medicalWorker) && !p.autoFill.canCommit)
            message = p.autoFill.message;
    }
    scroll(uiDormitoryFeedbackRect(), message, dormitoryUi_.feedbackScroll, dormitoryUi_.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_, previous);
}
