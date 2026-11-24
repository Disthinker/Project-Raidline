#include <gtest/gtest.h>

#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"
#include "raid_lifecycle.h"
#include "raid_squad_domain.h"
#include "save_repository.h"

namespace
{
class RaidSquadDomainTest : public testing::Test
{
protected:
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("squad-domain",content);
    AssetInstanceId add(const ItemDefinitionId &id, unsigned quantity = 1U)
    {
        const auto &definition = content.item(id);
        const auto stash = ProfileContainerId::stash();
        const auto origin = findFirstProfileFit(profile,content,stash,definition,ItemOrientation::Degrees0);
        EXPECT_TRUE(origin);
        return origin ? profile.assets.create(definition,StoredAssetLocation{stash,*origin},quantity) : 0U;
    }
    void recruit(RaidSquadRole role, const std::string &id)
    {
        const auto &map = role == RaidSquadRole::Support ? kRaidFireSupportMapId : kRaidFireSupportReplacementMapId;
        DeployCommand deploy{id + "-raid",id + "-settle",8847U,map};
        deploy.specialistRescue = role;
        auto receipt = executeDeploy(profile,content,deploy,{profile.revision,id + "-deploy"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        const auto count = profile.basePopulation.ordinaryResidents;
        const auto tx = raidSquadSourceId(deploy.settlementId,role) + ":admission";
        auto accepted = executeRaidSpecialistAdmission(profile,content,role,{profile.revision,tx});
        ASSERT_TRUE(accepted.succeeded) << accepted.message;
        EXPECT_TRUE(executeRaidSpecialistAdmission(profile,content,role,{profile.revision,tx}).alreadyCommitted);
        const auto view = queryRaidSpecialistAdmission(profile,map,role);
        EXPECT_TRUE(view.alreadyCommitted);
        EXPECT_EQ(view.residentsBefore,view.residentsAfter);
        EXPECT_EQ(profile.basePopulation.ordinaryResidents,count + 1U);
        auto done = settlePendingRaid(profile,content,deploy.settlementId,RaidResultOutcome::Extracted);
        ASSERT_TRUE(done.succeeded) << done.message;
        EXPECT_EQ(profile.lastRaidResult->rescuedOrdinaryResidents,1U);
    }
};

TEST_F(RaidSquadDomainTest, IndependentRecruitmentRetriesWithoutChangingOrdinaryRescue)
{
    const auto normal = content.map(kRaidFireSupportMapId).rescue->id;
    profile.committedRescues.insert(normal);
    const auto before = profile.basePopulation.ordinaryResidents;
    DeployCommand decline{"decline-raid","decline-settle",8847U,kRaidFireSupportMapId};
    decline.specialistRescue = RaidSquadRole::Support;
    const auto started = executeDeploy(profile,content,decline,{profile.revision,"decline-deploy"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(settlePendingRaid(profile,content,decline.settlementId,RaidResultOutcome::ActiveQuit).succeeded);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents,before);
    EXPECT_TRUE(canRecruitRaidSpecialist(profile,kRaidFireSupportMapId,RaidSquadRole::Support));
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"retry"));
    EXPECT_TRUE(profile.committedRescues.contains(normal));
    EXPECT_FALSE(canRecruitRaidSpecialist(profile,kRaidFireSupportMapId,RaidSquadRole::Support));
    EXPECT_TRUE(canRecruitRaidSpecialist(profile,kRaidFireSupportReplacementMapId,RaidSquadRole::Sniper));
    EXPECT_FALSE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Fire).residentAvailable);
}

