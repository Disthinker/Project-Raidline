#pragma once

#include <algorithm>
#include <cmath>
#include <span>

#include "shot_resolution.h"

struct HitFeedbackPresentationSnapshot
{
    HitSemantic semantic{HitSemantic::Normal};
    float remainingSeconds{};
    friend bool operator==(const HitFeedbackPresentationSnapshot &,
                           const HitFeedbackPresentationSnapshot &) = default;
};

// Presentation only: never infers head/weak-point from geometry, changes damage,
// or persists state. Owned by the shared shooting capability, not an activity UI.
class HitFeedbackPresentationState
{
public:
    void update(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds <= 0.0F) return;
        state_.remainingSeconds = std::max(0.0F, state_.remainingSeconds - seconds);
        if (state_.remainingSeconds == 0.0F) reset();
    }
    void consume(std::span<const HitResult> hits) noexcept
    {
        for (const auto &hit : hits)
        {
            if (hit.targetKind == HitTargetKind::Enemy &&
                (hit.semantic == HitSemantic::Headshot || hit.semantic == HitSemantic::WeakPoint))
                state_ = {hit.semantic, 0.18F};
        }
    }
    void reset() noexcept { state_ = {}; }
    [[nodiscard]] HitFeedbackPresentationSnapshot snapshot() const noexcept { return state_; }
private:
    HitFeedbackPresentationSnapshot state_;
};
