#include "profile_ui_command.h"

#include "base_wish_expedition.h"
#include "base_world.h"
#include "collision.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
template<class Receipt>
Receipt reject(ProfileRevision revision, DomainErrorCode error, std::string message)
{
    Receipt receipt;
    receipt.succeeded = false;
    if constexpr (requires { receipt.error; }) receipt.error = error;
    if constexpr (requires { receipt.revision; }) receipt.revision = revision;
    receipt.message = std::move(message);
    return receipt;
}
template<class Receipt>
ProfileUiCommandResult resultOf(Receipt receipt, ProfileUiPresentationFacts facts = {},
                                std::optional<AssetInstanceId> affected = {})
{
    return {ProfileUiReceipt{std::move(receipt)}, affected, std::move(facts)};
}

// This is the same physical repair gate used by the synchronous service. A
// domain repair is executed only after the current world permits the footprint.
bool fortificationRepairClear(const ProfileState &profile, const ContentRegistry &content,
                              const BaseWorld &world, FortificationInstanceId id)
{
    const auto plan = queryFortificationCommand(profile, content, RepairFortificationCommand{id});
    if (!plan.canCommit) return true; // Let the domain return its exact rejection.
    const auto &record = profile.baseFortifications.instances.at(id);
    if (!record.slot) return true;
    const auto *definition = content.findFortification(record.definition);
    if (!definition) return false;
    const auto candidates = baseDefensePositionCandidates(world.layout(), world.plotId(), *definition);
    const auto side = static_cast<std::size_t>(record.slot->side);
    if (side >= candidates.size()) return false;
    const auto &position = candidates[side];
    const Rect footprint{position.footprint.position, position.footprint.size};
    if (position.key != *record.slot || !position.available ||
        isCollision(footprint, {world.playerPosition(), world.playerSize()})) return false;
    for (const auto &enemy : world.perimeterEnemies())
        if (isCollision(footprint, enemy.bounds())) return false;
    for (const auto &facility : world.facilities())
        if (facility.active && isCollision(footprint, facility.bounds)) return false;
    for (const auto &bounds : world.layout().movementBlockers)
        if (isCollision(footprint, {bounds.position, bounds.size})) return false;
    for (const auto &asset : projectBaseGroundAssets(profile, record.slot->site))
    {
        const auto &item = content.item(asset.definitionId);
        Vec2 size = item.basePlacement ? item.basePlacement->footprint : item.worldRenderSize;
        size.x = std::max(size.x, item.pickupSize.x);
        size.y = std::max(size.y, item.pickupSize.y);
        if (asset.orientation == ItemOrientation::Degrees90 || asset.orientation == ItemOrientation::Degrees270)
            std::swap(size.x, size.y);
        if (isCollision(footprint, {{asset.position.x - size.x / 2, asset.position.y - size.y / 2}, size})) return false;
    }
    return true;
}
bool unfinishedPhysicalEffects(const ProfileState &profile)
{
    if (profile.baseReloadRecovery) return true;
    if (!profile.basePlayerEffects) return false;
    const auto &effects = *profile.basePlayerEffects;
    return effects.grenade.flight || effects.grenade.fuse || effects.grenade.explosion ||
           effects.noiseDecoy.flight || effects.noiseDecoy.source;
}
}

bool profileUiReceiptSucceeded(const ProfileUiReceipt &receipt) noexcept
{
    return std::visit([](const auto &value) { return value.succeeded; }, receipt);
}
bool profileUiReceiptAlreadyCommitted(const ProfileUiReceipt &receipt) noexcept
{
    return std::visit([](const auto &value) {
        if constexpr (requires { value.alreadyCommitted; }) return value.alreadyCommitted;
        else if constexpr (requires { value.idempotent; }) return value.idempotent;
        else return false;
    }, receipt);
}
DomainErrorCode profileUiReceiptError(const ProfileUiReceipt &receipt) noexcept
{
    return std::visit([](const auto &value) {
        if constexpr (requires { value.error; }) return value.error;
        else return value.succeeded ? DomainErrorCode::None : DomainErrorCode::IllegalDestination;
    }, receipt);
}
const std::string &profileUiReceiptMessage(const ProfileUiReceipt &receipt) noexcept
{
    return std::visit([](const auto &value) -> const std::string & { return value.message; }, receipt);
}
ProfileRevision profileUiReceiptRevision(const ProfileUiReceipt &receipt) noexcept
{
    return std::visit([](const auto &value) -> ProfileRevision {
        if constexpr (requires { value.revision; }) return value.revision;
        else return 0;
    }, receipt);
}