TEST_F(RaidSquadDomainTest, RealLoansPerMemberSettleAcrossMixedCasualtiesAndReplacement)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"medic"));
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Sniper,"scout"));
    const auto gun = add(kRaidSquadSupportWeaponId);
    const auto rifle = add(kRaidSquadSniperWeaponId);
    const auto kit = add(ItemDefinitionId{"item.medical.medkit_alpha"});
    add(kRaidSquadSupportAmmoId,30U);
    add(kRaidSquadSniperAmmoId,20U);
    const auto beforePistolAmmo = queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Support).preparationRounds;
    const auto beforeRifleAmmo = queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Sniper).preparationRounds;
    const auto gunOrigin = profile.assets.find(gun)->location;
    const auto charges = profile.assets.find(kit)->remainingCharges;
    DeployCommand command{"mixed-raid","mixed-settle",8847U,kHospitalFloorplanMapId};
    command.squad = {{RaidSquadRole::Sniper,rifle,{}},{RaidSquadRole::Support,gun,{kit}}};
    const auto receipt = executeDeploy(profile,content,command,{profile.revision,"mixed-deploy"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.pendingRaid->squadOrder,(std::vector<RaidSquadRole>{RaidSquadRole::Sniper,RaidSquadRole::Support}));
    EXPECT_EQ(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Support).preparationRounds,beforePistolAmmo - 30U);
    EXPECT_EQ(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Sniper).preparationRounds,beforeRifleAmmo - 20U);
    const CommandContext dose{profile.revision,"dose-1"};
    auto treated = consumeRaidSquadMedical(profile,content,kit,dose);
    ASSERT_TRUE(treated.succeeded) << treated.message;
    EXPECT_TRUE(consumeRaidSquadMedical(profile,content,kit,dose).alreadyCommitted);
    setRaidSquadHealthInSimulation(profile,RaidSquadRole::Support,49);
    ASSERT_TRUE(revealRaidSquadCorpseInSimulation(profile,content,RaidSquadRole::Sniper,outdoorRaidSpaceId(),profile.pendingRaid->playerSpawn));
    const auto result = settlePendingRaid(profile,content,command.settlementId,RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(profile.assets.find(gun)->location,gunOrigin);
    if (charges > 1U) { ASSERT_TRUE(profile.assets.find(kit)); EXPECT_EQ(profile.assets.find(kit)->remainingCharges,charges - 1U); }
    else EXPECT_FALSE(profile.assets.find(kit));
    ASSERT_TRUE(profile.lastRaidResult->squadResults.at(RaidSquadRole::Sniper).lostRecordId);
    const auto record = *profile.lastRaidResult->squadResults.at(RaidSquadRole::Sniper).lostRecordId;
    EXPECT_EQ(profile.lostRaidRecords.at(record).kind,LostRaidRecordKind::SpecialistSniper);
    EXPECT_EQ(std::get<LostRaidAssetLocation>(profile.assets.find(rifle)->location).recordId,record);
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Support).status,RaidSquadResidentStatus::Wounded);
    EXPECT_FALSE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Support).residentAvailable);
    EXPECT_TRUE(settlePendingRaid(profile,content,command.settlementId,RaidResultOutcome::PlayerDead).alreadyCommitted);
    const auto ready = profile.squadRoster.at(RaidSquadRole::Sniper).replacementReadyMinute;
    ASSERT_EQ(ready,profile.worldClock.elapsedWorldMinutes + 72U*60U);
    profile.worldClock.elapsedWorldMinutes = ready - 1U;
    EXPECT_FALSE(canRecruitRaidSpecialist(profile,kRaidFireSupportReplacementMapId,RaidSquadRole::Sniper));
    ++profile.worldClock.elapsedWorldMinutes;
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Sniper,"successor"));
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Sniper).generation,2U);
    EXPECT_EQ(std::get<LostRaidAssetLocation>(profile.assets.find(rifle)->location).recordId,record);
}

