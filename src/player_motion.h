#pragma once

#include <cstddef>
#include <cstdint>

enum class PlayerMotionClip { Idle, Walk, Run, StandToRun, WalkToRunNear, WalkToRunFar };

struct PlayerMotionFrame
{
    PlayerMotionClip clip{PlayerMotionClip::Idle};
    std::size_t index{};
    bool left{true};
    std::uint8_t entryPhase{}; // Originating walk frame within its four-frame support phase.
    bool operator==(const PlayerMotionFrame &) const = default;
};

// Presentation only. World and scope passes can sample the same frame repeatedly.
class PlayerMotion
{
public:
    void move(bool sprint, float seconds) noexcept;
    void stop() noexcept;
    void face(float horizontal) noexcept;
    [[nodiscard]] PlayerMotionFrame frame() const noexcept { return frame_; }

private:
    PlayerMotionFrame frame_;
    double elapsedMs_{};
};
