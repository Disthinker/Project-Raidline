#include "authored_weapon_pose.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    // Source project IDs, indexed first by authored facing (right, left).
    using ClipId = std::string_view;
    constexpr ClipId bare[2][4]{
        {"99aed59d-4476-45b2-b11b-ae12141c30a4", "200f670c-6978-418a-a960-c9e7aaed6b6e",
         "f4f7f723-5450-4fd7-bb56-acbbc9ac8442", "18efbc77-1523-406e-9eb8-3c0d997b7b38"},
        {"4df74256-8df6-438b-95ae-4c613715675e", "99c461d8-9c31-4509-8cf9-d6cd1ac3b431",
         "27d93b04-a41c-46c6-bae4-297dac422e55", "32c0cd51-49ff-41fe-9f05-4db18da618ab"},
    };
    constexpr ClipId idle[2][4]{
        {"modv2-idle-01", "modv2-idle-02", "modv2-idle-03", "modv2-idle-04"},
        {"modv2-idle-05", "modv2-idle-06", "modv2-idle-07", "modv2-idle-08"},
    };
    constexpr ClipId held[2][4]{
        {"modv2-held-walk-right", "modv2-held-run-right", "modv2-held-start-01", "modv2-held-start-02"},
        {"modv2-held-walk-left", "modv2-held-run-left", "modv2-held-start-11", "modv2-held-start-12"},
    };
    constexpr ClipId start[2][8]{
        {"modv2-held-start-03", "modv2-held-start-04", "modv2-held-start-05", "modv2-held-start-06",
         "modv2-held-start-07", "modv2-held-start-08", "modv2-held-start-09", "modv2-held-start-10"},
        {"modv2-held-start-13", "modv2-held-start-14", "modv2-held-start-15", "modv2-held-start-16",
         "modv2-held-start-17", "modv2-held-start-18", "modv2-held-start-19", "modv2-held-start-20"},
    };
    constexpr ClipId decel[2][6]{
        {"modv2-held-decel-01", "modv2-held-decel-02", "modv2-held-decel-03", "modv2-held-decel-04",
         "modv2-held-decel-05", "modv2-held-decel-06"},
        {"modv2-held-decel-07", "modv2-held-decel-08", "modv2-held-decel-09", "modv2-held-decel-10",
         "modv2-held-decel-11", "modv2-held-decel-12"},
    };
    constexpr ClipId draw[2][4]{
        {"modv2-draw-large-01", "modv2-draw-to-aim-01", "modv2-draw-small-01", "modv2-draw-to-aim-03"},
        {"modv2-draw-large-02", "modv2-draw-to-aim-02", "modv2-draw-small-02", "modv2-draw-to-aim-04"},
    };
    constexpr ClipId aim[2][4]{
        {"modv2-aim-01", "modv2-aim-02", "modv2-aim-05", "modv2-aim-06"},
        {"modv2-aim-03", "modv2-aim-04", "modv2-aim-07", "modv2-aim-08"},
    };

    constexpr std::array idleEnds{300, 520, 840, 1080};
    constexpr std::array largeDrawEnds{35, 75, 130, 175, 215, 260, 315};
    constexpr std::array smallDrawEnds{35, 80, 125, 165, 210};

    template<std::size_t Size>
    std::size_t frameAt(const std::array<int, Size> &ends, double milliseconds) noexcept
    {
        const auto i = std::upper_bound(ends.begin(), ends.end(), milliseconds + 0.00005);
        return std::min(static_cast<std::size_t>(i - ends.begin()), Size - 1);
    }

    ClipId bodyClip(PlayerMotionFrame motion, AuthoredGunKind gun) noexcept
    {
        const bool left = motion.left;
        const bool armed = gun != AuthoredGunKind::Unarmed;
        switch (motion.clip)
        {
        case PlayerMotionClip::Idle: return idle[left][static_cast<unsigned>(gun)];
        case PlayerMotionClip::Walk: return armed ? held[left][0] : bare[left][0];
        case PlayerMotionClip::Run: return armed ? held[left][1] : bare[left][1];
        case PlayerMotionClip::StandToWalk: return armed ? held[left][2] : bare[left][0];
        case PlayerMotionClip::StandToRun: return armed ? held[left][3] : bare[left][2];
        case PlayerMotionClip::WalkToRunNear:
        case PlayerMotionClip::WalkToRunFar:
        {
            // Unarmed frames use the source ID plus the baked entry variant in motion.
            const auto walkFrame = (motion.clip == PlayerMotionClip::WalkToRunFar ? 4 : 0) + motion.entryPhase % 4;
            return armed ? start[left][walkFrame] : bare[left][3];
        }
        case PlayerMotionClip::RunToWalk: return armed ? decel[left][motion.entryPhase % 6] : bare[left][0];
        }
        return {};
    }

    float fraction(float value) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
    }
}

