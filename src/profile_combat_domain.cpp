#include "profile_combat_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
IncomingDamageReceipt failure(DomainErrorCode error, std::string message, ProfileRevision revision)
{
    return IncomingDamageReceipt{false, false, error, std::move(message), revision};
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

struct DamageParticipants
{
    IncomingDamageReceipt receipt;
    MedicalStatusState medicalAfter;
};

DamageParticipants inspect(const ProfileState &candidate, const ContentRegistry &content,
                           const IncomingDamageCommand &command)
{
    CombatDamageCommand damage{command.baseDamage,  command.region,    command.penetration,
                               command.armorDamage, command.weakPoint, std::nullopt};

    std::optional<AssetInstanceId> armorAssetId;
    if (const auto slot = armorSlot(command.region))
    {
        armorAssetId = equippedAsset(candidate, *slot);
        if (armorAssetId.has_value())
        {
            const AssetRecord *armor = candidate.assets.find(*armorAssetId);
            if (armor == nullptr)
            {
                return {failure(DomainErrorCode::InvalidProfile, "equipped armor asset is missing",
                                candidate.revision),
                        {}};
            }
            const ItemDefinition &definition = content.item(armor->definitionId);
            if (!definition.armorProtection.has_value())
            {
                return {failure(DomainErrorCode::InvalidProfile,
                                "equipped armor has no protection definition", candidate.revision),
                        {}};
            }
            damage.armor = ArmorProtectionView{
                definition.armorProtection->coverage,
                definition.armorProtection->protectionRequirement, armor->currentDurability,
                definition.armorProtection->durabilityLossBasisPoints};
        }
    }

    const CombatDamageResolution resolution = resolveCombatDamage(damage);
    if (!resolution.resolved())
    {
        return {failure(DomainErrorCode::InvalidQuantity, "incoming damage command is invalid",
                        candidate.revision),
                {}};
    }
    if (candidate.currentHealth <= 0)
    {
        return {failure(DomainErrorCode::IllegalDestination,
                        "dead profile cannot receive more damage", candidate.revision),
                {}};
    }

    const int healthBefore = candidate.currentHealth;
    const int healthAfter = std::max(0, healthBefore - resolution.damageApplied);

    WoundRollResult wound;
    MedicalStatusState medicalAfter = candidate.medicalStatus;
    if (resolution.damageApplied > 0)
    {
        wound = applyWoundRoll(medicalAfter, command.wound);
    }

    if (armorAssetId.has_value() && resolution.armorDurabilityLoss > 0)
    {
        const AssetRecord *armor = candidate.assets.find(*armorAssetId);
        if (armor == nullptr || armor->currentDurability < resolution.armorDurabilityLoss)
        {
            return {failure(DomainErrorCode::InvalidProfile,
                            "armor durability cannot pay the resolved loss", candidate.revision),
                    {}};
        }
    }

    return {IncomingDamageReceipt{true,
                                  false,
                                  DomainErrorCode::None,
                                  {},
                                  candidate.revision,
                                  resolution,
                                  armorAssetId,
                                  healthBefore,
                                  healthAfter,
                                  wound},
            medicalAfter};
}

void commitDamageParticipants(ProfileState &profile,
                              const DamageParticipants &participants) noexcept
{
    profile.currentHealth = participants.receipt.healthAfter;
    profile.medicalStatus = participants.medicalAfter;
    if (participants.receipt.armorAssetId &&
        participants.receipt.resolution.armorDurabilityLoss > 0)
    {
        profile.assets.findMutable(*participants.receipt.armorAssetId)->currentDurability -=
            participants.receipt.resolution.armorDurabilityLoss;
    }
}

IncomingDamageReceipt apply(ProfileState &candidate, const ContentRegistry &content,
                            const IncomingDamageCommand &command)
{
    DamageParticipants participants = inspect(candidate, content, command);
    if (participants.receipt.succeeded)
    {
        commitDamageParticipants(candidate, participants);
    }
    return participants.receipt;
}
} // namespace

IncomingDamagePlan queryIncomingDamage(const ProfileState &profile, const ContentRegistry &content,
                                       const IncomingDamageCommand &command)
{
    const IncomingDamageReceipt receipt = inspect(profile, content, command).receipt;
    return IncomingDamagePlan{receipt.succeeded, receipt.error,      receipt.message,
                              profile.revision,  receipt.resolution, receipt.armorAssetId,
                              receipt.wound};
}

IncomingDamageReceipt executeIncomingDamage(ProfileState &profile, const ContentRegistry &content,
                                            const IncomingDamageCommand &command,
                                            const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(DomainErrorCode::InvalidTransaction, "transaction ID must not be empty",
                       profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return IncomingDamageReceipt{true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(DomainErrorCode::StaleRevision, "profile revision is stale",
                       profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(DomainErrorCode::RevisionOverflow, "profile revision cannot advance",
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
    const ProfileValidationResult validation = validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return failure(DomainErrorCode::InvalidProfile, validation.message, profile.revision);
    }

    profile = std::move(candidate);
    receipt.revision = profile.revision;
    return receipt;
}

IncomingDamageReceipt executeIncomingDamageInSimulation(ProfileState &profile,
                                                        const ContentRegistry &content,
                                                        const IncomingDamageCommand &command,
                                                        const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(DomainErrorCode::InvalidTransaction, "transaction ID must not be empty",
                       profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(DomainErrorCode::StaleRevision, "profile revision is stale",
                       profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(DomainErrorCode::RevisionOverflow, "profile revision cannot advance",
                       profile.revision);
    }

    // Runtime input is a validated Profile. Check every participant changed by
    // this command, not the unrelated world/warehouse graph on every hit.
    DamageParticipants participants = inspect(profile, content, command);
    if (!participants.receipt.succeeded)
    {
        return participants.receipt;
    }
    if (profile.profileId.empty() || profile.revision == 0 || profile.currentHealth > 100 ||
        !validMedicalStatus(profile.medicalStatus) ||
        !validMedicalStatus(participants.medicalAfter) ||
        (participants.receipt.healthAfter == 0 && !profile.pendingRaid &&
         !profile.activeBaseDefense))
    {
        return failure(DomainErrorCode::InvalidProfile,
                       "incoming damage participant state is invalid", profile.revision);
    }
    if (participants.receipt.armorAssetId)
    {
        const AssetRecord &armor = *profile.assets.find(*participants.receipt.armorAssetId);
        const auto &definition = *content.item(armor.definitionId).armorProtection;
        if (armor.instanceId != *participants.receipt.armorAssetId ||
            armor.currentMaximumDurability == 0 ||
            armor.currentMaximumDurability > definition.maximumDurability ||
            armor.currentDurability > armor.currentMaximumDurability)
        {
            return failure(DomainErrorCode::InvalidProfile,
                           "armor durability is outside definition limits", profile.revision);
        }
    }

    // Allocate the receipt ledger entry before changing no-throw participants.
    profile.committedTransactions.insert(context.transactionId);
    commitDamageParticipants(profile, participants);
    ++profile.revision;
    participants.receipt.revision = profile.revision;
    return participants.receipt;
}
