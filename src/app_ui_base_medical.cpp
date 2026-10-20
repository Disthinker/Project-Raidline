#include "app.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace {
using Control = UiBaseMedicalControl;
using Tab = UiBaseMedicalTab;
using Kind = UiBaseMedicalEventKind;
}

void App::resetBaseMedicalUi() {
    medicalUi_ = {}; pendingMedicalEvents_.clear(); medicalProjection_ = {};
    medicalProjectionRevision_.reset(); medicalFeedback_.clear();
}
void App::refreshBaseMedicalUi(bool force) {
    const auto &p = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    if (medicalProjectionRevision_ && medicalProjection_.profileId != p.profileId) resetBaseMedicalUi();
    if (medicalProjectionRevision_ && medicalProjectionLanguage_ != language) {
        medicalUi_.press.reset(); medicalUi_.detailScroll = medicalUi_.feedbackScroll = 0;
    }
    if (!force && medicalProjectionRevision_ && *medicalProjectionRevision_ == p.revision &&
        medicalProjection_.profileId == p.profileId && medicalProjection_.worldMinute == p.worldClock.elapsedWorldMinutes &&
        medicalProjection_.health == p.currentHealth && medicalProjection_.medical.bleeding == p.medicalStatus.bleeding &&
        medicalProjectionLanguage_ == language) {
        // Updating a transient medicine timer never rescans resident supplies.
        medicalProjection_.medical = p.medicalStatus;
        return;
    }
    medicalProjection_ = projectUiBaseMedical(p, publishedContentRegistry(), language);
    medicalProjectionRevision_ = p.revision; medicalProjectionLanguage_ = language;
    ++medicalProjectionGeneration_;
}
void App::handleBaseMedicalEvent(const UiBaseMedicalEvent &event) {
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Medical ||
        inventoryOverlayState_.isOpen() || pauseMenu_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        medicalUi_.press.reset(); return;
    }
    refreshBaseMedicalUi(event.kind == Kind::Down || event.kind == Kind::Up);
    if (event.kind == Kind::Wheel) {
        medicalUi_.press.reset();
        if (uiContains(uiBaseMedicalFeedbackRect(), event.position))
            medicalUi_.feedbackScroll = std::clamp(medicalUi_.feedbackScroll + event.delta * 2, 0, medicalUi_.feedbackMaximum);
        else if (medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)] && uiContains(uiBaseMedicalDetailsRect(), event.position))
            medicalUi_.detailScroll = std::clamp(medicalUi_.detailScroll + event.delta * 2, 0, medicalUi_.detailMaximum);
        return;
    }
    const auto hit = hitUiBaseMedical(event.position, medicalUi_);
    if (event.kind == Kind::Move) {
        if (medicalUi_.press && hit != medicalUi_.press->control) medicalUi_.press.reset();
        return;
    }
    const auto language = uiTextRenderer_.language();
    if (event.kind == Kind::Down) {
        medicalUi_.press.reset();
        if (hit) medicalUi_.press = UiBaseMedicalPress{*hit, medicalUi_.tab, language,
            uiBaseMedicalActionKey(*hit, medicalUi_.tab, medicalProjection_)};
        return;
    }
    const auto press = medicalUi_.press; medicalUi_.press.reset();
    if (!press || !hit || *hit != press->control || press->tab != medicalUi_.tab || press->language != language) return;
    const auto feedback = [&](std::string value) {
        medicalFeedback_ = std::move(value); uiMessage_ = medicalFeedback_;
        medicalUi_.feedbackScroll = 0;
    };
    if (press->action != uiBaseMedicalActionKey(*hit, medicalUi_.tab, medicalProjection_)) {
        feedback("TREATMENT PLAN CHANGED - CHECK AND CLICK AGAIN"); return;
    }
    switch (*hit) {
    case Control::Close:
        gameFlow_.closeBaseFacility(); resetBaseMedicalUi(); uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease(); return;
    case Control::Personal: case Control::Residents:
        medicalUi_.tab = *hit == Control::Personal ? Tab::Personal : Tab::Residents;
        medicalUi_.detailScroll = medicalUi_.feedbackScroll = 0; medicalFeedback_.clear(); uiMessage_.clear(); return;
    case Control::Details:
        medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)] = !medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)];
        medicalUi_.detailScroll = 0; return;
    case Control::Facilities:
        medicalUi_.facilities = !medicalUi_.facilities; return;
    default: break;
    }
    const auto &p = medicalProjection_;
    if (!uiBaseMedicalEnabled(*hit, medicalUi_.tab, p)) {
        feedback(*hit == Control::Staffing ? p.staffing.message : *hit == Control::Upgrade ?
            (p.upgradeId ? p.upgrade.message : "NO MEDICAL UPGRADE PUBLISHED") :
            medicalUi_.tab == Tab::Personal ? p.player.message : p.resident.message);
        return;
    }
    bool succeeded = false;
    if (*hit == Control::Main && medicalUi_.tab == Tab::Personal) {
        const auto receipt = gameSession_.executeBasePaidMedicalService(nextProfileTransactionId("base-paid-medical"));
        succeeded = receipt.succeeded;
        feedback(succeeded ? fmt::format("PLAYER FULLY TREATED | PAID {}", receipt.currencyPaid) : receipt.message);
    } else if (*hit == Control::Main) {
        const auto receipt = gameSession_.executeStartResidentTreatment(nextProfileTransactionId("resident-treatment"));
        succeeded = receipt.succeeded;
        feedback(succeeded ? "RESIDENT TREATMENT STARTED - MATERIALS DEDUCTED ONCE" : receipt.message);
    } else if (*hit == Control::Staffing) {
        const bool assigned = p.workforce.medicalWorker.has_value();
        const auto receipt = assigned ? gameSession_.executeClearBaseWorker(BaseFacilityStaffingKind::Medical, nextProfileTransactionId("clear-medical-worker"))
            : gameSession_.executeAssignBestBaseWorker(BaseFacilityStaffingKind::Medical, nextProfileTransactionId("assign-medical-worker"));
        succeeded = receipt.succeeded;
        feedback(succeeded ? assigned ? "MEDICAL WORKER CLEARED" : "MEDICAL WORKER ASSIGNED" : receipt.message);
    } else if (*hit == Control::Upgrade && p.upgradeId) {
        const bool cancel = p.cancelUpgrade;
        const auto receipt = cancel ? gameSession_.executeCancelBaseConstruction(*p.upgradeId, nextProfileTransactionId("cancel-medical-upgrade"))
            : gameSession_.executeStartBaseConstruction(*p.upgradeId, nextProfileTransactionId("start-medical-upgrade"));
        succeeded = receipt.succeeded;
        feedback(succeeded ? cancel ? "MEDICAL UPGRADE CANCELLED" : "MEDICAL UPGRADE STARTED" : receipt.message);
    }
    gameAudio_.play(succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
    refreshBaseMedicalUi(true);
}