TEST_F(RaidSquadDomainTest, NamedMedicalPatientReservesOneBedAndRecoversAfterTheFullJob)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"medic"));
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Sniper,"scout"));
    const auto pistol = add(kRaidSquadSupportWeaponId);
    const auto rifle = add(kRaidSquadSniperWeaponId);
    add(kRaidSquadSupportAmmoId,30U); add(kRaidSquadSniperAmmoId,20U);
    DeployCommand deploy{"wound-raid","wound-settle",8847U,kRaidFireSupportMapId};
    deploy.squad = {{RaidSquadRole::Support,pistol,{}},{RaidSquadRole::Sniper,rifle,{}}};
    const auto deployed = executeDeploy(profile,content,deploy,{profile.revision,"wound-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    setRaidSquadHealthInSimulation(profile,RaidSquadRole::Support,25);
    setRaidSquadHealthInSimulation(profile,RaidSquadRole::Sniper,49);
    const auto settled = settlePendingRaid(profile,content,deploy.settlementId,RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    const auto stock = profile.assets.create(content.item(kHospitalFirstAidStockId),
        StoredAssetLocation{ProfileContainerId::medicalReserve(),{0,0}});
    StartResidentTreatmentCommand chosen;
    chosen.squadPatient = RaidSquadRole::Sniper;
    const auto treatment = executeStartResidentTreatment(profile,content,chosen,{profile.revision,"treat-sniper"});
    ASSERT_TRUE(treatment.succeeded) << treatment.message;
    ASSERT_TRUE(profile.residentMedical.activeTreatment);
    EXPECT_EQ(profile.residentMedical.activeTreatment->squadPatient,RaidSquadRole::Sniper);
    EXPECT_FALSE(profile.assets.find(stock));
    const auto job = *profile.residentMedical.activeTreatment;
    auto atClinic = deserializeProfileEnvelope(serializeProfileEnvelope(profile,content.contentVersion()),content);
    ASSERT_TRUE(atClinic.profile) << atClinic.message;
    EXPECT_EQ(atClinic.profile->residentMedical.activeTreatment,job);
    EXPECT_FALSE(atClinic.profile->assets.find(stock));
    EXPECT_EQ(profileStateFingerprint(*atClinic.profile),profileStateFingerprint(profile));
    profile = std::move(*atClinic.profile);
    EXPECT_EQ(projectBaseWorkforce(profile).availableByProfession[baseProfessionIndex(BaseResidentProfession::Combat)],0U);
    const auto until = treatment.completionWorldMinute - profile.worldClock.elapsedWorldMinutes;
    ASSERT_GT(until,0U);
    static_cast<void>(advanceWorldClock(profile.worldClock,static_cast<unsigned>(until - 1U)));
    EXPECT_FALSE(applyResidentTreatmentThrough(profile).completed);
    DeployCommand solo{"pass-time","pass-time-settle",8847U,kRaidFireSupportReplacementMapId};
    const auto travelled = executeDeploy(profile,content,solo,{profile.revision,"pass-time-deploy"});
    ASSERT_TRUE(travelled.succeeded) << travelled.message;
    ASSERT_FALSE(profile.residentMedical.activeTreatment);
    auto onExpedition = deserializeProfileEnvelope(serializeProfileEnvelope(profile,content.contentVersion()),content);
    ASSERT_TRUE(onExpedition.profile) << onExpedition.message;
    ASSERT_TRUE(onExpedition.profile->pendingRaid);
    EXPECT_EQ(onExpedition.profile->pendingRaid->travel.startingResidentMedical.activeTreatment,job);
    EXPECT_FALSE(onExpedition.profile->residentMedical.activeTreatment);
    EXPECT_FALSE(onExpedition.profile->assets.find(stock));
    EXPECT_EQ(profileStateFingerprint(*onExpedition.profile),profileStateFingerprint(profile));
    profile = std::move(*onExpedition.profile);
    const auto rollback = rollbackPendingRaidToBase(profile,content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    ASSERT_TRUE(profile.residentMedical.activeTreatment);
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Sniper).status,RaidSquadResidentStatus::Wounded);
    static_cast<void>(advanceWorldClock(profile.worldClock,1U));
    ASSERT_TRUE(applyResidentTreatmentThrough(profile).completed);
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Sniper).status,RaidSquadResidentStatus::Ready);
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Support).status,RaidSquadResidentStatus::Wounded);
    EXPECT_TRUE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Sniper).residentAvailable);
    EXPECT_FALSE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Support).residentAvailable);
    EXPECT_EQ(profile.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::Combat)],1U);
    ASSERT_TRUE(validateProfileState(profile,content).valid);
}

