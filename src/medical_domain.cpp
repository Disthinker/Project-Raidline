#include "medical_domain.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t kLightBleedingDurationMs{40000};
constexpr std::uint32_t kLightDamageIntervalMs{1000};
constexpr std::uint32_t kHeavyDamageIntervalMs{500};
constexpr std::uint32_t kMinimumScreamDelayMs{15000};
constexpr std::uint32_t kMaximumScreamDelayMs{25000};

MedicalUseEffect effectOf(const MedicalUseDefinition &definition) noexcept
{
    switch (definition.effect)
    {
    case MedicalItemEffect::RestoreHealth:
        return MedicalUseEffect::RestoreHealth;
    case MedicalItemEffect::StopLightBleeding:
        return MedicalUseEffect::StopLightBleeding;
    case MedicalItemEffect::StopAnyBleeding:
        return MedicalUseEffect::StopAnyBleeding;
    case MedicalItemEffect::SuppressPain:
        return MedicalUseEffect::SuppressPain;
    }
    return MedicalUseEffect::RestoreHealth;
}

void clearBleeding(MedicalStatusState &status) noexcept
{
    status.bleeding = BleedingSeverity::None;
    status.lightBleedingRemainingMs = 0;
    status.bleedingDamageRemainingMs = 0;
    status.painScreamRemainingMs = 0;
}

bool medicalUseApplies(
    const ProfileState &profile,
    MedicalUseEffect effect) noexcept
{
    switch (effect)
    {
    case MedicalUseEffect::RestoreHealth:
        return profile.currentHealth < 100;
    case MedicalUseEffect::StopLightBleeding:
        return profile.medicalStatus.bleeding == BleedingSeverity::Light;
    case MedicalUseEffect::StopAnyBleeding:
        return profile.medicalStatus.bleeding != BleedingSeverity::None;
    case MedicalUseEffect::SuppressPain:
        return hasPain(profile.medicalStatus);
    }
    return false;
}

MedicalUseReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return MedicalUseReceipt{
        false,
        false,
        error,
        std::move(message),
        revision};
}
}

WoundRollResult applyWoundRoll(
    MedicalStatusState &status,
    const WoundRollCommand &command) noexcept
{
    WoundRollResult result{false, status.bleeding, status.bleeding};
    if (command.rollBasisPoints >= 10000 ||
        command.initialScreamDelayMs < kMinimumScreamDelayMs ||
        command.initialScreamDelayMs > kMaximumScreamDelayMs)
    {
        return result;
    }

    std::uint32_t chance{};
    BleedingSeverity target{BleedingSeverity::None};
    switch (command.source)
    {
    case WoundSource::None:
        return result;
    case WoundSource::Scratch:
        chance = 3500;
        target = BleedingSeverity::Light;
        break;
    case WoundSource::Bite:
        chance = 7500;
        target = BleedingSeverity::Heavy;
        break;
    }
    if (command.rollBasisPoints >= chance)
    {
        return result;
    }

    result.applied = true;
    const bool painBegan = status.bleeding == BleedingSeverity::None;
    if (target == BleedingSeverity::Heavy)
    {
        status.bleeding = BleedingSeverity::Heavy;
        status.lightBleedingRemainingMs = 0;
        status.bleedingDamageRemainingMs =
            status.bleedingDamageRemainingMs == 0
                ? kHeavyDamageIntervalMs
                : std::min(
                      status.bleedingDamageRemainingMs,
                      kHeavyDamageIntervalMs);
    }
    else if (status.bleeding != BleedingSeverity::Heavy)
    {
        status.bleeding = BleedingSeverity::Light;
        status.lightBleedingRemainingMs = kLightBleedingDurationMs;
        if (status.bleedingDamageRemainingMs == 0)
        {
            status.bleedingDamageRemainingMs = kLightDamageIntervalMs;
        }
    }

    if (painBegan)
    {
        // First pain vocalization is immediate on the next simulation tick.
        status.painScreamRemainingMs = 1;
    }
    else if (status.painScreamRemainingMs == 0)
    {
        status.painScreamRemainingMs = command.initialScreamDelayMs;
    }
    result.current = status.bleeding;
    return result;
}

