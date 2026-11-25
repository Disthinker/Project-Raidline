#pragma once

#include "player_motion.h"
#include "vec2.h"

#include <cstdint>
#include <string_view>

enum class AuthoredGunKind { Unarmed, Rifle, Compact, Pistol };

struct AuthoredWeaponBinding
{
    std::uint64_t id{};
    AuthoredGunKind kind{AuthoredGunKind::Unarmed};
};

struct AuthoredWeaponPoseInput
{
    PlayerMotionFrame motion{};
    Vec2 aimDirection{-1.0F, 0.0F};
    AuthoredWeaponBinding active{}, target{};
    float switchProgress{}, adsProgress{};
    bool aimRequested{};
};

struct AuthoredWeaponPoseFrame
{
    PlayerMotionFrame motion{};
    Vec2 aimDirection{-1.0F, 0.0F};
    AuthoredGunKind gun{AuthoredGunKind::Unarmed};
    std::uint64_t weaponId{};
    float aimHeight{}, drawProgress{};
    bool aimingUp{}, drawing{};
    std::string_view bodyClip{"modv2-idle-05"}, upperClip{};
    std::size_t bodyIndex{}, upperIndex{};
};

// A single update per simulation tick. All render passes share the stored snapshot.
class AuthoredWeaponPoseClock
{
public:
    void update(const AuthoredWeaponPoseInput &, float seconds) noexcept;
    [[nodiscard]] const AuthoredWeaponPoseFrame &frame() const noexcept { return frame_; }

private:
    AuthoredWeaponPoseFrame frame_{};
    double idleMs_{};
    bool bridgingAim_{};
};
