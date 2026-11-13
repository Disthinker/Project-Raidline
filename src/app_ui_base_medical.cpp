#include "app.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace {
using Control = UiBaseMedicalControl;
using Tab = UiBaseMedicalTab;
using Kind = UiBaseMedicalEventKind;

const UiBaseMedicalAsset *warehouseRow(
    const UiBaseMedicalProjection &projection, const UiBaseMedicalState &state, std::size_t row)
{
    const auto index = state.warehousePage * kUiBaseMedicalWarehouseRows + row;
    return index < projection.warehouseMedicine.size() ? &projection.warehouseMedicine[index] : nullptr;
}

std::string medicalActionKey(const UiBaseMedicalHit &hit, const UiBaseMedicalState &state,
    const UiBaseMedicalProjection &projection, const UiBaseMedicalTransferProjection &transfer)
{
    std::string key = uiBaseMedicalActionKey(hit.control, state.tab, projection);
    switch (hit.control)
    {
    case Control::WarehouseRow:
        if (const auto *asset = warehouseRow(projection, state, hit.index))
            key += fmt::format(":{}:{}:{}:{}", asset->id, asset->location.origin.x, asset->location.origin.y, asset->charges);
        break;
    case Control::ReserveCell:
        if (const auto *asset = uiBaseMedicalReserveAssetAt(projection, uiBaseMedicalReserveCell(hit.index)))
            key += fmt::format(":{}:{}:{}:{}", asset->id, asset->location.origin.x, asset->location.origin.y, asset->charges);
        if (state.selectedMedicine) key += fmt::format(":selected:{}", *state.selectedMedicine);
        break;
    case Control::ReserveTransfer:
        if (const auto &command = transfer.command)
            key += fmt::format(":{}:{}:{}:{}:{}:{}:{}", command->assetId,
                static_cast<int>(command->source.container.kind), command->source.origin.x, command->source.origin.y,
                static_cast<int>(command->destination.container.kind), command->destination.origin.x, command->destination.origin.y);
        if (const auto *asset = uiBaseMedicalAsset(projection, state.selectedMedicine))
            key += fmt::format(":{}", asset->charges);
        key += fmt::format(":{}", transfer.canCommit);
        break;
    case Control::AutoAdmission:
    case Control::MoveEarlier:
    case Control::MoveLater:
        key += fmt::format(":{}", projection.reservePolicy.automaticAdmission);
        for (const auto profession : projection.reservePolicy.professionPriority)
            key += fmt::format(":{}", static_cast<int>(profession));
        break;
    default:
        break;
    }
    return key;
}

std::string medicineUses(std::uint64_t charges, UiLanguage language)
{
    if (language == UiLanguage::English) return fmt::format("{} {}", charges, charges == 1 ? "use" : "uses");
    return fmt::format("{} {}", charges, localizeUiText(language, "MEDICINE RESERVE USES"));
}

std::string warehouseLocation(GridPosition position, UiLanguage language)
{
    const int page = (position.y / 8) * 3 + position.x / 8 + 1;
    return fmt::format(fmt::runtime(localizeUiText(language, "WAREHOUSE PAGE {} · ({}, {})")),
        page, position.x % 8 + 1, position.y % 8 + 1);
}
}