TEST_F(RaidSquadDomainTest, ClaimedCargoFollowsPlayerAndUnclaimedCargoKeepsIndividualLossOwner)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"medic"));
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::Combat)];
    profile.committedRescues.insert(kRaidFireSupportReplacementRescueId);
    const auto pack = profile.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto rifle = add(kRaidFireSupportRifleId);
    const auto gun = add(kRaidSquadSupportWeaponId);
    const auto claimed = add(ItemDefinitionId{"item.magazine.5_45x39_30"});
    const auto unopened = add(kRaidSquadSupportAmmoId,3U);
    const auto first = add(ItemDefinitionId{"item.medical.bandage_basic"});
    const auto second = add(ItemDefinitionId{"item.medical.medkit_alpha"});
    add(kRaidSquadSupportAmmoId,30U); add(kRaidFireSupportAmmunitionId,30U);
    DeployCommand deploy{"two-loss","two-loss-settle",8847U,kRaidFireSupportMapId};
    deploy.squad = {{RaidSquadRole::Fire,rifle,{claimed,unopened}},{RaidSquadRole::Support,gun,{first,second}}};
    const auto receipt = executeDeploy(profile,content,deploy,{profile.revision,"two-loss-deploy"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const CommandContext take{profile.revision,"receive-magazine"};
    const auto transfer = takeRaidSquadCargo(profile,content,RaidSquadRole::Fire,claimed,take);
    ASSERT_TRUE(transfer.succeeded) << transfer.message;
    EXPECT_TRUE(takeRaidSquadCargo(profile,content,RaidSquadRole::Fire,claimed,take).alreadyCommitted);
    for (auto role : {RaidSquadRole::Fire,RaidSquadRole::Support})
        ASSERT_TRUE(revealRaidSquadCorpseInSimulation(profile,content,role,outdoorRaidSpaceId(),profile.pendingRaid->playerSpawn));
    const auto settled = settlePendingRaid(profile,content,deploy.settlementId,RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(claimed)->location).container,ProfileContainerId::compartment(pack,0U));
    const auto fireLoss = *profile.lastRaidResult->fireSupport->lostRecordId;
    const auto supportLoss = *profile.lastRaidResult->squadResults.at(RaidSquadRole::Support).lostRecordId;
    EXPECT_NE(fireLoss,supportLoss);
    for (auto id : {gun,first,second}) EXPECT_EQ(std::get<LostRaidAssetLocation>(profile.assets.find(id)->location).recordId,supportLoss);
    for (auto id : {rifle,unopened}) EXPECT_EQ(std::get<LostRaidAssetLocation>(profile.assets.find(id)->location).recordId,fireLoss);
}

