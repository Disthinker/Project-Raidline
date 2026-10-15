#pragma once

#include <algorithm>
#include <cmath>
#include <span>

#include "shot_resolution.h"

struct HitFeedbackSource
{
    CombatTargetId targetId{};
    Vec2 position{};
    bool targetKilled{};
    friend bool operator==(const HitFeedbackSource &a,
                           const HitFeedbackSource &b)
    {
        return a.targetId == b.targetId && a.position.x == b.position.x &&
               a.position.y == b.position.y && a.targetKilled == b.targetKilled;
    }
};

struct HitFeedbackPresentationSnapshot
{
    HitSemantic semantic{HitSemantic::Normal};
    float remainingSeconds{};
    float armorFeedbackSeconds{};
    bool armorBroken{};
    std::optional<HitFeedbackSource> semanticSource, armorSource;
    friend bool operator==(const HitFeedbackPresentationSnapshot &,
                           const HitFeedbackPresentationSnapshot &) = default;
};

// Presentation only: never infers head/weak-point from geometry, changes
// damage, or persists state. Owned by the shared shooting capability, not an
// activity UI.
class HitFeedbackPresentationState
{
  public:
    void update(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds <= 0.0F)
            return;
        state_.remainingSeconds =
            std::max(0.0F, state_.remainingSeconds - seconds);
        if (state_.remainingSeconds == 0.0F)
        {
            state_.semantic = HitSemantic::Normal;
            state_.semanticSource.reset();
        }
        state_.armorFeedbackSeconds =
            std::max(0.0F, state_.armorFeedbackSeconds - seconds);
        if (state_.armorFeedbackSeconds == 0.0F)
        {
            state_.armorBroken = false;
            state_.armorSource.reset();
        }
    }
    void consume(std::span<const HitResult> hits) noexcept
    {
        for (const auto &hit : hits)
        {
            if (hit.targetKind == HitTargetKind::Enemy &&
                (hit.semantic == HitSemantic::Headshot ||
                 hit.semantic == HitSemantic::WeakPoint))
            {
                state_.semantic = hit.semantic;
                state_.remainingSeconds = 0.18F;
                state_.semanticSource = HitFeedbackSource{
                    hit.targetId, hit.position, hit.targetKilled};
            }
            if (hit.targetKind == HitTargetKind::Enemy &&
                (hit.armorReducedDamage || hit.armorBroken))
            {
                state_.armorBroken =
                    hit.armorBroken ||
                    (state_.armorFeedbackSeconds > 0 && state_.armorBroken &&
                     state_.armorSource &&
                     state_.armorSource->targetId == hit.targetId);
                state_.armorFeedbackSeconds = 0.8F;
                state_.armorSource = HitFeedbackSource{
                    hit.targetId, hit.position, hit.targetKilled};
            }
        }
    }
    void reset() noexcept { state_ = {}; }
    [[nodiscard]] HitFeedbackPresentationSnapshot snapshot() const noexcept
    {
        return state_;
    }

  private:
    HitFeedbackPresentationSnapshot state_;
};
