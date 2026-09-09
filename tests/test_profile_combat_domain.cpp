#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "profile_combat_domain.h"

namespace
{
AssetInstanceId findAsset(const ProfileState &profile, const ItemDefinitionId &definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            return id;
        }
    }
    return 0;
}

void equip(ProfileState &profile, const ContentRegistry &content, AssetInstanceId assetId,
           EquipmentSlotKind slot, const char *transactionId)
{
    const InventoryReceipt receipt =
        executeInventory(profile, content, InventoryEquipCommand{assetId, slot},
                         CommandContext{profile.revision, transactionId});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}
} // namespace

TEST(ProfileCombatDomainTest, EquippedHelmetReducesHeadshotAndLosesDurability)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-head", content);
    const AssetInstanceId helmet = findAsset(profile, alpha_content::helmet);
    ASSERT_NE(helmet, 0U);
    equip(profile, content, helmet, EquipmentSlotKind::Helmet, "equip-helmet");

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile, content, IncomingDamageCommand{18, HitRegion::Head, 1, 3, false},
        CommandContext{profile.revision, "bite-1"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.armorAssetId, std::optional<AssetInstanceId>{helmet});
    EXPECT_EQ(receipt.resolution.semantic, HitSemantic::Headshot);
    EXPECT_EQ(receipt.resolution.damageBeforeArmor, 36);
    EXPECT_EQ(receipt.resolution.damageApplied, 12);
    EXPECT_TRUE(receipt.resolution.armorReducedDamage);
    EXPECT_EQ(receipt.resolution.armorDurabilityLoss, 3U);
    EXPECT_EQ(profile.currentHealth, 88);
    EXPECT_EQ(profile.assets.find(helmet)->currentDurability, 97U);
}

TEST(ProfileCombatDomainTest, TorsoArmorDoesNotProtectLegs)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-legs", content);
    const AssetInstanceId bodyArmor = findAsset(profile, alpha_content::bodyArmor);
    ASSERT_NE(bodyArmor, 0U);
    equip(profile, content, bodyArmor, EquipmentSlotKind::BodyArmor, "equip-body");
    const std::uint32_t durability = profile.assets.find(bodyArmor)->currentDurability;

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile, content, IncomingDamageCommand{12, HitRegion::Legs, 0, 4, false},
        CommandContext{profile.revision, "leg-hit"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.armorAssetId.has_value());
    EXPECT_EQ(receipt.resolution.damageApplied, 9);
    EXPECT_EQ(profile.currentHealth, 91);
    EXPECT_EQ(profile.assets.find(bodyArmor)->currentDurability, durability);
}

TEST(ProfileCombatDomainTest, RejectedCommandLeavesProfileAndIdHighWaterUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-reject", content);
    const std::uint64_t before = profileStateFingerprint(profile);
    const AssetInstanceId nextBefore = profile.assets.nextAssetId();

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile, content, IncomingDamageCommand{0, HitRegion::Torso, 0, 0, false},
        CommandContext{profile.revision, "invalid-hit"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(profile.assets.nextAssetId(), nextBefore);
}

TEST(ProfileCombatDomainTest, TransactionIsIdempotent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-idempotent", content);
    const CommandContext context{profile.revision, "same-hit"};

    const IncomingDamageReceipt first = executeIncomingDamage(
        profile, content, IncomingDamageCommand{10, HitRegion::Torso, 0, 1, false}, context);
    ASSERT_TRUE(first.succeeded);
    const std::uint64_t afterFirst = profileStateFingerprint(profile);

    const IncomingDamageReceipt repeated = executeIncomingDamage(
        profile, content, IncomingDamageCommand{10, HitRegion::Torso, 0, 1, false}, context);

    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), afterFirst);
}

TEST(ProfileCombatDomainTest, EffectiveBiteAtomicallyAppliesHeavyBleeding)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("bite-wound", content);
    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile, content,
        IncomingDamageCommand{18, HitRegion::Head, 1, 3, false,
                              WoundRollCommand{WoundSource::Bite, 7499, 20000}},
        CommandContext{profile.revision, "bite-wound"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(receipt.wound.applied);
    EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::Heavy);
    EXPECT_EQ(profile.currentHealth, 64);
}

TEST(ProfileCombatDomainTest, SimulationDamageMatchesFullTransactionForHealthWoundsAndArmor)
{
    const ContentRegistry &content = publishedContentRegistry();
    for (const HitRegion region : {HitRegion::Head, HitRegion::Torso, HitRegion::Legs})
    {
        ProfileState profile = makeNewAlphaProfile("damage-participants", content);
        equip(profile, content, findAsset(profile, alpha_content::helmet),
              EquipmentSlotKind::Helmet, "equip-head");
        equip(profile, content, findAsset(profile, alpha_content::bodyArmor),
              EquipmentSlotKind::BodyArmor, "equip-body");
        ProfileState expected = profile;
        const IncomingDamageCommand command{
            12, region, 0, 3, false, WoundRollCommand{WoundSource::Scratch, 0, 20000}};
        const CommandContext context{profile.revision, "runtime-hit"};
        const auto full = executeIncomingDamage(expected, content, command, context);
        const auto runtime = executeIncomingDamageInSimulation(profile, content, command, context);
        ASSERT_TRUE(full.succeeded) << full.message;
        ASSERT_TRUE(runtime.succeeded) << runtime.message;
        EXPECT_EQ(runtime.healthAfter, full.healthAfter);
        EXPECT_EQ(runtime.armorAssetId, full.armorAssetId);
        EXPECT_EQ(runtime.wound.current, full.wound.current);
        EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(expected));
        EXPECT_TRUE(validateProfileState(profile, content).valid);
    }
}