MedicalAdvanceResult advanceMedicalStatus(
    MedicalStatusState &status,
    int &currentHealth,
    std::uint32_t elapsedMs,
    std::uint32_t nextScreamDelayMs) noexcept
{
    MedicalAdvanceResult result;
    if (currentHealth < 1 || currentHealth > 100 || elapsedMs == 0 ||
        nextScreamDelayMs < kMinimumScreamDelayMs ||
        nextScreamDelayMs > kMaximumScreamDelayMs)
    {
        return result;
    }

    std::uint32_t remaining = elapsedMs;
    for (std::size_t transition = 0;
         transition < 64 && remaining > 0;
         ++transition)
    {
        if (status.bleeding == BleedingSeverity::None &&
            status.painkillerRemainingMs == 0)
        {
            break;
        }

        std::uint32_t step = remaining;
        if (status.painkillerRemainingMs > 0)
        {
            step = std::min(step, status.painkillerRemainingMs);
        }
        if (status.bleeding != BleedingSeverity::None)
        {
            step = std::min(step, status.bleedingDamageRemainingMs);
            if (status.bleeding == BleedingSeverity::Light)
            {
                step = std::min(step, status.lightBleedingRemainingMs);
            }
            if (!painIsSuppressed(status) &&
                status.painScreamRemainingMs > 0)
            {
                step = std::min(step, status.painScreamRemainingMs);
            }
        }
        if (step == 0)
        {
            step = 1;
        }

        const bool screamClockRuns =
            hasPain(status) && !painIsSuppressed(status);
        if (status.painkillerRemainingMs > 0)
        {
            status.painkillerRemainingMs -=
                std::min(step, status.painkillerRemainingMs);
        }
        if (status.bleeding == BleedingSeverity::Light)
        {
            status.lightBleedingRemainingMs -=
                std::min(step, status.lightBleedingRemainingMs);
        }
        if (status.bleeding != BleedingSeverity::None)
        {
            status.bleedingDamageRemainingMs -=
                std::min(step, status.bleedingDamageRemainingMs);
            if (screamClockRuns && status.painScreamRemainingMs > 0)
            {
                status.painScreamRemainingMs -=
                    std::min(step, status.painScreamRemainingMs);
            }
        }
        remaining -= std::min(step, remaining);

        if (status.bleeding != BleedingSeverity::None &&
            status.bleedingDamageRemainingMs == 0)
        {
            if (currentHealth > 1)
            {
                --currentHealth;
                ++result.healthLost;
            }
            status.bleedingDamageRemainingMs =
                status.bleeding == BleedingSeverity::Light
                    ? kLightDamageIntervalMs
                    : kHeavyDamageIntervalMs;
        }

        if (screamClockRuns && status.painScreamRemainingMs == 0 &&
            status.bleeding != BleedingSeverity::None)
        {
            result.screamed = true;
            status.painScreamRemainingMs = nextScreamDelayMs;
        }

        if (status.bleeding == BleedingSeverity::Light &&
            status.lightBleedingRemainingMs == 0)
        {
            clearBleeding(status);
            result.lightBleedingEnded = true;
        }
    }
    return result;
}

MedicalUsePlan queryMedicalUse(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access)
{
    MedicalUsePlan plan;
    plan.revision = profile.revision;
    const AssetRecord *asset = profile.assets.find(medicalAssetId);
    if (asset == nullptr)
    {
        plan.error = DomainErrorCode::MissingAsset;
        plan.message = "medical asset does not exist";
        return plan;
    }
    if (access == MedicalAccess::CarriedOnly &&
        !assetIsCarried(profile, medicalAssetId))
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "medical asset is not carried into the Raid";
        return plan;
    }

    const ItemDefinition &definition = content.item(asset->definitionId);
    if (!definition.medicalUse.has_value() || asset->remainingCharges == 0)
    {
        plan.error = DomainErrorCode::InvalidQuantity;
        plan.message = "medical asset has no usable charge";
        return plan;
    }
    plan.effect = effectOf(*definition.medicalUse);
    plan.durationMs = definition.medicalUse->actionDurationMs;
    plan.slowMovement = definition.medicalUse->slowMovement;
    if (!medicalUseApplies(profile, plan.effect))
    {
        plan.error = DomainErrorCode::InvalidQuantity;
        plan.message = "medical effect is not applicable";
        return plan;
    }
    plan.canCommit = true;
    return plan;
}