void AuthoredWeaponPoseClock::update(const AuthoredWeaponPoseInput &input, float seconds) noexcept
{
    const float dt = std::isfinite(seconds) && seconds > 0.0F ? seconds : 0.0F;
    const bool drawing = input.target.id != 0;
    const auto binding = drawing ? input.target : input.active;
    const bool previouslyDrawing = frame_.drawing;
    const auto previousId = frame_.weaponId;
    const float previousHeight = frame_.aimHeight;

    if (input.motion.clip == PlayerMotionClip::Idle)
    {
        idleMs_ = std::fmod(idleMs_ + static_cast<double>(dt) * 1000.0, idleEnds.back());
        if (idleMs_ + 0.00005 >= idleEnds.back()) idleMs_ = 0.0;
    }
    else
        idleMs_ = 0.0;

    frame_.gun = binding.kind;
    frame_.weaponId = binding.id;
    frame_.motion = input.motion;
    frame_.aimDirection = input.aimDirection;
    frame_.bodyClip = bodyClip(input.motion, binding.kind);
    frame_.bodyIndex = input.motion.clip == PlayerMotionClip::Idle ? frameAt(idleEnds, idleMs_) : input.motion.index;
    frame_.drawing = drawing;
    frame_.drawProgress = drawing ? fraction(input.switchProgress) : 0.0F;
    frame_.upperClip = {};
    frame_.upperIndex = 0;
    if (binding.kind == AuthoredGunKind::Unarmed)
    {
        frame_.aimHeight = 0.0F;
        frame_.aimingUp = false;
        bridgingAim_ = false;
        return;
    }

    const bool small = binding.kind == AuthoredGunKind::Pistol;
    const bool left = input.motion.left;
    const float ads = fraction(input.adsProgress);
    if (drawing)
    {
        // Real optics temporarily close during a switch. The held, eligible intent
        // selects the direct draw and can reverse smoothly on any authored frame.
        const float step = dt / 0.1F;
        frame_.aimHeight = input.aimRequested ? std::min(1.0F, previousHeight + step)
                                              : std::max(0.0F, previousHeight - step);
        frame_.aimingUp = input.aimRequested;
        frame_.upperClip = draw[left][(small ? 2 : 0) + (input.aimRequested ? 1 : 0)];
        frame_.upperIndex = small ? frameAt(smallDrawEnds, frame_.drawProgress * smallDrawEnds.back())
                                  : frameAt(largeDrawEnds, frame_.drawProgress * largeDrawEnds.back());
        bridgingAim_ = false;
        return;
    }

    // Bridge only the same instance's completed aimed draw while its real optic
    // reopens. Cancellation or a different equipment result follows the active gun.
    if (previouslyDrawing || previousId != binding.id)
        bridgingAim_ = previouslyDrawing && previousId == binding.id && previousHeight > ads;
    if (bridgingAim_)
    {
        const float retained = input.aimRequested ? previousHeight : std::max(0.0F, previousHeight - dt / 0.1F);
        frame_.aimHeight = std::max(retained, ads);
        bridgingAim_ = retained > ads;
    }
    else
        frame_.aimHeight = ads;
    frame_.aimingUp = frame_.aimHeight == previousHeight ? input.aimRequested : frame_.aimHeight > previousHeight;
    frame_.upperClip = aim[left][(small ? 2 : 0) + (frame_.aimingUp ? 0 : 1)];
    const float progress = frame_.aimingUp ? frame_.aimHeight : 1.0F - frame_.aimHeight;
    frame_.upperIndex = std::min(static_cast<std::size_t>(progress * 4.0F), std::size_t{3});
}
