#include "medical_domain.h"
#include "raid_fire_support_domain.h"

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
    case MedicalItemEffect::StabilizeFracture:
        return MedicalUseEffect::StabilizeFracture;
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
    case MedicalUseEffect::StabilizeFracture:
        return profile.medicalStatus.legFractured;
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

bool medicalConsolidationAvailable(const ProfileState &profile) noexcept
{
    return !profile.pendingRaid && !profile.activeBaseDefense && !profile.homePerimeter.activeOuting;
}

bool isPersonalMultiUseMedicine(
    const ProfileState &profile, const ContentRegistry &content, const AssetRecord &asset)
{
    if (asset.quantity != 1U || asset.remainingCharges == 0U || asset.reliefBatchId ||
        !std::holds_alternative<StoredAssetLocation>(asset.location) ||
        (profile.basePlayerEffects && profile.basePlayerEffects->medical &&
         profile.basePlayerEffects->medical->medicalAssetId == asset.instanceId))
        return false;
    const auto &definition = content.item(asset.definitionId);
    return definition.category == ItemCategory::Medical && definition.medicalUse &&
        definition.maximumCharges > 1U && asset.remainingCharges <= definition.maximumCharges &&
        assetIsInStashOrEquipped(profile, asset.instanceId);
}
}

std::vector<AssetInstanceId> medicalConsolidationCandidates(const ProfileState &profile, const ContentRegistry &content)
{
    std::vector<AssetInstanceId> result;
    if (!medicalConsolidationAvailable(profile)) return result;
    for (const auto &[id, asset] : profile.assets.records())
        if (isPersonalMultiUseMedicine(profile, content, asset))
            result.push_back(id);
    return result;
}

MedicalConsolidationPlan queryMedicalConsolidation(
    const ProfileState &profile, const ContentRegistry &content, AssetInstanceId sourceId, AssetInstanceId targetId)
{
    const auto reject = [&](DomainErrorCode error, const char *message) {
        return MedicalConsolidationPlan{false, error, message, profile.revision};
    };
    if (!medicalConsolidationAvailable(profile))
        return reject(DomainErrorCode::IllegalDestination, "medical consolidation is only available in Base");
    if (!sourceId || !targetId || sourceId == targetId)
        return reject(DomainErrorCode::InvalidQuantity, "select two different medical items");
    const auto *source = profile.assets.find(sourceId);
    const auto *target = profile.assets.find(targetId);
    if (!source || !target || !isPersonalMultiUseMedicine(profile, content, *source) ||
        !isPersonalMultiUseMedicine(profile, content, *target))
        return reject(DomainErrorCode::IllegalDestination, "only personal multi-use medicines can be combined");
    if (source->definitionId != target->definitionId)
        return reject(DomainErrorCode::IncompatibleEquipment, "select two matching medical items");
    const auto free = content.item(target->definitionId).maximumCharges - target->remainingCharges;
    if (!free)
        return reject(DomainErrorCode::Capacity, "target medical item is full");
    const auto transfer = std::min(free, source->remainingCharges);
    return {true, DomainErrorCode::None, {}, profile.revision,
        source->remainingCharges, target->remainingCharges, transfer,
        source->remainingCharges - transfer, target->remainingCharges + transfer};
}

InventoryReceipt executeMedicalConsolidation(ProfileState &profile, const ContentRegistry &content,
    const MedicalConsolidationCommand &command, const CommandContext &context)
{
    if (context.transactionId.empty())
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID must not be empty", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return {false, false, DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, false, DomainErrorCode::RevisionOverflow, "profile revision cannot advance", profile.revision};
    const auto plan = queryMedicalConsolidation(profile, content, command.sourceId, command.targetId);
    if (!plan.canCommit)
        return {false, false, plan.error, plan.message, profile.revision};
    if (plan.sourceBefore != command.sourceBefore || plan.targetBefore != command.targetBefore)
        return {false, false, DomainErrorCode::StaleRevision, "medical charges changed", profile.revision};

    ProfileState candidate = profile;
    candidate.assets.findMutable(command.targetId)->remainingCharges = plan.targetAfter;
    if (plan.sourceAfter)
        candidate.assets.findMutable(command.sourceId)->remainingCharges = plan.sourceAfter;
    else
        static_cast<void>(candidate.assets.erase(command.sourceId));
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return {false, false, DomainErrorCode::InvalidProfile, validation.message, profile.revision};
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision};
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
            status.bleeding != BleedingSeverity::None && !painIsSuppressed(status);
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
    MedicalAccess access,
    std::optional<TeammateMedicalTarget> teammate)
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
    if (isMedicalReserveAsset(*asset) ||
        std::holds_alternative<BaseServiceAssetLocation>(asset->location))
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "medical asset is reserved by a Base service";
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
    if (plan.effect == MedicalUseEffect::StabilizeFracture &&
        access == MedicalAccess::AnyOwned &&
        (profile.pendingRaid || !assetIsBaseAccessible(profile, medicalAssetId)))
    {
        plan.error = DomainErrorCode::IllegalDestination;
        plan.message = "splint is not available for personal treatment in Base";
        return plan;
    }
    plan.durationMs = definition.medicalUse->actionDurationMs;
    plan.slowMovement = definition.medicalUse->slowMovement;
    const bool applicable = teammate
        ? access == MedicalAccess::CarriedOnly && hasLivingRaidFireSupport(profile) &&
          teammate->health > 0 && teammate->health < teammate->maximumHealth &&
          plan.effect == MedicalUseEffect::RestoreHealth
        : medicalUseApplies(profile, plan.effect);
    if (!applicable)
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
    const CommandContext &context,
    std::optional<TeammateMedicalTarget> teammate)
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
        access,
        teammate);
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
            teammate ? teammate->maximumHealth - teammate->health : 100 - candidate.currentHealth);
        if (!teammate)
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
    case MedicalUseEffect::StabilizeFracture:
        candidate.medicalStatus.legFractured = false;
        if (candidate.basePlayerEffects && candidate.basePlayerEffects->medical &&
            candidate.basePlayerEffects->medical->effect == MedicalUseEffect::StabilizeFracture)
        {
            // The completed action cannot recover an unconsumed splint.
            candidate.basePlayerEffects->medical.reset();
        }
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