TEST(ProfileCombatDomainTest, SimulationDamageRejectionsLeaveAllParticipantsAndHighWaterUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState valid = makeNewAlphaProfile("damage-invalid-participants", content);
    const auto armorId = findAsset(valid, alpha_content::bodyArmor);
    equip(valid, content, armorId, EquipmentSlotKind::BodyArmor, "equip-body");
    for (int invalidCase = 0; invalidCase < 6; ++invalidCase)
    {
        ProfileState profile = valid;
        IncomingDamageCommand command{12, HitRegion::Torso, 0, 3, false};
        CommandContext context{profile.revision, "runtime-invalid"};
        if (invalidCase == 0)
            command.baseDamage = 0;
        if (invalidCase == 1)
            profile.currentHealth = 101;
        if (invalidCase == 2)
            profile.medicalStatus.painkillerRemainingMs = 180001;
        if (invalidCase == 3)
            profile.assets.findMutable(armorId)->currentMaximumDurability = 0;
        if (invalidCase == 4)
            context.expectedRevision = 0;
        if (invalidCase == 5)
            context.transactionId.clear();
        const auto before = profileStateFingerprint(profile);
        const auto highWater = profile.assets.nextAssetId();
        const auto receipt = executeIncomingDamageInSimulation(profile, content, command, context);
        EXPECT_FALSE(receipt.succeeded) << invalidCase;
        EXPECT_EQ(profileStateFingerprint(profile), before) << invalidCase;
        EXPECT_EQ(profile.assets.nextAssetId(), highWater) << invalidCase;
    }
}

TEST(ProfileCombatDomainTest, SimulationDamageIdempotencyPrecedesRevisionAndRejectsOverflow)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("damage-runtime-idempotency", content);
    const CommandContext context{profile.revision, "runtime-hit"};
    const IncomingDamageCommand command{5, HitRegion::Torso, 0, 0, false};
    ASSERT_TRUE(executeIncomingDamageInSimulation(profile, content, command, context).succeeded);
    const auto committed = profileStateFingerprint(profile);
    const auto repeated = executeIncomingDamageInSimulation(profile, content, command, context);
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
    profile.revision = std::numeric_limits<ProfileRevision>::max();
    const auto beforeOverflow = profileStateFingerprint(profile);
    const auto overflow = executeIncomingDamageInSimulation(profile, content, command,
                                                            {profile.revision, "overflow"});
    EXPECT_EQ(overflow.error, DomainErrorCode::RevisionOverflow);
    EXPECT_EQ(profileStateFingerprint(profile), beforeOverflow);
}

TEST(ProfileCombatDomainTest, SimulationLethalHitRequiresAnActiveActivityForTerminalHealth)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("damage-terminal", content);
    profile.currentHealth = 1;
    const IncomingDamageCommand command{10, HitRegion::Torso, 0, 0, false};
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeIncomingDamageInSimulation(profile, content, command,
                                                   {profile.revision, "idle-lethal"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    // The full event is validated by its service before entering simulation.
    profile.activeBaseDefense.emplace();
    const auto damage = executeIncomingDamageInSimulation(profile, content, command,
                                                          {profile.revision, "defense-lethal"});
    ASSERT_TRUE(damage.succeeded) << damage.message;
    EXPECT_EQ(profile.currentHealth, 0);
    const auto after = profileStateFingerprint(profile);
    EXPECT_FALSE(executeIncomingDamageInSimulation(profile, content, command,
                                                   {profile.revision, "after-down"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), after);
}

TEST(ProfileCombatDomainTest, DamageQueryAndSimulationDoNotReplaceUnrelatedAssetStorage)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("damage-large-inventory", content);
    const auto &ammo = content.item(alpha_content::ammunition);
    for (int index = 0; index < 1000; ++index)
    {
        static_cast<void>(profile.assets.create(
            ammo,
            BaseGroundAssetLocation{
                RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"},
                {1500.0F + static_cast<float>(index), 1500.0F}},
            1));
    }
    const AssetInstanceId unrelated = profile.assets.nextAssetId() - 1U;
    const AssetRecord *addressBefore = profile.assets.find(unrelated);
    const auto fingerprintBefore = profileStateFingerprint(profile);
    const IncomingDamageCommand command{1, HitRegion::Torso, 0, 0, false};
    EXPECT_TRUE(queryIncomingDamage(profile, content, command).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprintBefore);
    EXPECT_EQ(profile.assets.find(unrelated), addressBefore);
    for (int hit = 0; hit < 20; ++hit)
    {
        ASSERT_TRUE(
            executeIncomingDamageInSimulation(
                profile, content, command, {profile.revision, "runtime-hit-" + std::to_string(hit)})
                .succeeded);
        EXPECT_EQ(profile.assets.find(unrelated), addressBefore);
    }
    EXPECT_EQ(profile.currentHealth, 80);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