TEST_F(RaidSquadDomainTest, ReservedCellsReturnBorrowedKitEvenAfterFillingEveryOtherStashCell)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"medic"));
    const auto gun = add(kRaidSquadSupportWeaponId);
    const auto kit = add(ItemDefinitionId{"item.medical.bandage_basic"});
    add(kRaidSquadSupportAmmoId,30U);
    const auto originalGun = profile.assets.find(gun)->location;
    const auto originalKit = profile.assets.find(kit)->location;
    DeployCommand deploy{"full","full-settle",8847U,kRaidFireSupportMapId};
    deploy.squad = {{RaidSquadRole::Support,gun,{kit}}};
    const auto started = executeDeploy(profile,content,deploy,{profile.revision,"full-deploy"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const auto &item = content.item(kRaidSquadSupportAmmoId);
    const auto stash = ProfileContainerId::stash();
    const auto extent = profileContainerSize(profile,content,stash);
    for (int y = 0; y < extent.height; ++y)
        for (int x = 0; x < extent.width; ++x)
            if (profilePlacementFits(profile,content,stash,{x,y},item,ItemOrientation::Degrees0))
                static_cast<void>(profile.assets.create(item,StoredAssetLocation{stash,{x,y}}));
    ASSERT_FALSE(findFirstProfileFit(profile,content,stash,item,ItemOrientation::Degrees0));
    const auto result = settlePendingRaid(profile,content,deploy.settlementId,RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(profile.assets.find(gun)->location,originalGun);
    EXPECT_EQ(profile.assets.find(kit)->location,originalKit);
}

TEST_F(RaidSquadDomainTest, NamedFireDeathRequiresDistinctSuccessorDespiteUninjuredAndInjuredCombatResidents)
{
    const auto combat = baseProfessionIndex(BaseResidentProfession::Combat);
    auto &population = profile.basePopulation;
    population.ordinaryResidents += 2U;
    population.professionResidents[combat] += 2U;
    ++population.injuredResidents;
    ++population.injuredByProfession[combat];
    const auto rifle = add(kRaidFireSupportRifleId);
    add(kRaidFireSupportAmmunitionId,60U);
    ASSERT_FALSE(profile.committedRescues.contains(kRaidFireSupportReplacementRescueId));
    DeployCommand deploy{"bind-fire","bind-fire-result",8847U,kRaidFireSupportMapId};
    const CommandContext start{profile.revision,"bind-fire-deploy"};
    auto legacy = profile;
    deploy.fireSupport = RaidFireSupportLoadout{rifle,{}};
    ASSERT_TRUE(executeDeploy(legacy,content,deploy,start).succeeded);
    ASSERT_TRUE(rollbackPendingRaidToBase(legacy,content).succeeded);
    deploy.fireSupport.reset();
    deploy.squad = {{RaidSquadRole::Fire,rifle,{}}};
    ASSERT_TRUE(executeDeploy(profile,content,deploy,start).succeeded);
    auto abandoned = profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(abandoned,content).succeeded);
    EXPECT_FALSE(abandoned.squadRoster.contains(RaidSquadRole::Fire));
    EXPECT_EQ(profileStateFingerprint(abandoned),profileStateFingerprint(legacy));
    ASSERT_TRUE(revealRaidSquadCorpseInSimulation(profile,content,RaidSquadRole::Fire,outdoorRaidSpaceId(),profile.pendingRaid->playerSpawn));
    const auto dead = settlePendingRaid(profile,content,deploy.settlementId,RaidResultOutcome::Extracted);
    ASSERT_TRUE(dead.succeeded) << dead.message;
    ASSERT_TRUE(profile.squadRoster.contains(RaidSquadRole::Fire));
    ASSERT_EQ(profile.squadRoster.at(RaidSquadRole::Fire).status,RaidSquadResidentStatus::Missing);
    const auto original = profile.squadRoster.at(RaidSquadRole::Fire).generation;
    EXPECT_EQ(profile.basePopulation.professionResidents[combat],1U);
    EXPECT_EQ(profile.basePopulation.injuredByProfession[combat],1U);
    // Another healthy, anonymous combat resident arrives during the vacancy.
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[combat];
    const auto originalLoss = *profile.lastRaidResult->fireSupport->lostRecordId;
    EXPECT_EQ(queryRaidFireSupportLoadout(profile,content,kRaidFireSupportMapId).availableCombatResidents,0U);
    EXPECT_FALSE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Fire).residentAvailable);
    const auto end = profile.squadRoster.at(RaidSquadRole::Fire).replacementReadyMinute;
    ASSERT_EQ(end,profile.worldClock.elapsedWorldMinutes + kRaidFireSupportReplacementDelayMinutes);
    static_cast<void>(advanceWorldClock(profile.worldClock,kRaidFireSupportReplacementDelayMinutes - 1U));
    EXPECT_FALSE(readyRaidFireSupportReplacement(profile,content,kRaidFireSupportReplacementMapId));
    static_cast<void>(advanceWorldClock(profile.worldClock,1U));
    auto source = readyRaidFireSupportReplacement(profile,content,kRaidFireSupportReplacementMapId);
    ASSERT_EQ(source,deploy.settlementId);
    DeployCommand rescue{"relief","relief-result",8847U,kRaidFireSupportReplacementMapId};
    const auto started = executeDeploy(profile,content,rescue,{profile.revision,"relief-deploy"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_EQ(profile.pendingRaid->fireSupportReplacementSource,source);
    auto reloaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile,content.contentVersion()),content);
    ASSERT_TRUE(reloaded.profile) << reloaded.message;
    profile = std::move(*reloaded.profile);
    const CommandContext admission{profile.revision,raidFireSupportReplacementTransactionId(*source)};
    const auto rescued = executeRaidFireSupportReplacementAdmission(profile,content,*source,admission);
    ASSERT_TRUE(rescued.succeeded) << rescued.message;
    EXPECT_TRUE(executeRaidFireSupportReplacementAdmission(profile,content,*source,admission).alreadyCommitted);
    ASSERT_TRUE(settlePendingRaid(profile,content,rescue.settlementId,RaidResultOutcome::Extracted).succeeded);
    EXPECT_FALSE(profile.committedRescues.contains(kRaidFireSupportReplacementRescueId));
    EXPECT_EQ(profile.squadRoster.at(RaidSquadRole::Fire).generation,original+1U);
    EXPECT_TRUE(queryRaidSquadLoadout(profile,content,kRaidFireSupportMapId,RaidSquadRole::Fire).residentAvailable);
    EXPECT_EQ(std::get<LostRaidAssetLocation>(profile.assets.find(rifle)->location).recordId,originalLoss);
}

