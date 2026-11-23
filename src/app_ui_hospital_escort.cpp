#include "app.h"

#include <algorithm>
#include <fmt/format.h>

namespace
{
constexpr SDL_Color kResident{247, 199, 111, 255};
constexpr SDL_Color kHandoff{116, 228, 190, 255};
constexpr SDL_Color kLost{213, 141, 132, 255};

const char *residentLabel(HospitalEscortState state, bool interact)
{
    switch (state) {
    case HospitalEscortState::AwaitingContact: return interact ? "F: start escort" : "Survivor pickup";
    case HospitalEscortState::Following: return interact ? "F: wait here" : "Survivor following";
    case HospitalEscortState::Waiting: return interact ? "F: follow me" : "Survivor waiting";
    case HospitalEscortState::Down: return "Survivor lost";
    case HospitalEscortState::Delivered: return "Handoff complete";
    }
    return "";
}
}

void App::renderHospitalEscortWorld(const HospitalEscortView &view)
{
    if (!view.outdoor) return;
    const bool delivered = view.state == HospitalEscortState::Delivered;
    const bool down = view.state == HospitalEscortState::Down;
    const SDL_FRect zone{view.handoff.position.x, view.handoff.position.y, view.handoff.size.x, view.handoff.size.y};
    if (!down && gameSession_.world().raidBoundsObservable({view.handoff.position, view.handoff.size})) {
        SDL_SetRenderDrawColor(renderer_, 38, 112, 89, 92);
        SDL_RenderFillRect(renderer_, &zone);
        SDL_SetRenderDrawColor(renderer_, kHandoff.r, kHandoff.g, kHandoff.b, kHandoff.a);
        SDL_RenderRect(renderer_, &zone);
        const char *prompt = delivered ? "Handoff complete" : !view.atHandoff ? "Safe handoff" :
            view.threatened ? "Clear hostiles before handoff" : "Hold F for 2 s to hand off";
        renderWorldLabel(zone.x + 6, zone.y + 6, prompt);
        if (view.atHandoff && !delivered) {
            if (const auto plan = gameSession_.ordinarySurvivorRescuePlan()) {
                const auto translated = [&](std::string_view format) {
                    return localizeUiText(uiTextRenderer_.language(), format);
                };
                renderWorldLabel(zone.x + 6, zone.y + 27, fmt::format(fmt::runtime(translated(
                    "Residents {} to {} · Beds {} · Rations {}/day")), plan->residentsBefore, plan->residentsAfter,
                    plan->bedCapacity, plan->dailyRationsAfter));
                if (plan->bedShortfallAfter > 0)
                    renderWorldLabel(zone.x + 6, zone.y + 48, fmt::format(fmt::runtime(translated(
                        "Bed shortfall {} after handoff")), plan->bedShortfallAfter));
            }
        }
        const float progress = gameSession_.world().ordinarySurvivorRescueProgress();
        if (view.atHandoff && !delivered && progress > 0) {
            const SDL_FRect track{zone.x + 6, zone.y + zone.h - 12, zone.w - 12, 6};
            const SDL_FRect fill{track.x, track.y, track.w * std::clamp(progress, 0.0F, 1.0F), track.h};
            SDL_SetRenderDrawColor(renderer_, 26, 52, 40, 232);
            SDL_RenderFillRect(renderer_, &track);
            SDL_SetRenderDrawColor(renderer_, kHandoff.r, kHandoff.g, kHandoff.b, kHandoff.a);
            SDL_RenderFillRect(renderer_, &fill);
        }
    }
    if (!view.visible || delivered) return;
    const auto [x, y] = view.position;
    if (down) {
        SDL_SetRenderDrawColor(renderer_, kLost.r, kLost.g, kLost.b, kLost.a);
        SDL_RenderLine(renderer_, x - 7, y - 7, x + 7, y + 7);
        SDL_RenderLine(renderer_, x + 7, y - 7, x - 7, y + 7);
    } else {
        const SDL_FRect head{x - 5, y - 13, 10, 9};
        const SDL_FRect coat{x - 8, y - 3, 16, 17};
        SDL_SetRenderDrawColor(renderer_, kResident.r, kResident.g, kResident.b, kResident.a);
        SDL_RenderFillRect(renderer_, &head);
        SDL_SetRenderDrawColor(renderer_, 129, 96, 59, 255);
        SDL_RenderFillRect(renderer_, &coat);
        SDL_SetRenderDrawColor(renderer_, kResident.r, kResident.g, kResident.b, kResident.a);
        SDL_RenderRect(renderer_, &coat);
    }
    if (!view.atHandoff || down)
        renderWorldLabel(x + 10, y - 23, residentLabel(view.state, view.inInteractionRange));
}

void App::renderHospitalEscortMap(const HospitalEscortView &view, SDL_FRect chart, Vec2 worldSize)
{
    const auto mark = [&](Vec2 location, const char *label, SDL_Color color, bool resident) {
        const float x = chart.x + location.x * chart.w / worldSize.x;
        const float y = chart.y + location.y * chart.h / worldSize.y;
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        if (resident) {
            const std::array<SDL_FPoint, 5> diamond{{{x, y-6}, {x+6, y}, {x, y+6}, {x-6, y}, {x, y-6}}};
            SDL_RenderLines(renderer_, diamond.data(), static_cast<int>(diamond.size()));
        } else {
            const SDL_FRect square{x - 6, y - 6, 12, 12};
            SDL_RenderRect(renderer_, &square);
        }
        const auto translated = localizeUiText(uiTextRenderer_.language(), label);
        const float width = std::min(chart.w, uiTextRenderer_.measure(translated, 12) + 10.0F);
        const SDL_FRect plate{std::clamp(x + 9.0F, chart.x, chart.x + chart.w - width),
            std::clamp(y + (resident ? -21.0F : 5.0F), chart.y, chart.y + chart.h - 19.0F), width, 19};
        SDL_SetRenderDrawColor(renderer_, 25, 32, 32, 245);
        SDL_RenderFillRect(renderer_, &plate);
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        uiTextRenderer_.renderBox(renderer_, {plate.x+5, plate.y, plate.w-6, plate.h}, translated, {12, UiTextAlign::Left, 1});
    };
    const bool down = view.state == HospitalEscortState::Down;
    const bool delivered = view.state == HospitalEscortState::Delivered;
    const auto &zone = view.handoff;
    mark({zone.position.x + zone.size.x * .5F, zone.position.y + zone.size.y * .5F},
        down ? "Handoff failed" : delivered ? "Handoff complete" : "Safe handoff", down ? kLost : kHandoff, false);
    // The briefed pickup and an explicitly ordered waiting spot are known.
    // Following or fallen residents only appear at a currently visible position.
    if (!delivered && (view.visible || view.state == HospitalEscortState::AwaitingContact ||
        view.state == HospitalEscortState::Waiting))
        mark(view.position, residentLabel(view.state, false), down ? kLost : kResident, true);
}