void App::resetBaseMedicalUi() {
    medicalUi_ = {}; pendingMedicalEvents_.clear(); medicalProjection_ = {};
    medicalProjectionRevision_.reset(); medicalTransferProjection_.reset(); medicalFeedback_.clear();
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
    medicalTransferProjection_.reset();
    medicalUi_.warehousePage = std::min(medicalUi_.warehousePage,
        uiBaseMedicalWarehousePageCount(medicalProjection_) - 1);
    if (medicalUi_.selectedMedicine && !uiBaseMedicalAsset(medicalProjection_, medicalUi_.selectedMedicine)) {
        medicalUi_.selectedMedicine.reset();
        medicalUi_.reserveDestination.reset();
    }
    medicalProjectionRevision_ = p.revision; medicalProjectionLanguage_ = language;
    ++medicalProjectionGeneration_;
}
const UiBaseMedicalTransferProjection &App::medicalTransferProjection()
{
    if (!medicalTransferProjection_)
        medicalTransferProjection_ = projectUiBaseMedicalTransfer(
            gameSession_.profile(), publishedContentRegistry(), medicalUi_, medicalProjection_);
    return *medicalTransferProjection_;
}
void App::handleBaseMedicalEvent(const UiBaseMedicalEvent &event) {
    if (profileUiBusy()) { medicalUi_.press.reset(); return; }
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Medical ||
        inventoryOverlayState_.isOpen() || pauseMenu_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        medicalUi_.press.reset(); return;
    }
    refreshBaseMedicalUi(event.kind == Kind::Down || event.kind == Kind::Up);
    if (event.kind == Kind::Wheel) {
        medicalUi_.press.reset();
        if (uiContains(uiBaseMedicalFeedbackRect(), event.position))
            medicalUi_.feedbackScroll = std::clamp(medicalUi_.feedbackScroll + event.delta * 2, 0, medicalUi_.feedbackMaximum);
        else if ((medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)] ||
                  (medicalUi_.tab == Tab::Residents && medicalUi_.facilities)) &&
            uiContains(uiBaseMedicalDetailsRect(medicalUi_.tab), event.position))
            medicalUi_.detailScroll = std::clamp(medicalUi_.detailScroll + event.delta * 2, 0, medicalUi_.detailMaximum);
        else if (medicalUi_.tab == Tab::Residents && !medicalUi_.priorityOpen &&
            uiContains({720, 234, 472, 228}, event.position)) {
            const int page = std::clamp(static_cast<int>(medicalUi_.warehousePage) + event.delta,
                0, static_cast<int>(uiBaseMedicalWarehousePageCount(medicalProjection_) - 1));
            medicalUi_.warehousePage = static_cast<std::size_t>(page);
        }
        return;
    }
    const auto hit = hitUiBaseMedical(event.position, medicalUi_);
    if (event.kind == Kind::Move) {
        if (medicalUi_.press && hit != medicalUi_.press->hit) medicalUi_.press.reset();
        return;
    }
    const auto language = uiTextRenderer_.language();
    const auto transfer = hit && hit->control == Control::ReserveTransfer
        ? medicalTransferProjection()
        : UiBaseMedicalTransferProjection{};
    if (event.kind == Kind::Down) {
        medicalUi_.press.reset();
        if (hit) medicalUi_.press = UiBaseMedicalPress{*hit, medicalUi_.tab, language,
            medicalActionKey(*hit, medicalUi_, medicalProjection_, transfer)};
        return;
    }
    const auto press = medicalUi_.press; medicalUi_.press.reset();
    if (!press || !hit || *hit != press->hit || press->tab != medicalUi_.tab || press->language != language) return;
    const auto feedback = [this](std::string value) {
        medicalFeedback_ = std::move(value); uiMessage_ = medicalFeedback_;
        medicalUi_.feedbackScroll = 0;
    };
    if (press->action != medicalActionKey(*hit, medicalUi_, medicalProjection_, transfer)) {
        feedback("TREATMENT PLAN CHANGED - CHECK AND CLICK AGAIN"); return;
    }
    const auto control = hit->control;
    const bool startsTreatment = control == Control::Main ||
        (control == Control::AutoAdmission && !medicalProjection_.reservePolicy.automaticAdmission);
    if (startsTreatment && medicalUi_.tab == Tab::Residents &&
        (!medicalUi_.details[static_cast<std::size_t>(Tab::Residents)] || medicalUi_.facilities) &&
        medicalProjection_.resident.canCommit) {
        medicalUi_.details[static_cast<std::size_t>(Tab::Residents)] = true;
        medicalUi_.facilities = false;
        medicalUi_.detailScroll = 0;
        return;
    }
    switch (control) {
    case Control::Close:
        gameFlow_.closeBaseFacility(); resetBaseMedicalUi(); uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease(); return;
    case Control::Personal: case Control::Residents:
        medicalUi_.tab = control == Control::Personal ? Tab::Personal : Tab::Residents;
        medicalUi_.detailScroll = medicalUi_.feedbackScroll = 0; medicalFeedback_.clear(); uiMessage_.clear(); return;
    case Control::Details:
        medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)] = !medicalUi_.details[static_cast<std::size_t>(medicalUi_.tab)];
        medicalUi_.detailScroll = 0; return;
    case Control::Facilities:
        medicalUi_.facilities = !medicalUi_.facilities;
        medicalUi_.detailScroll = 0;
        return;
    case Control::Priority:
        medicalUi_.priorityOpen = !medicalUi_.priorityOpen; return;
    case Control::PreviousWarehouse:
        if (medicalUi_.warehousePage > 0) --medicalUi_.warehousePage;
        return;
    case Control::NextWarehouse:
        if (medicalUi_.warehousePage + 1 < uiBaseMedicalWarehousePageCount(medicalProjection_)) ++medicalUi_.warehousePage;
        return;
    case Control::WarehouseRow:
        if (const auto *asset = warehouseRow(medicalProjection_, medicalUi_, hit->index)) {
            medicalUi_.selectedMedicine = asset->id;
            medicalUi_.reserveDestination.reset();
            medicalTransferProjection_.reset();
            medicalFeedback_.clear();
        }
        return;
    case Control::ReserveCell: {
        const auto cell = uiBaseMedicalReserveCell(hit->index);
        if (const auto *asset = uiBaseMedicalReserveAssetAt(medicalProjection_, cell)) {
            medicalUi_.selectedMedicine = asset->id;
            medicalUi_.reserveDestination.reset();
            medicalTransferProjection_.reset();
            medicalFeedback_.clear();
        } else if (const auto *selected = uiBaseMedicalAsset(medicalProjection_, medicalUi_.selectedMedicine);
                   selected && selected->location.container == ProfileContainerId::stash()) {
            medicalUi_.reserveDestination = cell;
            medicalTransferProjection_.reset();
            medicalFeedback_.clear();
        }
        return;
    }
    default: break;
    }
    const auto &p = medicalProjection_;
    if (!uiBaseMedicalEnabled(control, medicalUi_.tab, p)) {
        feedback(control == Control::Staffing ? p.staffing.message : control == Control::Upgrade ?
            (p.upgradeId ? p.upgrade.message : "NO MEDICAL UPGRADE PUBLISHED") :
            medicalUi_.tab == Tab::Personal ? p.player.message : p.resident.message);
        return;
    }
    const auto finish=[this,feedback](bool succeeded,std::string message) {
        feedback(std::move(message));
        gameAudio_.play(succeeded?SoundEventId::UiConfirm:SoundEventId::UiDeny);
        refreshBaseMedicalUi(true);
    };
    if (control == Control::ReserveTransfer) {
        if (!transfer.canCommit || !transfer.command) {
            if (!transfer.message.empty()) feedback(transfer.message);
            return;
        }
        const auto command = *transfer.command;
        const bool depositing = command.destination.container == ProfileContainerId::medicalReserve();
        submitProfileUi<MedicalReserveReceipt>(command, nextProfileTransactionId("medical-reserve-transfer"),
            [this, finish, depositing](const auto &receipt) {
                if (receipt.succeeded) {
                    medicalUi_.selectedMedicine.reset();
                    medicalUi_.reserveDestination.reset();
                }
                finish(receipt.succeeded, receipt.succeeded ?
                    depositing ? "MEDICINE DEPOSITED" : "MEDICINE RETURNED TO WAREHOUSE" : receipt.message);
            });
    } else if (control == Control::AutoAdmission || control == Control::MoveEarlier || control == Control::MoveLater) {
        auto policy = p.reservePolicy;
        if (control == Control::AutoAdmission) policy.automaticAdmission = !policy.automaticAdmission;
        else {
            const auto index = hit->index;
            if (control == Control::MoveEarlier) {
                if (index == 0) return;
                std::swap(policy.professionPriority[index], policy.professionPriority[index - 1]);
            } else {
                if (index + 1 >= policy.professionPriority.size()) return;
                std::swap(policy.professionPriority[index], policy.professionPriority[index + 1]);
            }
        }
        submitProfileUi<MedicalReserveReceipt>(SetResidentMedicalReservePolicyCommand{policy},
            nextProfileTransactionId("medical-reserve-policy"), [finish](const auto &receipt) {
                finish(receipt.succeeded, receipt.succeeded ? "MEDICAL ADMISSION SETTINGS SAVED" : receipt.message);
            });
    } else if (control==Control::Main && medicalUi_.tab==Tab::Personal) {
        submitProfileUi<BaseMedicalServiceReceipt>(BaseMedicalServiceCommand{},nextProfileTransactionId("base-paid-medical"),
            [finish](const auto &r){finish(r.succeeded,r.succeeded?fmt::format("PLAYER FULLY TREATED | PAID {}",r.currencyPaid):r.message);});
    } else if(control==Control::Main) {
        submitProfileUi<ResidentTreatmentReceipt>(StartResidentTreatmentCommand{p.resident.patientProfession, p.resident.supplies},nextProfileTransactionId("resident-treatment"),
            [finish](const auto &r){finish(r.succeeded,r.succeeded?"RESIDENT TREATMENT STARTED - MATERIALS DEDUCTED ONCE":r.message);});
    } else if(control==Control::Staffing) {
        const bool assigned=p.workforce.medicalWorker.has_value();
        submitProfileUi<BaseWorkforceReceipt>(assigned?ProfileUiCommand{ProfileUiClearWorkerCommand{BaseFacilityStaffingKind::Medical}}:
            ProfileUiCommand{ProfileUiAssignWorkerCommand{BaseFacilityStaffingKind::Medical}},nextProfileTransactionId("medical-worker"),
            [finish,assigned](const auto &r){finish(r.succeeded,r.succeeded?(assigned?"MEDICAL WORKER CLEARED":"MEDICAL WORKER ASSIGNED"):r.message);});
    } else if(control==Control::Upgrade && p.upgradeId) {
        const bool cancel=p.cancelUpgrade;
        submitProfileUi<BaseConstructionReceipt>(cancel?ProfileUiCommand{CancelBaseConstructionCommand{*p.upgradeId}}:
            ProfileUiCommand{StartBaseConstructionCommand{*p.upgradeId}},nextProfileTransactionId("medical-upgrade"),
            [finish,cancel](const auto &r){finish(r.succeeded,r.succeeded?(cancel?"MEDICAL UPGRADE CANCELLED":"MEDICAL UPGRADE STARTED"):r.message);});
    }

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
        renderFoundationButton(5700 + static_cast<int>(c), uiBaseMedicalControlRect(c, medicalUi_.tab), label,
            !profileUiBusy() && uiBaseMedicalEnabled(c, medicalUi_.tab, p),
            c == Control::Upgrade && p.cancelUpgrade, selected, {}, 14);
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
        const auto &power = gameSession_.profile().emergencyPower;
        const bool powerTarget = p.activeTreatment && power && power->target == BaseEmergencyPowerTarget::ResidentMedical;
        const bool powerRunning = powerTarget && power->activeJobId == p.activeTreatment->jobId;
        const auto duration = p.population.treatmentActive ? p.population.remainingMinutes : p.resident.durationMinutes;
        const auto timeLabel = tr(p.population.treatmentActive ? powerRunning ? "WORK REMAINING" : "WORLD TIME REMAINING"
            : p.resident.canCommit ? "TREATMENT DURATION" : "NO ACTIVE TREATMENT");
        text({80, 308, 328, 24}, p.population.treatmentActive || p.resident.canCommit
            ? fmt::format("{} {}", timeLabel, uiBaseMedicalDuration(duration, language)) : timeLabel, 14);
        if (p.activeTreatment || p.resident.canCommit) {
            const auto patient = p.activeTreatment ? p.activeTreatment->patientProfession : p.resident.patientProfession;
            text({80, 332, 328, 24}, fmt::format("{} {}", tr("PATIENT PROFESSION"), tr(baseResidentProfessionName(patient))), 13);
        }
        const auto worker = p.workforce.medicalWorker ? tr(baseResidentProfessionName(*p.workforce.medicalWorker)) : tr("EMPTY");
        const auto facilityLine = fmt::format("{} {}  |  {} {}", tr("CLINIC LEVEL"), p.level, tr("ASSIGNED CLINIC WORKER"), worker);
        text({80, 356, 328, 22}, facilityLine, 12);
        if (medicalUi_.facilities) {
            button(Control::Staffing, p.workforce.medicalWorker ? "CLEAR MEDICAL WORKER" : "ASSIGN BEST WORKER");
            button(Control::Upgrade, p.cancelUpgrade ? "CANCEL MEDICAL UPGRADE" : p.upgradeComplete ? "MEDICAL LEVEL 2 COMPLETE" : "UPGRADE MEDICAL TO LEVEL 2");
            std::string reasons;
            if (!p.staffing.canCommit) reasons = tr(p.staffing.message);
            if (!p.upgrade.canCommit && !p.upgradeComplete && p.upgradeId) {
                if (!reasons.empty()) reasons += "\n";
                reasons += tr(p.upgrade.message);
            }
            if (p.activeUpgrade) {
                const auto left = p.activeUpgrade->completionWorldMinute > p.worldMinute
                    ? p.activeUpgrade->completionWorldMinute - p.worldMinute : 0;
                if (!reasons.empty()) reasons += "\n";
                reasons += fmt::format("{} {}", tr("CLINIC UPGRADE IN PROGRESS"), uiBaseMedicalDuration(left, language));
            }
            if (!reasons.empty())
                text({80, 458, 328, 36}, reasons, 12);
        } else {
            button(Control::Details, detailed ? "HIDE MATERIAL DETAILS" : "SHOW MATERIAL DETAILS", detailed);
            std::string materials;
            if (p.population.treatmentActive) {
                materials = tr("MATERIALS ALREADY DEDUCTED - NO SECOND CHARGE");
                if (powerTarget)
                    materials += "\n" + tr(powerRunning ? "ORDER POWERED · DOUBLE PROGRESS" : "ORDER POWER PAUSED");
            }
            else if (p.supplies.empty()) materials = tr("NO EXECUTABLE MATERIAL PLAN");
            else {
                if (!p.resident.canCommit) materials = tr("PARTIAL MATERIALS - CANNOT START") + "\n";
                else materials = tr("MEDICINE USED WHEN TREATMENT STARTS") + "\n";
                for (const auto &row : p.supplies)
                    materials += fmt::format("{}  {}\n", row.name, row.charges
                        ? medicineUses(row.charges, language) : fmt::format("×{}", row.quantity));
            }
            scrollText(uiBaseMedicalDetailsRect(Tab::Residents), detailed ? materials :
                p.population.treatmentActive ? tr("MATERIALS ALREADY DEDUCTED - NO SECOND CHARGE") :
                tr("EXPAND TO CHECK ALL MATERIALS"), 13, medicalUi_.detailScroll, medicalUi_.detailMaximum);
        }

        if (medicalUi_.priorityOpen) {
            text({464, 200, 390, 34}, "TREATMENT PRIORITY", 18);
            button(Control::AutoAdmission, p.reservePolicy.automaticAdmission ? "AUTO ADMISSION ON" : "AUTO ADMISSION OFF",
                p.reservePolicy.automaticAdmission);
            const auto &priorities = p.reservePolicy.professionPriority;
            for (std::size_t i = 0; i < priorities.size(); ++i) {
                const auto y = uiBaseMedicalPriorityButtonRect(Control::MoveEarlier, i).y;
                text({464, y, 450, 34}, fmt::format("{}  {}", i + 1, tr(baseResidentProfessionName(priorities[i]))));
                renderFoundationButton(5740 + static_cast<int>(2 * i), uiBaseMedicalPriorityButtonRect(Control::MoveEarlier, i),
                    "MOVE UP", !profileUiBusy() && i > 0, false, false, {}, 13);
                renderFoundationButton(5741 + static_cast<int>(2 * i), uiBaseMedicalPriorityButtonRect(Control::MoveLater, i),
                    "MOVE DOWN", !profileUiBusy() && i + 1 < priorities.size(), false, false, {}, 13);
            }
            text({464, 466, 728, 55}, "AUTO ADMISSION USES CABINET MEDICINE AT BASE", 13);
        } else {
            const auto grid = uiBaseMedicalReserveRect();
            const float cell = kUiBaseMedicalReserveCellSize;
            text({grid.x, 198, grid.w + 24, 28}, "PUBLIC MEDICINE CABINET", 15);
            text({720, 198, 472, 28}, "WAREHOUSE MEDICINE", 17);
            renderProfileGrid(ProfileContainerId::medicalReserve(), grid.x, grid.y, cell, "");
            const auto outline = [&](SDL_FRect r, SDL_Color color) {
                SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
                SDL_RenderRect(renderer_, &r);
            };
            const auto itemRect = [&](GridPosition cellPosition, InventoryFootprint footprint) {
                return SDL_FRect{grid.x + cellPosition.x * cell, grid.y + cellPosition.y * cell,
                    footprint.width * cell, footprint.height * cell};
            };
            outline(grid, {111, 145, 145, 255});
            for (const auto &asset : p.reserveMedicine) {
                const auto r = itemRect(asset.location.origin, asset.footprint);
                if (asset.charges) {
                    const SDL_FRect badge{r.x + 2, r.y + r.h - 19, r.w - 4, 16};
                    fill(badge, {24, 34, 38, 244});
                    text(badge, medicineUses(asset.charges, language), 10, UiTextAlign::Center);
                }
                if (medicalUi_.selectedMedicine == asset.id) outline(r, {146, 228, 201, 255});
            }
            for (std::size_t i = 0; i < kUiBaseMedicalWarehouseRows; ++i) {
                if (const auto *asset = warehouseRow(p, medicalUi_, i)) {
                    const auto r = uiBaseMedicalWarehouseRowRect(i);
                    const bool selected = medicalUi_.selectedMedicine == asset->id;
                    fill(r, selected ? SDL_Color{48, 81, 78, 255} : SDL_Color{34, 46, 48, 255});
                    if (selected) outline(r, {146, 228, 201, 255});
                    text({r.x + 8, r.y, 196, r.h}, asset->name, 14);
                    text({r.x + 210, r.y, 72, r.h}, asset->charges
                        ? medicineUses(asset->charges, language) : "×1", 12);
                    text({r.x + 286, r.y, r.w - 294, r.h}, warehouseLocation(asset->location.origin, language),
                        12, UiTextAlign::Right);
                }
            }
            if (p.warehouseMedicine.empty()) text({728, 236, 456, 30}, "NO ELIGIBLE MEDICINE IN WAREHOUSE", 14);
            const auto warehousePages = uiBaseMedicalWarehousePageCount(p);
            if (warehousePages > 1) {
                renderFoundationButton(5720, uiBaseMedicalControlRect(Control::PreviousWarehouse), "<",
                    !profileUiBusy() && medicalUi_.warehousePage > 0, false, false, {}, 14);
                renderFoundationButton(5721, uiBaseMedicalControlRect(Control::NextWarehouse), ">",
                    !profileUiBusy() && medicalUi_.warehousePage + 1 < warehousePages, false, false, {}, 14);
                text({780, 432, 352, 30}, fmt::format("{} / {}", medicalUi_.warehousePage + 1, warehousePages),
                    13, UiTextAlign::Center);
            }
            const auto &transfer = medicalTransferProjection();
            const auto *selected = uiBaseMedicalAsset(p, medicalUi_.selectedMedicine);
            const bool withdrawing = selected && selected->location.container == ProfileContainerId::medicalReserve();
            if (selected) {
                std::string line = selected->name;
                if (selected->charges) line += " · " + medicineUses(selected->charges, language);
                if (transfer.canCommit) {
                    const auto position = transfer.command->destination.origin;
                    if (withdrawing) line += " · " + warehouseLocation(position, language);
                    else {
                        const auto r = itemRect(position, selected->footprint);
                        fill(r, {100, 185, 165, 48});
                        outline(r, {146, 228, 201, 255});
                        line += " · " + fmt::format(fmt::runtime(tr("CABINET CELL ({}, {})")), position.x + 1, position.y + 1);
                    }
                }
                text({464, 468, 728, 24}, line, 13);
                if (!transfer.canCommit)
                    text({464, 494, 436, 36}, transfer.message, 12, UiTextAlign::Left, {237, 195, 152, 255});
            } else {
                text({464, 468, 728, 50}, "SELECT WAREHOUSE MEDICINE TO DEPOSIT OR CABINET MEDICINE TO WITHDRAW", 13);
            }
            if (selected)
                renderFoundationButton(5722, uiBaseMedicalControlRect(Control::ReserveTransfer),
                    withdrawing ? "RETURN TO WAREHOUSE" : "DEPOSIT IN CABINET", !profileUiBusy() && transfer.canCommit,
                    false, false, {}, 14);
        }
        button(Control::Facilities, medicalUi_.facilities ? "HIDE FACILITY AND STAFF" : "FACILITY AND STAFF", medicalUi_.facilities);
        button(Control::Priority, medicalUi_.priorityOpen ? "BACK TO MEDICINE CABINET" :
            p.reservePolicy.automaticAdmission ? "ADMISSION SETTINGS · AUTO ON" : "ADMISSION SETTINGS", medicalUi_.priorityOpen);
    }
    const auto mainLabel = personal ? (p.player.quotedCurrency ? fmt::format("{} · {}", tr("PAY & TREAT"), p.player.quotedCurrency) : tr("PAY & TREAT"))
        : tr(p.population.treatmentActive ? "RESIDENT TREATMENT ACTIVE" :
            p.resident.canCommit && (!detailed || medicalUi_.facilities) ? "REVIEW TREATMENT COST" : "START RESIDENT TREATMENT");
    renderFoundationButton(5700 + static_cast<int>(Control::Main), uiBaseMedicalControlRect(Control::Main),
        mainLabel, !profileUiBusy() && uiBaseMedicalEnabled(Control::Main, medicalUi_.tab, p), false, false, {}, 14);
    const std::string message = profileUiBusy() ? "SAVING CHANGES" : !medicalFeedback_.empty() ? medicalFeedback_ : personal ?
        (p.player.canCommit ? "" : p.player.message) :
        p.population.treatmentActive ? "TREATMENT CONTINUES USING WORLD TIME" :
        p.resident.canCommit ? "" : p.resident.message;
    if (!message.empty()) fill({64, 606, 1152, 60}, {31, 39, 40, 255});
    scrollText(uiBaseMedicalFeedbackRect(), message, 14, medicalUi_.feedbackScroll, medicalUi_.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_, previousBlend);
}
