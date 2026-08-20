#include "raid_action.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
template <typename Action>
float actionProgress(const Action &action) noexcept
{
    return std::clamp(
        action.elapsedSeconds / action.durationSeconds,
        0.0F,
        1.0F);
}

bool validAction(const RaidAction &action) noexcept
{
    return std::visit(
        [](const auto &typed)
        {
            return std::isfinite(typed.durationSeconds) &&
                   typed.durationSeconds > 0.0F &&
                   std::isfinite(typed.elapsedSeconds) &&
                   typed.elapsedSeconds >= 0.0F;
        },
        action);
}
}

bool RaidActionState::start(RaidAction action) noexcept
{
    if (active_.has_value() || completed_.has_value() || !validAction(action))
    {
        return false;
    }
    active_ = std::move(action);
    return true;
}

RaidActionAdvance RaidActionState::update(
    float deltaTime,
    bool interrupted) noexcept
{
    if (!active_.has_value())
    {
        return RaidActionAdvance::Idle;
    }
    if (interrupted)
    {
        active_.reset();
        return RaidActionAdvance::Interrupted;
    }
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return RaidActionAdvance::Running;
    }

    bool complete{};
    std::visit(
        [deltaTime, &complete](auto &typed)
        {
            typed.elapsedSeconds = std::min(
                typed.durationSeconds,
                typed.elapsedSeconds + deltaTime);
            complete = typed.elapsedSeconds >= typed.durationSeconds;
        },
        *active_);
    if (!complete)
    {
        return RaidActionAdvance::Running;
    }
    completed_ = std::move(active_);
    active_.reset();
    return RaidActionAdvance::Completed;
}

void RaidActionState::cancel() noexcept
{
    active_.reset();
    completed_.reset();
}

const std::optional<RaidAction> &RaidActionState::active() const noexcept
{
    return active_;
}

RaidAction *RaidActionState::activeMutable() noexcept
{
    return active_.has_value() ? &*active_ : nullptr;
}

std::optional<RaidAction> RaidActionState::takeCompleted() noexcept
{
    std::optional<RaidAction> result = std::move(completed_);
    completed_.reset();
    return result;
}

float RaidActionState::progress() const noexcept
{
    if (!active_.has_value())
    {
        return 0.0F;
    }
    return std::visit(
        [](const auto &typed) { return actionProgress(typed); },
        *active_);
}