void App::renderBaseMedicalService() {
    refreshBaseMedicalUi();
    const auto &p = medicalProjection_;
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view s) { return localizeUiText(language, s); };
    const bool personal = medicalUi_.tab == Tab::Personal;
    const bool detailed = medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)];
    const auto fill = [&](SDL_FRect r, SDL_Color c) { SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a); SDL_RenderFillRect(renderer_, &r); };
    const auto text = [&](SDL_FRect r, std::string_view s, int size = 14, UiTextAlign align = UiTextAlign::Left, SDL_Color color = SDL_Color{222, 231, 226, 255}) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        uiTextRenderer_.renderBox(renderer_, r, s, {size, align, 0});
    };
    const auto button = [&](Control c, std::string_view label, bool selected = false) {
        renderFoundationButton(5700 + static_cast<int>(c), uiBaseMedicalControlRect(c), label,
            uiBaseMedicalEnabled(c, medicalUi_.tab, p), c == Control::Upgrade && p.cancelUpgrade, selected, {}, 14);
    };
    // All scrolling is clipped and wrapped before drawing. No ellipsis hides a
    // cost, quantity or rejection; the complete text stays reachable by wheel.
    const auto scrollText = [&](SDL_FRect r, std::string_view s, int size, int &offset, int &maximum) {
        const auto lines = wrapUiText(tr(s), r.w - 12, 0, [&](auto value) { return uiTextRenderer_.measure(value, size); });
        const float lineHeight = size * 1.4F;
        const int visible = std::max(1, static_cast<int>(r.h / lineHeight));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int i = 0; i < visible && offset + i < static_cast<int>(lines.size()); ++i)
            text({r.x, r.y + i * lineHeight, r.w - 12, lineHeight + 0.1F}, lines[offset + i].text, size);
        if (maximum) {
            const SDL_FRect bar{r.x + r.w - 4, r.y, 3, r.h}; fill(bar, {51, 69, 69, 255});
            fill({bar.x, bar.y + (bar.h - 12) * offset / maximum, 3, 12}, {113, 191, 179, 255});
        }
    };
    SDL_BlendMode previousBlend{}; SDL_GetRenderDrawBlendMode(renderer_, &previousBlend);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill({40, 60, 1200, 620}, {21, 29, 31, 252});
    fill({64, 184, 360, 354}, {27, 38, 40, 255});
    fill({444, 184, 772, 354}, {27, 38, 40, 255});
    text({64, 80, 600, 34}, "MEDICAL SERVICE", 22);
    text({800, 84, 268, 26}, fmt::format("{} {}", tr("CURRENCY"), p.currency), 16, UiTextAlign::Right);
    button(Control::Close, language == UiLanguage::English ? "CLOSE" : tr("CLOSE MEDICAL"));
    button(Control::Personal, "PERSONAL TREATMENT", personal);
    button(Control::Residents, "RESIDENT CARE", !personal);

    if (personal) {
        text({80, 200, 328, 28}, "CURRENT CONDITION", 16);
        text({80, 242, 328, 48}, fmt::format("{} / 100", p.health), 30);
        const char *bleeding = p.medical.bleeding == BleedingSeverity::Heavy ? "HEAVY BLEEDING" :
            p.medical.bleeding == BleedingSeverity::Light ? "LIGHT BLEEDING" : "NO BLEEDING";
        text({80, 304, 328, 42}, bleeding);
        text({80, 356, 328, 42}, hasPain(p.medical) ? painIsSuppressed(p.medical) ? "PAIN SUPPRESSED" : "PAIN ACTIVE" : "NO INJURY PAIN");
        if (p.medical.painkillerRemainingMs)
            text({80, 410, 328, 56}, fmt::format("{} {}{}", tr("PAINKILLER LEFT"), p.medical.painkillerRemainingMs / 1000, language == UiLanguage::English ? "s" : "秒"));
        text({464, 200, 728, 28}, "TOTAL TREATMENT COST", 16);
        text({464, 236, 728, 44}, fmt::format("{}", p.player.quotedCurrency), 28);
        text({464, 278, 728, 22}, "CURRENCY ONLY - NO PERSONAL MEDICINE - IMMEDIATE", 12);
        button(Control::Details, detailed ? "HIDE COST DETAILS" : "SHOW COST DETAILS", detailed);
        if (detailed) {
            const auto details = fmt::format("{} {}\n{} {}\n\n{}", tr("HEALTH RESTORATION"), p.player.healthCost,
                tr("INJURY TREATMENT"), p.player.injuryCost, tr("Restores health and stops bleeding. Remaining painkiller effect is retained."));
            scrollText(uiBaseMedicalDetailsRect(), details, 14, medicalUi_.detailScroll, medicalUi_.detailMaximum);
        } else {
            text({464, 362, 728, 60}, "Restores health and stops bleeding. Remaining painkiller effect is retained.");
            medicalUi_.detailMaximum = 0;
        }
    } else {
        text({80, 200, 328, 28}, "RESIDENT OVERVIEW", 16);
        constexpr const char *labels[]{"RESIDENTS", "INJURED", "HEALTHY"};
        const std::uint32_t values[]{p.population.ordinaryResidents, p.population.injuredResidents, p.population.healthyResidents};
        for (int i = 0; i < 3; ++i) {
            text({80 + 112.0F * i, 236, 104, 22}, labels[i], 12);
            text({80 + 112.0F * i, 262, 104, 40}, fmt::format("{}", values[i]), 24);
        }
        text({80, 312, 328, 32}, p.population.treatmentActive ? "RESIDENT TREATMENT ACTIVE" : "NO ACTIVE TREATMENT");
        const auto worker = p.workforce.medicalWorker ? tr(baseResidentProfessionName(*p.workforce.medicalWorker)) : tr("EMPTY");
        const auto facilityLine = fmt::format("{} {}  |  {} {}", tr("CLINIC LEVEL"), p.level, tr("ASSIGNED CLINIC WORKER"), worker);
        text({80, 350, 328, 30}, facilityLine, 12);
        if (medicalUi_.facilities) {
            button(Control::Staffing, p.workforce.medicalWorker ? "CLEAR MEDICAL WORKER" : "ASSIGN BEST WORKER");
            button(Control::Upgrade, p.cancelUpgrade ? "CANCEL MEDICAL UPGRADE" : p.upgradeComplete ? "MEDICAL LEVEL 2 COMPLETE" : "UPGRADE MEDICAL TO LEVEL 2");
            std::string reasons;
            if (!p.staffing.canCommit) reasons = tr(p.staffing.message);
            if (!p.upgrade.canCommit && !p.upgradeComplete && p.upgradeId) {
                if (!reasons.empty()) reasons += "\n";
                reasons += tr(p.upgrade.message);
            }
            text({80, 464, 328, 70}, reasons, 12);
        } else if (p.activeTreatment || p.resident.canCommit) {
            const auto patient = p.activeTreatment ? p.activeTreatment->patientProfession : p.resident.patientProfession;
            text({80, 394, 328, 42}, fmt::format("{} {}", tr("PATIENT PROFESSION"), tr(baseResidentProfessionName(patient))));
        }
        text({464, 200, 728, 28}, p.population.treatmentActive ? "RESIDENT TREATMENT ACTIVE" : p.resident.canCommit ? "THIS TREATMENT: 1 RESIDENT" : "TREATMENT PLAN NOT READY", 16);
        const auto duration = p.population.treatmentActive ? p.population.remainingMinutes : p.resident.durationMinutes;
        text({464, 240, 728, 28}, fmt::format("{} {}", tr(p.population.treatmentActive ? "WORLD TIME REMAINING" : "TREATMENT DURATION"),
            p.population.treatmentActive || p.resident.canCommit ? uiBaseMedicalDuration(duration, language) : tr("AVAILABLE WITH VALID PLAN")), 16);
        text({464, 278, 728, 22}, "AUTHORIZED MEDICAL ITEMS ONLY - DEDUCTED ON START", 12);
        button(Control::Details, detailed ? "HIDE MATERIAL DETAILS" : "SHOW MATERIAL DETAILS", detailed);
        if (detailed) {
            std::string materials;
            if (p.population.treatmentActive) materials = tr("MATERIALS ALREADY DEDUCTED - NO SECOND CHARGE");
            else if (p.supplies.empty()) materials = tr("NO EXECUTABLE MATERIAL PLAN");
            else {
                if (!p.resident.canCommit) materials = tr("PARTIAL MATERIALS - CANNOT START") + "\n";
                for (const auto &row : p.supplies) materials += fmt::format("{}  x{}\n", row.name, row.quantity);
            }
            scrollText(uiBaseMedicalDetailsRect(), materials, 14, medicalUi_.detailScroll, medicalUi_.detailMaximum);
        } else {
            text({464, 362, 728, 60}, p.population.treatmentActive ? "MATERIALS ALREADY DEDUCTED - NO SECOND CHARGE" : "EXPAND TO CHECK ALL MATERIALS");
            medicalUi_.detailMaximum = 0;
        }
        if (p.activeUpgrade) {
            const auto left = p.activeUpgrade->completionWorldMinute > p.worldMinute ? p.activeUpgrade->completionWorldMinute - p.worldMinute : 0;
            text({464, 508, 728, 22}, fmt::format("{} {}", tr("CLINIC UPGRADE IN PROGRESS"), uiBaseMedicalDuration(left, language)), 12);
        }
        button(Control::Facilities, medicalUi_.facilities ? "HIDE FACILITY AND STAFF" : "FACILITY AND STAFF", medicalUi_.facilities);
    }
    const auto mainLabel = personal ? (p.player.quotedCurrency ? fmt::format("{} · {}", tr("PAY & TREAT"), p.player.quotedCurrency) : tr("PAY & TREAT"))
        : tr(p.population.treatmentActive ? "RESIDENT TREATMENT ACTIVE" : "START RESIDENT TREATMENT");
    button(Control::Main, mainLabel);
    const std::string message = !medicalFeedback_.empty() ? medicalFeedback_ : personal ?
        (p.player.canCommit ? "" : p.player.message) :
        p.population.treatmentActive ? "TREATMENT CONTINUES USING WORLD TIME" :
        p.resident.canCommit ? "" : p.resident.message;
    if (!message.empty()) fill({64, 606, 1152, 60}, {31, 39, 40, 255});
    scrollText(uiBaseMedicalFeedbackRect(), message, 14, medicalUi_.feedbackScroll, medicalUi_.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_, previousBlend);
}