ProfileUiPrepared prepareProfileUiCommand(ProfileState &candidate, const ContentRegistry &content,
    const ProfileUiCommand &command, const CommandContext &context, const BaseWorld *world)
{
    ProfileUiPrepared prepared;
    try
    {
        const bool admissionWasEnabled = candidate.residentMedicalReservePolicy.automaticAdmission;
        // All alternatives are Base operations. The activity owner performs
        // its boundary barrier before handing this isolated candidate here.
        if (candidate.pendingRaid)
            prepared.result = resultOf(reject<InventoryReceipt>(candidate.revision,
                DomainErrorCode::IllegalDestination, "Base command is unavailable during a Raid"));
        else prepared.result = std::visit([&](const auto &typed) -> ProfileUiCommandResult {
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, EconomyCommand>)
                return resultOf(executeEconomy(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, BaseRestCommand>)
            {
                auto receipt = executeBaseRest(candidate, content, typed, context);
                ProfileUiPresentationFacts facts;
                if (receipt.succeeded && !receipt.alreadyCommitted) {
                    facts.restCompletion = receipt;
                    facts.resetWorldClockAccumulator = true;
                }
                return resultOf(std::move(receipt), std::move(facts));
            }
            else if constexpr (std::is_same_v<Command, ContributeBaseAssetCommand>)
                return resultOf(executeBaseResourceContribution(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, SetBaseSupplyAssignmentCommand>)
                return resultOf(executeBaseSupplyAssignment(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, ContributeConstructionMaterialCommand>)
                return resultOf(executeConstructionMaterialContribution(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, StartBaseConstructionCommand>)
                return resultOf(executeStartBaseConstruction(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, CancelBaseConstructionCommand>)
                return resultOf(executeCancelBaseConstruction(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, BaseMigrationCommand>)
            {
                if (unfinishedPhysicalEffects(candidate))
                    return resultOf(reject<BaseMigrationReceipt>(candidate.revision, DomainErrorCode::IllegalDestination,
                        "Finish the current weapon or throwable activity before moving Base"));
                candidate.basePlayerEffects.reset();
                auto receipt = executeBaseMigration(candidate, content, typed, context);
                ProfileUiPresentationFacts facts;
                if (receipt.succeeded && !receipt.alreadyCommitted)
                    facts.resetWorldClockAccumulator = facts.synchronizeBaseSite = true;
                return resultOf(std::move(receipt), facts);
            }
            else if constexpr (std::is_same_v<Command, BaseSiteFeatureRepairCommand>)
            {
                auto receipt = executeBaseSiteFeatureRepair(candidate, content, typed, context);
                ProfileUiPresentationFacts facts;
                facts.resetWorldClockAccumulator = receipt.succeeded && !receipt.alreadyCommitted;
                return resultOf(std::move(receipt), facts);
            }
            else if constexpr (std::is_same_v<Command, InstallBaseFacilityCommand>)
                return resultOf(executeInstallBaseFacility(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, EstablishRegionalOutpostCommand>)
                return resultOf(executeEstablishRegionalOutpost(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, RegionalOutpostStaffingCommand>)
                return resultOf(executeRegionalOutpostStaffing(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, SubmitBasePriorityCommand>)
                return resultOf(executeBasePrioritySubmission(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, StartBaseManufacturingCommand>)
                return resultOf(executeStartBaseManufacturing(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, StartBaseEmergencyPowerCommand> ||
                               std::is_same_v<Command, PauseBaseEmergencyPowerCommand> ||
                               std::is_same_v<Command, ResumeBaseEmergencyPowerCommand>)
                return resultOf(executeBaseEmergencyPower(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, BaseMedicalServiceCommand>)
            {
                auto receipt = executeBaseMedicalService(candidate, content, typed, context);
                ProfileUiPresentationFacts facts; facts.medicalCompleted = receipt.succeeded && !receipt.idempotent;
                return resultOf(std::move(receipt), facts);
            }
            else if constexpr (std::is_same_v<Command, StartResidentTreatmentCommand>)
                return resultOf(executeStartResidentTreatment(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, MoveMedicalReserveAssetCommand>)
                return resultOf(executeMoveMedicalReserveAsset(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, SetResidentMedicalReservePolicyCommand>)
                return resultOf(executeSetResidentMedicalReservePolicy(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, StartGunsmithMaintenanceCommand>)
            {
                auto receipt = executeGunsmithMaintenance(candidate, content, typed, context);
                const auto id = receipt.succeeded ? std::optional{receipt.weaponAssetId} : std::nullopt;
                return resultOf(std::move(receipt), {}, id);
            }
            else if constexpr (std::is_same_v<Command, RaidIntelligencePurchaseCommand>)
                return resultOf(executeRaidIntelligencePurchase(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, RaidInteriorIntelligencePurchaseCommand>)
                return resultOf(executeRaidInteriorIntelligencePurchase(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, BaseGroundCommand>)
            {
                auto receipt = executeBaseGround(candidate, content, typed, context);
                const auto id = receipt.succeeded ? std::optional{receipt.affectedAssetId} : std::nullopt;
                return resultOf(std::move(receipt), {}, id);
            }
            else if constexpr (std::is_same_v<Command, RepositionBaseFacilityCommand>)
                return resultOf(executeBaseFacilityLayout(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, InstallBaseFacilityAtCommand>)
                return resultOf(executeInstallBaseFacilityAt(candidate, content, typed, context));
            else if constexpr (std::is_same_v<Command, ProfileUiAssignWorkerCommand>)
                return resultOf(executeAssignBestBaseWorker(candidate, content, {typed.facility}, context));
            else if constexpr (std::is_same_v<Command, ProfileUiClearWorkerCommand>)
                return resultOf(executeClearBaseWorker(candidate, content, {typed.facility}, context));
            else if constexpr (std::is_same_v<Command, ProfileUiAutoFillWorkersCommand>)
                return resultOf(executeAutoFillBaseWorkers(candidate, content, context));
            else if constexpr (std::is_same_v<Command, ProfileUiWishFocusCommand>)
                return resultOf(executeBaseWishFocus(candidate, content, typed.focus, context));
            else if constexpr (std::is_same_v<Command, ProfileUiCancelManufacturingCommand>)
                return resultOf(executeCancelBaseManufacturing(candidate, content, context));
            else if constexpr (std::is_same_v<Command, ProfileUiCollectManufacturingCommand>)
            {
                auto receipt = executeCollectBaseManufacturing(candidate, content, context);
                const auto id = receipt.succeeded ? receipt.outputAssetId : std::nullopt;
                return resultOf(std::move(receipt), {}, id);
            }
            else if constexpr (std::is_same_v<Command, ProfileUiCollectGunsmithCommand>)
            {
                auto receipt = executeGunsmithCollection(candidate, content, context);
                const auto id = receipt.succeeded ? std::optional{receipt.weaponAssetId} : std::nullopt;
                return resultOf(std::move(receipt), {}, id);
            }
            else if constexpr (std::is_same_v<Command, ProfileUiStartRecoveryCommand>)
                return resultOf(executeStartRecoveryTask(candidate, content, typed.recordId, context));
            else if constexpr (std::is_same_v<Command, ProfileUiCancelRecoveryCommand>)
                return resultOf(executeCancelRecoveryTask(candidate, content, context));
            else if constexpr (std::is_same_v<Command, ProfileUiCollectRecoveryCommand>)
                return resultOf(executeCollectRecoveryTask(candidate, content, context));
            else if constexpr (std::is_same_v<Command, ProfileUiAutoDefenseCommand>)
                return resultOf(executeBaseAutoDefense(candidate, content, context));
            else if constexpr (std::is_same_v<Command, ProfileUiPlaceablePurchaseCommand>)
            {
                const auto &definition = content.item(typed.definitionId);
                if (!definition.basePlacement || !definition.marketBuyPrice || definition.maxStackSize != 1U)
                    return resultOf(reject<ProfileUiPlaceablePurchaseReceipt>(candidate.revision,
                        DomainErrorCode::IllegalDestination, "definition is not available in the Base build catalog"));
                const auto id = candidate.assets.nextAssetId();
                const auto receipt = executeEconomy(candidate, content, EconomyCommand{PurchaseCommand{typed.definitionId, 1}}, context);
                ProfileUiPlaceablePurchaseReceipt result{receipt.succeeded, receipt.error, receipt.message,
                    receipt.revision, 0, receipt.currencyDelta, receipt.alreadyCommitted};
                if (receipt.succeeded && !receipt.alreadyCommitted) {
                    const auto *created = candidate.assets.find(id);
                    if (!created || created->definitionId != typed.definitionId)
                        return resultOf(reject<ProfileUiPlaceablePurchaseReceipt>(candidate.revision,
                            DomainErrorCode::InvalidProfile, "Base placeable purchase did not create the expected asset"));
                    result.assetId = id;
                }
                return resultOf(result, {}, result.assetId ? std::optional{result.assetId} : std::nullopt);
            }
            else if constexpr (std::is_same_v<Command, WeaponAmmoCommand>)
            {
                const auto weaponId = std::visit([](const auto &value) -> std::optional<AssetInstanceId> {
                    if constexpr (requires { value.weaponAssetId; }) return value.weaponAssetId;
                    else return {};
                }, typed);
                const auto oldMagazine = weaponId ? installedMagazine(candidate, *weaponId) : std::nullopt;
                const auto *oldWeapon = weaponId ? candidate.assets.find(*weaponId) : nullptr;
                const bool oldChamber = oldWeapon && oldWeapon->chamberedRound.has_value();
                const auto oldMode = oldWeapon ? oldWeapon->fireMode : std::nullopt;
                auto receipt = executeWeaponAmmo(candidate, content, typed, context);
                ProfileUiPresentationFacts facts;
                if (receipt.succeeded && !receipt.alreadyCommitted && weaponId) {
                    const auto newMagazine = installedMagazine(candidate, *weaponId);
                    const auto *now = candidate.assets.find(*weaponId);
                    facts.weaponMagazineRemoved = oldMagazine != newMagazine && oldMagazine.has_value();
                    facts.weaponMagazineInserted = oldMagazine != newMagazine && newMagazine.has_value();
                    facts.weaponChambered = !oldChamber && now && now->chamberedRound.has_value();
                    facts.releaseWeaponTrigger = now && now->fireMode != oldMode;
                }
                return resultOf(std::move(receipt), facts, weaponId);
            }
            else if constexpr (std::is_same_v<Command, WeaponMaintenanceCommand>)
            {
                auto command = typed; command.access = MaintenanceAccess::AnyOwned; command.location = MaintenanceLocation::Base;
                return resultOf(executeWeaponMaintenance(candidate, content, command, context), {}, typed.weaponAssetId);
            }
            else if constexpr (std::is_same_v<Command, ArmorMaintenanceCommand>)
            {
                auto command = typed; command.access = MaintenanceAccess::AnyOwned; command.location = MaintenanceLocation::Base;
                return resultOf(executeArmorMaintenance(candidate, content, command, context), {}, typed.armorAssetId);
            }
            else if constexpr (std::is_same_v<Command, WeaponComponentCommand>)
            {
                auto receipt = executeWeaponComponentChange(candidate, content, typed, context);
                ProfileUiPresentationFacts facts;
                if (receipt.succeeded && !receipt.alreadyCommitted)
                    facts.resetWeaponConfiguration = facts.clearWeaponOverrides = true;
                return resultOf(std::move(receipt), facts, typed.weaponAssetId);
            }
            else if constexpr (std::is_same_v<Command, HelmetDeviceCommand>)
            {
                if (!world || !world->canAccessStash())
                    return resultOf(reject<InventoryReceipt>(candidate.revision,
                        DomainErrorCode::IllegalDestination, "HELMET FITTING NEEDS BASE STORAGE"));
                return resultOf(executeHelmetDeviceChange(candidate, content, typed, context), {}, typed.helmetAssetId);
            }
            else if constexpr (std::is_same_v<Command, DeviceBatteryCommand>)
            {
                if(!world) return resultOf(reject<InventoryReceipt>(candidate.revision,
                    DomainErrorCode::IllegalDestination,"Battery change needs the current Base world"));
                const auto site=RegionalBaseSiteDefinitionId{world->siteDefinitionId()};
                const auto activity=candidate.activeBaseDefense?WeaponActionActivity::BaseDefense:WeaponActionActivity::BaseDaily;
                const WeaponActionScope scope{activity,site,
                    std::string{candidate.activeBaseDefense?candidate.activeBaseDefense->eventId:site.value()}};
                const DeviceBatteryAccess access{false,std::nullopt,scope,
                    activity==WeaponActionActivity::BaseDaily && world->canAccessStash()};
                return resultOf(executeDeviceBatteryChange(candidate,content,typed,access,context),{},typed.deviceAssetId);
            }
            else if constexpr (std::is_same_v<Command, FortificationCommand>)
            {
                if (const auto *repair = std::get_if<RepairFortificationCommand>(&typed)) {
                    if (!world || !fortificationRepairClear(candidate, content, *world, repair->instance))
                        return resultOf(reject<FortificationReceipt>(candidate.revision, DomainErrorCode::IllegalDestination,
                            "Clear the barricade footprint before repair"));
                }
                return resultOf(executeFortificationCommand(candidate, content, typed, context));
            }
            else if constexpr (std::is_same_v<Command, InstallFortificationCommand>)
            {
                if (!world) return resultOf(reject<FortificationReceipt>(candidate.revision,
                    DomainErrorCode::IllegalDestination, "Fortification installation needs the current Base world"));
                return resultOf(executeFortificationPlacement(candidate, content, *world, typed, context));
            }
            else if constexpr (std::is_same_v<Command, ProfileUiBaseMedicalCommand>)
            {
                auto receipt = executeMedicalUse(candidate, content, typed.assetId, MedicalAccess::AnyOwned, context);
                ProfileUiPresentationFacts facts; facts.medicalCompleted = receipt.succeeded && !receipt.idempotent;
                return resultOf(std::move(receipt), facts, typed.assetId);
            }
            else if constexpr (std::is_same_v<Command, ProfileUiBaseHealCommand>)
            {
                auto receipt = executeHeal(candidate, content, typed.assetId, HealAccess::AnyOwned, context);
                ProfileUiPresentationFacts facts; facts.medicalCompleted = receipt.succeeded && !receipt.alreadyCommitted;
                return resultOf(std::move(receipt), facts, typed.assetId);
            }
            else if constexpr (std::is_same_v<Command, ProfileUiEstablishHomeCommand>)
            {
                if (!world || world->siteDefinitionId() != typed.region.value())
                    return resultOf(reject<HomeFoundingReceipt>(candidate.revision,
                        DomainErrorCode::IllegalDestination, "Home founding needs the current Base world"));
                const Vec2 center{world->playerPosition().x + world->playerSize().x / 2,
                                  world->playerPosition().y + world->playerSize().y / 2};
                auto receipt = executeHomeFounding(candidate, content, typed.plotId, typed.region, center, context);
                ProfileUiPresentationFacts facts; facts.synchronizeBaseSite = receipt.succeeded && !receipt.alreadyCommitted;
                return resultOf(std::move(receipt), facts);
            }
            else if constexpr (std::is_same_v<Command, ProfileUiHintsCommand>)
            {
                if (context.transactionId.empty()) return resultOf(reject<InventoryReceipt>(candidate.revision,
                    DomainErrorCode::InvalidTransaction, "transaction ID must not be empty"));
                if (candidate.committedTransactions.contains(context.transactionId))
                    return resultOf(InventoryReceipt{true,true,DomainErrorCode::None,{},candidate.revision});
                if (context.expectedRevision != candidate.revision) return resultOf(reject<InventoryReceipt>(candidate.revision,
                    DomainErrorCode::StaleRevision, "profile revision is stale"));
                if (typed.finishFirstRaid && (!candidate.lastRaidResult ||
                    candidate.lastRaidResult->outcome == RaidResultOutcome::AbnormalQuit ||
                    !candidate.committedSettlements.contains(candidate.lastRaidResult->settlementId)))
                    return resultOf(reject<InventoryReceipt>(candidate.revision, DomainErrorCode::IllegalDestination,
                        "First-Raid hints require a committed Raid result"));
                if (candidate.homeFounding.hintsDismissed && (!typed.finishFirstRaid || candidate.tutorial == TutorialProgress::Complete))
                    return resultOf(InventoryReceipt{true,true,DomainErrorCode::None,{},candidate.revision});
                if (candidate.revision == std::numeric_limits<ProfileRevision>::max())
                    return resultOf(reject<InventoryReceipt>(candidate.revision, DomainErrorCode::RevisionOverflow,
                        "profile revision cannot advance"));
                candidate.committedTransactions.insert(context.transactionId);
                candidate.homeFounding.hintsDismissed = true;
                if (typed.finishFirstRaid) candidate.tutorial = TutorialProgress::Complete;
                ++candidate.revision;
                return resultOf(InventoryReceipt{true,false,DomainErrorCode::None,{},candidate.revision});
            }
            else static_assert(!sizeof(Command), "unhandled Profile UI command");
        }, command);
        if (world && world->playerSafetyZone() == HomeRegionSafetyZone::SafeCore &&
            world->siteDefinitionId() == candidate.regionalOperations.technologyCore.baseSiteDefinitionId.value() &&
            profileUiReceiptSucceeded(prepared.result.receipt) && !profileUiReceiptAlreadyCommitted(prepared.result.receipt))
        {
            const auto *move = std::get_if<MoveMedicalReserveAssetCommand>(&command);
            const auto *assign = std::get_if<ProfileUiAssignWorkerCommand>(&command);
            const bool enablesAdmission = !admissionWasEnabled &&
                std::holds_alternative<SetResidentMedicalReservePolicyCommand>(command);
            if (enablesAdmission || (move && move->destination.container == ProfileContainerId::medicalReserve()) ||
                (assign && assign->facility == BaseFacilityStaffingKind::Medical) ||
                std::holds_alternative<ProfileUiAutoFillWorkersCommand>(command) ||
                std::holds_alternative<BaseRestCommand>(command) ||
                std::holds_alternative<BaseSiteFeatureRepairCommand>(command))
            {
                if (tryStartAutomaticResidentTreatment(candidate, content))
                    std::visit([&](auto &receipt) {
                        if constexpr (requires { receipt.revision; }) receipt.revision = candidate.revision;
                    }, prepared.result.receipt);
            }
        }
    }
    catch (const std::exception &error)
    {
        prepared.result = resultOf(reject<InventoryReceipt>(context.expectedRevision,
            DomainErrorCode::InvalidProfile, error.what()));
    }
    catch (...)
    {
        prepared.result = resultOf(reject<InventoryReceipt>(context.expectedRevision,
            DomainErrorCode::InvalidProfile, "Profile UI command could not be prepared"));
    }
    prepared.canCommit = profileUiReceiptSucceeded(prepared.result.receipt);
    prepared.error = profileUiReceiptError(prepared.result.receipt);
    prepared.message = profileUiReceiptMessage(prepared.result.receipt);
    if (!prepared.canCommit || profileUiReceiptAlreadyCommitted(prepared.result.receipt))
        prepared.result.facts = {};
    return prepared;
}