std::optional<AssetInstanceId> selectRaidReloadMagazine(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId weaponAssetId) noexcept
{
    try
    {
        const AssetRecord *weapon = profile.assets.find(weaponAssetId);
        if (weapon == nullptr)
        {
            return std::nullopt;
        }
        const ItemDefinition &weaponDefinition = content.item(weapon->definitionId);
        if (!weaponDefinition.compatibleMagazineDefinitionId.has_value())
        {
            return std::nullopt;
        }
        const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
        if (!chest.has_value())
        {
            return std::nullopt;
        }
        const ItemDefinition &chestDefinition =
            content.item(profile.assets.find(*chest)->definitionId);
        std::vector<const AssetRecord *> candidates;
        for (std::size_t index = 0;
             index < chestDefinition.containerCompartments.size();
             ++index)
        {
            if (chestDefinition.containerCompartments[index].pocketKind !=
                ContainerPocketKind::MagazineOnly)
            {
                continue;
            }
            for (const AssetRecord *asset : assetsInContainer(
                     profile,
                     ProfileContainerId::compartment(
                         *chest,
                         static_cast<std::uint32_t>(index))))
            {
                if (asset->definitionId ==
                        *weaponDefinition.compatibleMagazineDefinitionId)
                {
                    candidates.push_back(asset);
                }
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const AssetRecord *left, const AssetRecord *right)
            {
                if (left->magazineRounds.size() != right->magazineRounds.size())
                {
                    return left->magazineRounds.size() >
                           right->magazineRounds.size();
                }
                return left->instanceId < right->instanceId;
            });
        return candidates.front()->instanceId;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<AssetInstanceId> selectQuickMedkit(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    try
    {
        const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
        if (!chest.has_value())
        {
            return std::nullopt;
        }
        const ItemDefinition &chestDefinition =
            content.item(profile.assets.find(*chest)->definitionId);
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
                if (content.item(asset->definitionId).category ==
                        ItemCategory::Medical &&
                    content.item(asset->definitionId).medicalUse.has_value() &&
                    content.item(asset->definitionId).medicalUse->effect ==
                        MedicalItemEffect::RestoreHealth &&
                    asset->remainingCharges > 0)
                {
                    return asset->instanceId;
                }
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<ProfileContainerId> selectRaidMagazineUnloadDestination(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId magazineAssetId) noexcept
{
    if (!assetIsCarried(profile, magazineAssetId))
    {
        return std::nullopt;
    }
    try
    {
        // Loose ammunition prefers the backpack. General chest-rig pockets
        // remain a deterministic fallback when no backpack placement fits.
        for (EquipmentSlotKind slot : {
                 EquipmentSlotKind::Backpack,
                 EquipmentSlotKind::ChestRig})
        {
            const auto containerAssetId = equippedAsset(profile, slot);
            if (!containerAssetId.has_value())
            {
                continue;
            }
            const AssetRecord *container = profile.assets.find(*containerAssetId);
            if (container == nullptr)
            {
                continue;
            }
            const ItemDefinition &definition = content.item(container->definitionId);
            for (std::size_t index = 0;
                 index < definition.containerCompartments.size();
                 ++index)
            {
                if (definition.containerCompartments[index].pocketKind !=
                    ContainerPocketKind::General)
                {
                    continue;
                }
                const ProfileContainerId destination =
                    ProfileContainerId::compartment(
                        *containerAssetId,
                        static_cast<std::uint32_t>(index));
                if (queryWeaponAmmo(
                        profile,
                        content,
                        UnloadMagazineCommand{
                            magazineAssetId,
                            destination}).canCommit)
                {
                    return destination;
                }
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

HealReceipt executeHeal(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId medkitAssetId,
    HealAccess access,
    const CommandContext &context)
{
    const auto fail = [&profile](DomainErrorCode error, std::string message)
    {
        return HealReceipt{false, false, error, std::move(message),
                           profile.revision, 0};
    };
    if (context.transactionId.empty())
        return fail(DomainErrorCode::InvalidTransaction,
                    "transaction ID must not be empty");
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision, 0};
    if (context.expectedRevision != profile.revision)
        return fail(DomainErrorCode::StaleRevision, "profile revision is stale");
    if (profile.currentHealth >= 100)
        return fail(DomainErrorCode::InvalidQuantity, "health is already full");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return fail(DomainErrorCode::RevisionOverflow,
                    "profile revision cannot advance");

    ProfileState candidate = profile;
    AssetRecord *medkit = candidate.assets.findMutable(medkitAssetId);
    if (medkit == nullptr)
        return fail(DomainErrorCode::MissingAsset, "Medkit does not exist");
    const ItemDefinition &definition = content.item(medkit->definitionId);
    if (access == HealAccess::CarriedOnly &&
        !assetIsCarried(candidate, medkitAssetId))
        return fail(DomainErrorCode::IllegalDestination,
                    "Medkit is not carried into the Raid");
    if (definition.category != ItemCategory::Medical ||
        medkit->remainingCharges == 0)
        return fail(DomainErrorCode::InvalidQuantity,
                    "Medkit has no usable charge");

    const int healed = std::min(30, 100 - candidate.currentHealth);
    candidate.currentHealth += healed;
    --medkit->remainingCharges;
    if (medkit->remainingCharges == 0)
    {
        static_cast<void>(candidate.assets.erase(medkitAssetId));
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
        return fail(DomainErrorCode::InvalidProfile, validation.message);
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision, healed};
}
