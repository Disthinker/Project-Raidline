#pragma once

#include <optional>
#include <cstdint>
#include <vector>

#include "item_definition.h"
#include "rect.h"
#include "vec2.h"

enum class RaidReloadInputKind { Automatic, Selected, UpgradeEmergency };
// Stable asset IDs only: this device-independent intent never owns assets.
struct RaidReloadIntent {
    RaidReloadInputKind kind{};
    std::uint64_t expectedWeaponId{};
    std::optional<std::uint64_t> magazineId;
    float ageSeconds{};
    // One short-lived identity joins the first release and its double tap.
    // Event timing is independent of frame/action progression.
    std::uint64_t gestureId{};
    double doubleTapSeconds{};
};

struct GameplayInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool sprint{};
    bool meleeJustPressed{}, meleePressed{};

    bool fireJustPressed{};
    bool firePressed{};
    // Only the service's release barrier reads this unmasked device fact.
    // It never grants permission to shoot through a modal surface.
    bool firePhysicallyPressed{};
    bool aimDownSights{};
    // A modal or consumed secondary-button press must not open an optic.
    // Mechanical aim retains its existing input rules.
    bool allowWeaponOptics{true};
    bool forceMaximumWeaponSpread{};

    // Runtime developer override supplied by the SDL client. It never enters
    // ProfileState or persistence. The service still validates that the
    // equipped weapon can cycle ammunition; successful shots simply discard
    // the candidate ammo mutation instead of committing it.
    bool developerInfiniteAmmo{};

    // World-space pointer aim. Absence preserves the movement/previous facing
    // direction so the Space-key regression path remains available.
    std::optional<Vec2> aimWorldPosition;

    // SDL relative mouse motion is an explicit per-frame delta. Simulation
    // tests and legacy callers may keep using the absolute position above.
    std::optional<Vec2> aimMotionDelta;

    // Client-provided world-space visibility constraint. Gameplay simulation
    // clamps both the actual reticle and its control target to this rectangle,
    // so rendering and authoritative firing direction cannot diverge.
    std::optional<Rect> aimWorldBounds;

    // Full client viewport in world coordinates, before movement/portal
    // transfer. Transient projection data; no SDL or persisted camera state.
    std::optional<Rect> aimViewport;

    // 只在 F 从未按下变为按下的这一帧为 true。
    bool interactJustPressed{};
    // 可中断的世界交互读取持续状态；单击拾取仍使用上面的边沿。
    bool interactPressed{};

    bool reloadJustPressed{};
    bool cycleFireModeJustPressed{};
    std::vector<RaidReloadIntent> raidReloadIntents;
    // Interrupt intent only. Starting an action requires an ordered intent.
    bool reloadGestureJustPressed{};
    bool healJustPressed{};
    bool lightJustPressed{}, lightJustReleased{}, lightPressed{};
    std::optional<EquipmentSlotKind> weaponSlotJustPressed;
    bool quitRaidJustPressed{};
    bool inventoryOpen{};
    bool throwableModal{};
    // Interface/focus cursor must never rotate the world observation cone.
    bool freezeObservationDirection{};
    float movementSpeedMultiplier{1.0F};

    // Service-owned profile query; the world only consumes this eligibility
    // bit and never inspects asset ownership.
    bool conditionalExtractionEligible{};
    // Service-owned mission objective gate. All extraction types remain
    // unavailable until the active mission contract is complete.
    bool extractionEligible{true};
    // Service arbitration preserves portal/container/loot priority for F.
    bool credentialInvestigationAllowed{true};
};
