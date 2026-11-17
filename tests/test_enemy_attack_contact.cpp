#include "enemy_attack_contact.h"
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

namespace {
Enemy activeAttack(EnemyAttackType type, CombatTargetId id = 7) {
    Enemy enemy{{100, 100}, {50, 50}, {}, 12, id};
    if (!enemy.tryStartAttack(type == EnemyAttackType::Bite ? EnemyAttackType::Grab : type, {1, 0}))
        throw std::runtime_error("attack setup");
    static_cast<void>(enemy.update(enemy.attackConfig()->windupDuration + 0.00001F, 1000, 1000));
    static_cast<void>(enemy.setPosition({100, 100}));
    if (type == EnemyAttackType::Bite && !enemy.confirmGrabContact())
        throw std::runtime_error("bite setup");
    return enemy;
}
constexpr Rect nearTarget{{120, 100}, {40, 50}};
constexpr Rect forwardTarget{{160, 100}, {40, 50}};
}

TEST(EnemyAttackContact, AcceptedScratchAndBitePreserveDamageIdentityAndLegacyAdapter) {
    for (auto type : {EnemyAttackType::Scratch, EnemyAttackType::Bite}) {
        auto enemy = activeAttack(type);
        float timer = 0;
        const auto result = resolveEnemyAttackContact(enemy, forwardTarget, true, timer, [] { return true; });
        EXPECT_EQ(result.disposition, EnemyContactDisposition::Applied);
        ASSERT_TRUE(result.damage);
        EXPECT_EQ(*result.damage, enemyAttackDamageObservation(7, type));
        EXPECT_EQ(result.legacyDamage, type == EnemyAttackType::Scratch ? 1 : 2);
        EXPECT_FLOAT_EQ(result.controlDuration, type == EnemyAttackType::Scratch ? 0.0F : 0.75F);
        EXPECT_FLOAT_EQ(timer, 0.25F);
        EXPECT_TRUE(enemy.checkpoint().hitConsumed);
        const auto after = enemy.checkpoint();
        timer = 0;
        EXPECT_EQ(resolveEnemyAttackContact(enemy, forwardTarget, true, timer, [] { return true; }).disposition,
                  EnemyContactDisposition::NoContact);
        EXPECT_EQ(enemy.checkpoint(), after);
        EXPECT_FLOAT_EQ(timer, 0);
    }
}

TEST(EnemyAttackContact, GrabConfirmsAndConsumesBiteInOneCall) {
    auto enemy = activeAttack(EnemyAttackType::Grab);
    float timer = 0;
    const auto result = resolveEnemyAttackContact(enemy, nearTarget, true, timer, [] { return true; });
    ASSERT_TRUE(result.damage);
    EXPECT_EQ(*result.damage, enemyAttackDamageObservation(7, EnemyAttackType::Bite));
    EXPECT_EQ(enemy.attackType(), EnemyAttackType::Bite);
    EXPECT_TRUE(enemy.checkpoint().hitConsumed);
    EXPECT_FALSE(enemy.hasGrabContactOpportunity());
    EXPECT_FALSE(enemy.hasAttackHitOpportunity());
    EXPECT_FLOAT_EQ(result.controlDuration, 0.75F);
}

TEST(EnemyAttackContact, ProtectionConsumesContactWithoutDamageControlOrExtension) {
    for (auto type : {EnemyAttackType::Scratch, EnemyAttackType::Grab, EnemyAttackType::Bite}) {
        auto enemy = activeAttack(type);
        float timer = 0.1F;
        const auto target = type == EnemyAttackType::Grab ? nearTarget : forwardTarget;
        const auto result = resolveEnemyAttackContact(enemy, target, true, timer, [] { return true; });
        EXPECT_EQ(result.disposition, EnemyContactDisposition::Suppressed);
        EXPECT_FALSE(result.damage);
        EXPECT_EQ(result.legacyDamage, 0);
        EXPECT_FLOAT_EQ(result.controlDuration, 0);
        EXPECT_FLOAT_EQ(timer, 0.1F);
        EXPECT_TRUE(enemy.checkpoint().hitConsumed);
        // Existing checkpoint fields already express suppression. Restore must
        // not recreate a pending hit when the player's protection later ends.
        auto restored = Enemy::restoreCheckpoint(enemy.checkpoint());
        ASSERT_TRUE(restored);
        timer = 0;
        EXPECT_EQ(resolveEnemyAttackContact(*restored, target, true, timer, [] { return true; }).disposition,
                  EnemyContactDisposition::NoContact);
        EXPECT_FLOAT_EQ(timer, 0);
    }
}