TEST_F(RaidSquadDomainTest, NeitherFireLoadoutBorrowsAnIdleSpecialistsPlace)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"clinic"));
    const auto map = kRaidFireSupportMapId;
    const auto combat = baseProfessionIndex(BaseResidentProfession::Combat);
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[combat];
    auto expectOnlyMedic = [&] {
        EXPECT_FALSE(queryRaidSquadLoadout(profile,content,map,RaidSquadRole::Fire).residentAvailable);
        EXPECT_EQ(queryRaidFireSupportLoadout(profile,content,map).availableCombatResidents,0U);
        EXPECT_TRUE(queryRaidSquadLoadout(profile,content,map,RaidSquadRole::Support).residentAvailable);
    };
    ++profile.basePopulation.injuredResidents;
    ++profile.basePopulation.injuredByProfession[combat];
    expectOnlyMedic();
    --profile.basePopulation.injuredResidents;
    --profile.basePopulation.injuredByProfession[combat];
    auto &outpost = profile.regionalOperations.outposts.at(RegionalOutpostDefinitionId{"regional_outpost.old_service_relay"});
    outpost.established = true;
    outpost.assignedStaff[combat] = 1U;
    ASSERT_TRUE(validateProfileState(profile,content).valid);
    expectOnlyMedic();
    profile.squadRoster[RaidSquadRole::Fire] = {};
    expectOnlyMedic();
    outpost.assignedStaff[combat] = 0U;
    EXPECT_TRUE(queryRaidSquadLoadout(profile,content,map,RaidSquadRole::Fire).residentAvailable);
    EXPECT_EQ(queryRaidFireSupportLoadout(profile,content,map).availableCombatResidents,1U);
}

TEST_F(RaidSquadDomainTest, InvalidOrDuplicateLoadoutsLeaveProfileUntouched)
{
    ASSERT_NO_FATAL_FAILURE(recruit(RaidSquadRole::Support,"medic"));
    const auto gun = add(kRaidSquadSupportWeaponId);
    std::vector<AssetInstanceId> existingAmmo;
    for (const auto &[id,a] : profile.assets.records())
        if (a.definitionId == kRaidSquadSupportAmmoId && std::holds_alternative<StoredAssetLocation>(a.location) &&
            std::get<StoredAssetLocation>(a.location).container == ProfileContainerId::stash()) existingAmmo.push_back(id);
    for (auto id : existingAmmo) static_cast<void>(profile.assets.erase(id));
    const auto rounds = add(kRaidSquadSupportAmmoId,29U);
    const auto pain = add(ItemDefinitionId{"item.medical.painkiller_basic"});
    DeployCommand command{"bad-raid","bad-settle",8847U,kRaidFireSupportMapId};
    command.squad = {{RaidSquadRole::Support,gun,{}}};
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeDeploy(profile,content,command,{profile.revision,"short"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile),before);
    ++profile.assets.findMutable(rounds)->quantity;
    const auto restored = profileStateFingerprint(profile);
    command.squad.front().cargoAssetIds = {pain};
    EXPECT_FALSE(executeDeploy(profile,content,command,{profile.revision,"wrong-cargo"}).succeeded);
    command.squad.front().cargoAssetIds.clear();
    command.squad.push_back(command.squad.front());
    EXPECT_FALSE(executeDeploy(profile,content,command,{profile.revision,"duplicate"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile),restored);
}
}
