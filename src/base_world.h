#pragma once

#include <array>
#include <optional>

#include "animation.h"
#include "rect.h"
#include "vec2.h"

enum class BaseFacilityKind
{
    Storage,
    Supply,
    Allocation,
    Medical,
    Dormitory,
    RaidGate
};

struct BaseFacility
{
    BaseFacilityKind kind{BaseFacilityKind::Storage};
    Rect bounds;
    float interactionRange{56.0F};
};

struct BaseInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool sprint{};
    bool interactJustPressed{};
};

class BaseWorld
{
public:
    BaseWorld();

    [[nodiscard]] std::optional<BaseFacilityKind> update(
        const BaseInput &input,
        float deltaTime) noexcept;

    [[nodiscard]] Vec2 playerPosition() const noexcept;
    [[nodiscard]] Vec2 playerSize() const noexcept;
    [[nodiscard]] Vec2 playerFacingDirection() const noexcept;
    [[nodiscard]] bool playerIsMoving() const noexcept;
    [[nodiscard]] std::size_t playerAnimationFrame() const noexcept;
    [[nodiscard]] const std::array<BaseFacility, 6> &facilities() const noexcept;
    [[nodiscard]] std::optional<BaseFacilityKind>
    interactableFacility() const noexcept;

    void resetAtRaidGate() noexcept;
    void resetAtMedicalPoint() noexcept;

private:
    Vec2 playerPosition_{620.0F, 600.0F};
    Vec2 playerSize_{40.0F, 52.0F};
    Vec2 playerFacingDirection_{-1.0F, 0.0F};
    float playerHorizontalFacing_{-1.0F};
    bool playerIsMoving_{};
    Animator playerMovementAnimator_;
    Rect walkableBounds_{{32.0F, 24.0F}, {1216.0F, 664.0F}};
    std::array<BaseFacility, 6> facilities_;
};

[[nodiscard]] const char *baseFacilityName(BaseFacilityKind kind) noexcept;