MedicalUseReceipt executeMedicalUse(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access,
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
        return MedicalUseReceipt{
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

    const MedicalUsePlan plan = queryMedicalUse(
        profile,
        content,
        medicalAssetId,
        access);
    if (!plan.canCommit)
    {
        return failure(plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    AssetRecord *asset = candidate.assets.findMutable(medicalAssetId);
    const ItemDefinition &definition = content.item(asset->definitionId);
    const BleedingSeverity bleedingBefore = candidate.medicalStatus.bleeding;
    int healed{};
    switch (plan.effect)
    {
    case MedicalUseEffect::RestoreHealth:
        healed = std::min(
            static_cast<int>(definition.medicalUse->effectMagnitude),
            100 - candidate.currentHealth);
        candidate.currentHealth += healed;
        break;
    case MedicalUseEffect::StopLightBleeding:
    case MedicalUseEffect::StopAnyBleeding:
        clearBleeding(candidate.medicalStatus);
        break;
    case MedicalUseEffect::SuppressPain:
        candidate.medicalStatus.painkillerRemainingMs =
            definition.medicalUse->effectMagnitude;
        break;
    }

    --asset->remainingCharges;
    if (asset->remainingCharges == 0)
    {
        static_cast<void>(candidate.assets.erase(medicalAssetId));
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

    const BleedingSeverity bleedingAfter = candidate.medicalStatus.bleeding;
    profile = std::move(candidate);
    return MedicalUseReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.effect,
        healed,
        bleedingBefore,
        bleedingAfter};
}

MedicalUseReceipt beginContinuousHealing(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medicalAssetId,
    MedicalAccess access,
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
        return MedicalUseReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
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
    const MedicalUsePlan plan = queryMedicalUse(
        profile, content, medicalAssetId, access);
    if (!plan.canCommit || plan.effect != MedicalUseEffect::RestoreHealth)
    {
        return failure(
            plan.canCommit ? DomainErrorCode::InvalidQuantity : plan.error,
            plan.canCommit ? "medical item is not a healing item" : plan.message,
            profile.revision);
    }

    ProfileState candidate = profile;
    AssetRecord *asset = candidate.assets.findMutable(medicalAssetId);
    const BleedingSeverity bleeding = candidate.medicalStatus.bleeding;
    --asset->remainingCharges;
    if (asset->remainingCharges == 0)
    {
        static_cast<void>(candidate.assets.erase(medicalAssetId));
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    return MedicalUseReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        MedicalUseEffect::RestoreHealth,
        0,
        bleeding,
        bleeding};
}

std::optional<AssetInstanceId> selectQuickMedicalAsset(
    const ProfileState &profile,
    const ContentRegistry &content,
    MedicalUseEffect preferredEffect) noexcept
{
    try
    {
        const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
        if (!chest.has_value())
        {
            return std::nullopt;
        }
        const AssetRecord *chestAsset = profile.assets.find(*chest);
        const ItemDefinition &chestDefinition = content.item(
            chestAsset->definitionId);
        std::vector<const AssetRecord *> candidates;
        for (std::size_t index = 0;
             index < chestDefinition.containerCompartments.size();
             ++index)
        {
            if (chestDefinition.containerCompartments[index].pocketKind !=
                ContainerPocketKind::General)
            {
                continue;
            }
            for (const AssetRecord *asset : assetsInContainer(
                     profile,
                     ProfileContainerId::compartment(
                         *chest,
                         static_cast<std::uint32_t>(index))))
            {
                const ItemDefinition &definition = content.item(
                    asset->definitionId);
                if (definition.medicalUse.has_value() &&
                    effectOf(*definition.medicalUse) == preferredEffect &&
                    queryMedicalUse(
                        profile,
                        content,
                        asset->instanceId,
                        MedicalAccess::CarriedOnly).canCommit)
                {
                    candidates.push_back(asset);
                }
            }
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const AssetRecord *left, const AssetRecord *right)
            {
                if (left->remainingCharges != right->remainingCharges)
                {
                    return left->remainingCharges < right->remainingCharges;
                }
                return left->instanceId < right->instanceId;
            });
        if (!candidates.empty())
        {
            return candidates.front()->instanceId;
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}
