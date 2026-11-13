#include "app.h"
#include "base_emergency_power_domain.h"
#include "ui_foundation.h"
#include <array>
#include <fmt/format.h>

namespace {

enum class GeneratorControl { None, Close, Workshop, Medical, Pause, Resume };

constexpr SDL_FRect kClose{962, 146, 96, 36};
constexpr SDL_FRect kWorkshopStart{230, 396, 388, 42};
constexpr SDL_FRect kMedicalStart{662, 396, 388, 42};
constexpr SDL_FRect kPause{230, 447, 388, 42};
constexpr SDL_FRect kResume{662, 447, 388, 42};

constexpr std::array kTargets{BaseEmergencyPowerTarget::Workshop, BaseEmergencyPowerTarget::ResidentMedical};
constexpr std::array kStartButtons{kWorkshopStart, kMedicalStart};

struct GeneratorOrders {
    std::optional<AssetInstanceId> fuel;
    std::array<std::optional<BaseServiceJobId>, 2> jobs;
};

std::size_t targetIndex(BaseEmergencyPowerTarget target)
{
    return target == BaseEmergencyPowerTarget::Workshop ? 0 : 1;
}

std::string_view targetName(BaseEmergencyPowerTarget target)
{
    return target == BaseEmergencyPowerTarget::Workshop ? "WORKSHOP PRODUCTION" : "RESIDENT TREATMENT";
}

std::string_view orderName(const ProfileState &profile, BaseEmergencyPowerTarget target)
{
    if (target == BaseEmergencyPowerTarget::Workshop && profile.baseManufacturing.activeOrder &&
        !profile.baseManufacturing.activeOrder->outputReady)
        return publishedContentRegistry().baseManufacturingRecipe(profile.baseManufacturing.activeOrder->recipeDefinitionId).displayName;
    if (target == BaseEmergencyPowerTarget::ResidentMedical && profile.residentMedical.activeTreatment)
        return baseResidentProfessionName(profile.residentMedical.activeTreatment->patientProfession);
    return "NO ACTIVE ORDER";
}

GeneratorOrders generatorOrders(const ProfileState &profile)
{
    return {profile.emergencyPower ? std::nullopt : baseEmergencyPowerFuel(profile),
        {baseEmergencyPowerJob(profile, kTargets[0]), baseEmergencyPowerJob(profile, kTargets[1])}};
}

GeneratorControl generatorControlAt(Vec2 position, const std::optional<BaseEmergencyPowerState> &batch)
{
    if (uiContains(kClose, position)) return GeneratorControl::Close;
    if (batch) {
        if (batch->activeJobId && uiContains(kPause, position)) return GeneratorControl::Pause;
        if (!batch->activeJobId && uiContains(kResume, position)) return GeneratorControl::Resume;
        return GeneratorControl::None;
    }
    if (uiContains(kWorkshopStart, position)) return GeneratorControl::Workshop;
    if (uiContains(kMedicalStart, position)) return GeneratorControl::Medical;
    return GeneratorControl::None;
}

std::optional<ProfileUiCommand> generatorCommand(GeneratorControl control, const ProfileState &profile,
    const GeneratorOrders &orders)
{
    switch (control) {
    case GeneratorControl::Workshop:
    case GeneratorControl::Medical: {
        const auto index = control == GeneratorControl::Workshop ? 0U : 1U;
        const auto command = StartBaseEmergencyPowerCommand{kTargets[index], orders.fuel.value_or(0),
            orders.jobs[index].value_or(0)};
        if (queryBaseEmergencyPower(profile, command).canCommit) return ProfileUiCommand{command};
        break;
    }
    case GeneratorControl::Pause:
        if (queryBaseEmergencyPower(profile, PauseBaseEmergencyPowerCommand{}).canCommit)
            return ProfileUiCommand{PauseBaseEmergencyPowerCommand{}};
        break;
    case GeneratorControl::Resume:
        if (profile.emergencyPower) {
            const auto command = ResumeBaseEmergencyPowerCommand{
                orders.jobs[targetIndex(profile.emergencyPower->target)].value_or(0)};
            if (queryBaseEmergencyPower(profile, command).canCommit) return ProfileUiCommand{command};
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace

void App::resetBaseGeneratorUi()
{
    generatorPress_.cancel();
    generatorFeedback_.clear();
    pendingGeneratorEvents_.clear();
}

void App::handleBaseGeneratorEvent(const UiWorkshopEvent &event)
{
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Generator ||
        pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        !windowHasInputFocus_ || baseSiegeWarningVisible() || baseDefenseResultVisible_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() || profileUiBusy()) {
        generatorPress_.cancel();
        return;
    }
    const auto &profile = gameSession_.profile();
    const auto control = generatorControlAt(event.position, profile.emergencyPower);
    const auto action = static_cast<int>(control);
    if (event.kind == UiWorkshopEventKind::Move) {
        if (action != generatorPress_.action) generatorPress_.cancel();
        return;
    }
    if (event.kind == UiWorkshopEventKind::Wheel) return;
    if (control == GeneratorControl::Close) {
        if (event.kind == UiWorkshopEventKind::Down)
            generatorPress_.begin(action, {});
        else if (generatorPress_.release(action, {})) {
            gameFlow_.closeBaseFacility();
            resetBaseGeneratorUi();
            uiMessage_.clear();
            input_.suppressPrimaryPointerUntilRelease();
        }
        return;
    }

    const auto orders = generatorOrders(profile);
    const auto command = generatorCommand(control, profile, orders);
    const auto &batch = profile.emergencyPower;
    const auto job = batch ? orders.jobs[targetIndex(batch->target)] :
        orders.jobs[control == GeneratorControl::Medical ? 1 : 0];
    // Re-evaluate the exact fuel and order shown when the pointer was pressed.
    const auto context = fmt::format("{}:{}:{}:{}", profile.profileId, orders.fuel.value_or(0),
        job.value_or(0), batch ? targetIndex(batch->target) : 0);
    if (event.kind == UiWorkshopEventKind::Down) {
        generatorPress_.begin(command ? action : 0, context);
        return;
    }
    if (generatorPress_.release(action, context) && command) {
        submitProfileUi<BaseEmergencyPowerReceipt>(*command, nextProfileTransactionId("base-generator"),
            [this](const BaseEmergencyPowerReceipt &receipt) {
                generatorFeedback_ = receipt.message;
                uiMessage_ = generatorFeedback_;
                gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
            });
    }
}

void App::renderBaseGenerator()
{
    const auto &profile = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
    const auto text = [&](SDL_FRect bounds, std::string_view value, int size = 14, int lines = 1) {
        uiFinalText(renderer_, uiTextRenderer_, bounds, value, size, lines);
    };
    const auto button = [&](GeneratorControl control, SDL_FRect bounds, std::string_view label, bool enabled) {
        renderFoundationButton(6200 + static_cast<int>(control), bounds, label, !profileUiBusy() && enabled,
            false, false, {}, 14);
    };
    const auto minutes = [&](std::uint32_t count) {
        return fmt::format("{} {}", count, tr("GAME MINUTES"));
    };
    uiFinalPanel(renderer_, {194, 124, 892, 472});
    text({230, 149, 696, 42}, "GENERATOR ROOM", 24);
    button(GeneratorControl::Close, kClose, "CLOSE", true);
    text({230, 211, 820, 32}, fmt::format("{} {} / {}", tr("BASE THREAT"),
        gameSession_.baseThreatProjection().totalThreatUnits, kBaseSiegeThreatThreshold), 18);
    const auto threatPeriod = kBaseEmergencyPowerThreatPeriodMinutes;
    text({230, 247, 820, 28}, fmt::format(fmt::runtime(tr("+1 threat per {} running minutes · max {} per can")),
        threatPeriod, kBaseEmergencyPowerBatchMinutes / threatPeriod));

    const auto orders = generatorOrders(profile);
    if (const auto &batch = profile.emergencyPower) {
        const bool running = batch->activeJobId.has_value();
        const auto control = running ? GeneratorControl::Pause : GeneratorControl::Resume;
        const auto job = orders.jobs[targetIndex(batch->target)];
        text({230, 299, 820, 34}, fmt::format("{} · {}", tr(running ? "EMERGENCY POWER RUNNING" : "EMERGENCY POWER PAUSED"),
            tr(targetName(batch->target))), 18);
        text({230, 340, 388, 28}, fmt::format("{} {}", tr("FUEL RUN TIME LEFT"), minutes(batch->remainingMinutes)));
        text({662, 340, 388, 38}, job ? orderName(profile, batch->target) : "NO ACTIVE ORDER", 14, 2);
        button(control, running ? kPause : kResume, running ? "PAUSE GENERATOR" : "RESUME GENERATOR",
            generatorCommand(control, profile, orders).has_value());
        text({230, 396, 820, 36}, running ? "POWER DOUBLES JOB PROGRESS; PAUSING KEEPS FUEL" :
            job ? "RESUME USES EXISTING FUEL" : "START AN ELIGIBLE ORDER OF THIS TYPE", 14, 2);
    } else {
        for (std::size_t index = 0; index < kTargets.size(); ++index) {
            const auto rect = kStartButtons[index];
            const auto control = index == 0 ? GeneratorControl::Workshop : GeneratorControl::Medical;
            const auto target = kTargets[index];
            text({rect.x, 299, rect.w, 34}, targetName(target), 18);
            text({rect.x, 340, rect.w, 40}, orderName(profile, target), 14, 2);
            button(control, rect, "SPEND 1 FUEL CAN AND START", generatorCommand(control, profile, orders).has_value());
            if (!orders.jobs[index]) text({rect.x, 452, rect.w, 42}, "START AN ELIGIBLE ORDER OF THIS TYPE", 14, 2);
        }
        if (orders.fuel) {
            const auto &stored = std::get<StoredAssetLocation>(profile.assets.find(*orders.fuel)->location);
            const auto source = uiComponentStashDestination(profile, publishedContentRegistry(), stored.origin);
            text({230, 496, 820, 28}, fmt::format("{} · {} · {}", tr(source),
                minutes(kBaseEmergencyPowerBatchMinutes), tr("DOUBLE JOB PROGRESS")));
        } else {
            text({230, 496, 820, 28}, "STORE A FUEL CAN IN THE WAREHOUSE");
        }
    }
    text({230, 544, 820, 42}, profileUiBusy() ? "SAVING CHANGES" : generatorFeedback_, 14, 2);
}