TEST(EnemyAttackContact, InvalidTargetsRejectWithoutMutationOrOcclusionWork) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        auto enemy = activeAttack(EnemyAttackType::Scratch);
        if (scenario == 0) ASSERT_TRUE(enemy.takeDamage(12));
        if (scenario == 1) enemy = Enemy{{100, 100}, {50, 50}, {}, 12, 7};
        const auto before = enemy.checkpoint();
        const auto target = scenario == 2 ? Rect{{800, 800}, {40, 50}} : forwardTarget;
        float timer = scenario == 4 ? -1.0F : 0.0F;
        const float oldTimer = timer;
        int queries = 0;
        const auto result = resolveEnemyAttackContact(enemy, target, scenario != 3, timer,
                                                      [&] { ++queries; return true; });
        EXPECT_EQ(result.disposition, EnemyContactDisposition::NoContact);
        EXPECT_FALSE(result.damage);
        EXPECT_EQ(enemy.checkpoint(), before);
        EXPECT_FLOAT_EQ(timer, oldTimer);
        EXPECT_EQ(queries, 0);
    }
}

TEST(EnemyAttackContact, OccludedContactDoesNotConsumeGrabOrExtendProtection) {
    for (auto type : {EnemyAttackType::Scratch, EnemyAttackType::Grab, EnemyAttackType::Bite}) {
        auto enemy = activeAttack(type);
        const auto before = enemy.checkpoint();
        float timer = 0.1F;
        int queries = 0;
        const auto result = resolveEnemyAttackContact(enemy, type == EnemyAttackType::Grab ? nearTarget : forwardTarget,
            true, timer, [&] { ++queries; return false; });
        EXPECT_EQ(result.disposition, EnemyContactDisposition::NoContact);
        EXPECT_EQ(queries, 1);
        EXPECT_EQ(enemy.checkpoint(), before);
        EXPECT_FLOAT_EQ(timer, 0.1F);
    }
}

TEST(EnemyAttackContact, ExactExpiryAppliesButPositiveRemainderSuppresses) {
    for (float remaining : {0.0F, 0.000001F}) {
        auto enemy = activeAttack(EnemyAttackType::Scratch);
        const bool expired = remaining == 0;
        const auto result = resolveEnemyAttackContact(enemy, forwardTarget, true, remaining, [] { return true; });
        EXPECT_EQ(result.disposition, expired ? EnemyContactDisposition::Applied
                                              : EnemyContactDisposition::Suppressed);
        EXPECT_FLOAT_EQ(remaining, expired ? 0.25F : 0.000001F);
        EXPECT_TRUE(enemy.checkpoint().hitConsumed);
    }
    auto enemy = activeAttack(EnemyAttackType::Scratch);
    const auto before = enemy.checkpoint();
    float invalid = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(resolveEnemyAttackContact(enemy, forwardTarget, true, invalid, [] { return true; }).disposition,
              EnemyContactDisposition::NoContact);
    EXPECT_EQ(enemy.checkpoint(), before);
    EXPECT_TRUE(std::isnan(invalid));
}

TEST(EnemyAttackContact, MultipleActorsProduceOnlyOneAcceptedFactWithinInterval) {
    float timer = 0;
    auto first = activeAttack(EnemyAttackType::Scratch, 11);
    auto second = activeAttack(EnemyAttackType::Grab, 12);
    auto third = activeAttack(EnemyAttackType::Bite, 13);
    const auto a = resolveEnemyAttackContact(first, forwardTarget, true, timer, [] { return true; });
    const auto b = resolveEnemyAttackContact(second, nearTarget, true, timer, [] { return true; });
    const auto c = resolveEnemyAttackContact(third, forwardTarget, true, timer, [] { return true; });
    ASSERT_TRUE(a.damage);
    EXPECT_EQ(a.damage->sourceEnemyId, 11U);
    EXPECT_EQ(b.disposition, EnemyContactDisposition::Suppressed);
    EXPECT_EQ(c.disposition, EnemyContactDisposition::Suppressed);
    EXPECT_FALSE(b.damage);
    EXPECT_FALSE(c.damage);
    EXPECT_FLOAT_EQ(timer, 0.25F);
    EXPECT_TRUE(first.checkpoint().hitConsumed);
    EXPECT_TRUE(second.checkpoint().hitConsumed);
    EXPECT_TRUE(third.checkpoint().hitConsumed);
}

