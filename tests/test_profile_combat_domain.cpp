#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "profile_combat_domain.h"

namespace
{
    AssetInstanceId findAsset(
        const ProfileState &profile,
        const ItemDefinitionId &definitionId)
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

    void equip(
        ProfileState &profile,
        const ContentRegistry &content,
        AssetInstanceId assetId,
        EquipmentSlotKind slot,
        const char *transactionId)
    {
        const InventoryReceipt receipt = executeInventory(
            profile,
            content,
            InventoryEquipCommand{assetId, slot},
            CommandContext{profile.revision, transactionId});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
}

TEST(ProfileCombatDomainTest, EquippedHelmetReducesHeadshotAndLosesDurability)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-head", content);
    const AssetInstanceId helmet = findAsset(profile, alpha_content::helmet);
    ASSERT_NE(helmet, 0U);
    equip(profile, content, helmet, EquipmentSlotKind::Helmet, "equip-helmet");

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile,
        content,
        IncomingDamageCommand{18, HitRegion::Head, 1, 3, false},
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
    const AssetInstanceId bodyArmor = findAsset(
        profile,
        alpha_content::bodyArmor);
    ASSERT_NE(bodyArmor, 0U);
    equip(
        profile,
        content,
        bodyArmor,
        EquipmentSlotKind::BodyArmor,
        "equip-body");
    const std::uint32_t durability = profile.assets.find(bodyArmor)
        ->currentDurability;

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile,
        content,
        IncomingDamageCommand{12, HitRegion::Legs, 0, 4, false},
        CommandContext{profile.revision, "leg-hit"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(receipt.armorAssetId.has_value());
    EXPECT_EQ(receipt.resolution.damageApplied, 9);
    EXPECT_EQ(profile.currentHealth, 91);
    EXPECT_EQ(
        profile.assets.find(bodyArmor)->currentDurability,
        durability);
}

TEST(ProfileCombatDomainTest, RejectedCommandLeavesProfileAndIdHighWaterUnchanged)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("armor-reject", content);
    const std::uint64_t before = profileStateFingerprint(profile);
    const AssetInstanceId nextBefore = profile.assets.nextAssetId();

    const IncomingDamageReceipt receipt = executeIncomingDamage(
        profile,
        content,
        IncomingDamageCommand{0, HitRegion::Torso, 0, 0, false},
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
        profile,
        content,
        IncomingDamageCommand{10, HitRegion::Torso, 0, 1, false},
        context);
    ASSERT_TRUE(first.succeeded);
    const std::uint64_t afterFirst = profileStateFingerprint(profile);

    const IncomingDamageReceipt repeated = executeIncomingDamage(
        profile,
        content,
        IncomingDamageCommand{10, HitRegion::Torso, 0, 1, false},
        context);

    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.idempotent);
    EXPECT_EQ(profileStateFingerprint(profile), afterFirst);
}
