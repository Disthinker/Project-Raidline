#include "profile_combat_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
    IncomingDamageReceipt failure(
        DomainErrorCode error,
        std::string message,
        ProfileRevision revision)
    {
        return IncomingDamageReceipt{
            false,
            false,
            error,
            std::move(message),
            revision};
    }

    std::optional<EquipmentSlotKind> armorSlot(HitRegion region) noexcept
    {
        switch (region)
        {
        case HitRegion::Head:
            return EquipmentSlotKind::Helmet;
        case HitRegion::Torso:
            return EquipmentSlotKind::BodyArmor;
        case HitRegion::Legs:
            return std::nullopt;
        }
        return std::nullopt;
    }

    IncomingDamageReceipt apply(
        ProfileState &candidate,
        const ContentRegistry &content,
        const IncomingDamageCommand &command)
    {
        CombatDamageCommand damage{
            command.baseDamage,
            command.region,
            command.penetration,
            command.armorDamage,
            command.weakPoint,
            std::nullopt};

        std::optional<AssetInstanceId> armorAssetId;
        if (const auto slot = armorSlot(command.region))
        {
            armorAssetId = equippedAsset(candidate, *slot);
            if (armorAssetId.has_value())
            {
                const AssetRecord *armor = candidate.assets.find(*armorAssetId);
                if (armor == nullptr)
                {
                    return failure(
                        DomainErrorCode::InvalidProfile,
                        "equipped armor asset is missing",
                        candidate.revision);
                }
                const ItemDefinition &definition = content.item(
                    armor->definitionId);
                if (!definition.armorProtection.has_value())
                {
                    return failure(
                        DomainErrorCode::InvalidProfile,
                        "equipped armor has no protection definition",
                        candidate.revision);
                }
                damage.armor = ArmorProtectionView{
                    definition.armorProtection->coverage,
                    definition.armorProtection->protectionRequirement,
                    armor->currentDurability,
                    definition.armorProtection->durabilityLossBasisPoints};
            }
        }

        const CombatDamageResolution resolution = resolveCombatDamage(damage);
        if (!resolution.resolved())
        {
            return failure(
                DomainErrorCode::InvalidQuantity,
                "incoming damage command is invalid",
                candidate.revision);
        }
        if (candidate.currentHealth <= 0)
        {
            return failure(
                DomainErrorCode::IllegalDestination,
                "dead profile cannot receive more damage",
                candidate.revision);
        }

        const int healthBefore = candidate.currentHealth;
        candidate.currentHealth = std::max(
            0,
            healthBefore - resolution.damageApplied);

        if (armorAssetId.has_value() &&
            resolution.armorDurabilityLoss > 0)
        {
            AssetRecord *armor = candidate.assets.findMutable(*armorAssetId);
            if (armor == nullptr ||
                armor->currentDurability < resolution.armorDurabilityLoss)
            {
                return failure(
                    DomainErrorCode::InvalidProfile,
                    "armor durability cannot pay the resolved loss",
                    candidate.revision);
            }
            armor->currentDurability -= resolution.armorDurabilityLoss;
        }

        return IncomingDamageReceipt{
            true,
            false,
            DomainErrorCode::None,
            {},
            candidate.revision,
            resolution,
            armorAssetId,
            healthBefore,
            candidate.currentHealth};
    }
}

IncomingDamagePlan queryIncomingDamage(
    const ProfileState &profile,
    const ContentRegistry &content,
    const IncomingDamageCommand &command)
{
    ProfileState candidate = profile;
    const IncomingDamageReceipt receipt = apply(candidate, content, command);
    return IncomingDamagePlan{
        receipt.succeeded,
        receipt.error,
        receipt.message,
        profile.revision,
        receipt.resolution,
        receipt.armorAssetId};
}

IncomingDamageReceipt executeIncomingDamage(
    ProfileState &profile,
    const ContentRegistry &content,
    const IncomingDamageCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return IncomingDamageReceipt{
            true,
            true,
            DomainErrorCode::None,
            {},
            profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    ProfileState candidate = profile;
    IncomingDamageReceipt receipt = apply(candidate, content, command);
    if (!receipt.succeeded)
    {
        receipt.revision = profile.revision;
        return receipt;
    }

    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate,
        content);
    if (!validation.valid)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }

    profile = std::move(candidate);
    receipt.revision = profile.revision;
    return receipt;
}