TEST(EnemyAttackContact, HospitalLowLungeTelegraphsLockedLegStrikeAndConsumesOneHit)
{
    Enemy enemy{{100, 100}, {50, 50}, {}, 12, 7, std::nullopt,
        lowLungeEnemyVariantSnapshot()};
    Enemy ordinary{{100, 100}, {50, 50}, {}, 12, 8};
    EXPECT_FALSE(ordinary.tryStartAttack(EnemyAttackType::LowLunge, {1, 0}));

    const EnemyTacticalDirective engage{EnemyTacticalRole::Engage, true};
    static_cast<void>(enemy.updateTowardsTarget({185, 125}, engage, 0.0F, 1000, 1000));
    ASSERT_EQ(enemy.attackType(), EnemyAttackType::LowLunge);
    ASSERT_EQ(enemy.attackPhase(), EnemyAttackPhase::Windup);
    EXPECT_FLOAT_EQ(enemy.attackPhaseRemaining(), 0.70F);
    ASSERT_TRUE(enemy.lowLungeCorridor());
    EXPECT_FLOAT_EQ(enemy.lowLungeCorridor()->origin.x, 125.0F);
    EXPECT_FLOAT_EQ(enemy.attackDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(enemy.attackDirection().y, 0.0F);

    // Changing sides while it winds up does not steer the attack.
    static_cast<void>(enemy.updateTowardsTarget({125, 185}, engage, 0.68F, 1000, 1000));
    EXPECT_FALSE(enemy.hasAttackHitOpportunity());
    static_cast<void>(enemy.updateTowardsTarget({125, 185}, engage, 0.04F, 1000, 1000));
    ASSERT_EQ(enemy.attackPhase(), EnemyAttackPhase::Active);
    ASSERT_TRUE(enemy.lowLungeCorridor());
    EXPECT_FLOAT_EQ(enemy.lowLungeCorridor()->origin.x, 125.0F);
    EXPECT_FLOAT_EQ(enemy.attackDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(enemy.attackDirection().y, 0.0F);
    EXPECT_GT(enemy.position().x, 100.0F);

    float timer = 0.0F;
    const Rect sidestep{{155, 160}, {40, 50}};
    int sightChecks = 0;
    EXPECT_EQ(resolveEnemyAttackContact(enemy, sidestep, true, timer,
        [&] { ++sightChecks; return true; }).disposition, EnemyContactDisposition::NoContact);
    EXPECT_EQ(sightChecks, 0);
    EXPECT_EQ(resolveEnemyAttackContact(enemy, forwardTarget, true, timer,
        [&] { ++sightChecks; return false; }).disposition, EnemyContactDisposition::NoContact);
    EXPECT_EQ(sightChecks, 1);
    const auto hit = resolveEnemyAttackContact(enemy, forwardTarget, true, timer, [] { return true; });
    ASSERT_TRUE(hit.damage);
    EXPECT_EQ(hit.damage->baseDamage, 22);
    EXPECT_EQ(hit.damage->region, HitRegion::Legs);
    EXPECT_EQ(hit.damage->attackType, EnemyAttackType::LowLunge);
    EXPECT_EQ(hit.damage->woundSource, WoundSource::None);
    EXPECT_EQ(hit.damage->sourceEnemyId, 7U);
    EXPECT_FLOAT_EQ(hit.controlDuration, 0.0F);
    const auto resolved = resolveCombatDamage({hit.damage->baseDamage, hit.damage->region,
        hit.damage->penetration, hit.damage->armorDamage, hit.damage->weakPoint, std::nullopt});
    EXPECT_EQ(resolved.damageApplied, 17);
    timer = 0.0F;
    EXPECT_FALSE(resolveEnemyAttackContact(enemy, forwardTarget, true, timer,
        [] { return true; }).damage);
}

TEST(EnemyAttackContact, HospitalLowLungeCannotStrikeAfterDeathOrProtection)
{
    for (const bool killDuringWindup : {false, true})
    {
        Enemy enemy{{100, 100}, {50, 50}, {}, 12, 7, std::nullopt,
            lowLungeEnemyVariantSnapshot()};
        ASSERT_TRUE(enemy.tryStartAttack(EnemyAttackType::LowLunge, {1, 0}));
        if (killDuringWindup)
            ASSERT_TRUE(enemy.takeDamage(12));
        static_cast<void>(enemy.update(0.73F, 1000, 1000));
        float timer = killDuringWindup ? 0.0F : 0.1F;
        const auto hit = resolveEnemyAttackContact(enemy, forwardTarget, true, timer, [] { return true; });
        EXPECT_EQ(hit.disposition, killDuringWindup ? EnemyContactDisposition::NoContact :
            EnemyContactDisposition::Suppressed);
        EXPECT_FALSE(hit.damage);
        timer = 0.0F;
        EXPECT_FALSE(resolveEnemyAttackContact(enemy, forwardTarget, true, timer,
            [] { return true; }).damage);
    }
}

TEST(EnemyAttackContact, HospitalLowLungeDirectionalBoundsAndCoverClipAgree)
{
    constexpr float diagonal = 0.70710678118F;
    EnemyLowLungeCorridor corridor{{100, 100}, {diagonal, diagonal}};
    EXPECT_TRUE(corridor.overlaps({{151, 153}, {7, 8}}));
    // Inside the broad rectangle, but well away from the real diagonal.
    EXPECT_FALSE(corridor.overlaps({{113, 160}, {8, 8}}));
    corridor.clipBefore({{115, 70}, {30, 9}});
    EXPECT_FLOAT_EQ(corridor.reach, 100.0F);
    // The near corner at (130,130) truncates the entire visible threat strip.
    corridor.clipBefore({{130, 130}, {10, 20}});
    EXPECT_NEAR(corridor.reach, 60.0F * diagonal, 0.001F);
    EXPECT_TRUE(corridor.overlaps({{110, 112}, {5, 5}}));
    EXPECT_FALSE(corridor.overlaps({{151, 153}, {7, 8}}));
}
