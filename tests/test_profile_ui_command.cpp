#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "profile_ui_command.h"
#include <algorithm>

namespace
{
AssetInstanceId firstUiAsset(const ProfileState &profile,const ItemDefinitionId &definition)
{
    for(const auto &[id,asset]:profile.assets.records())if(asset.definitionId==definition)return id;
    return 0;
}
std::uint64_t looseRounds(const ProfileState &profile)
{
    std::uint64_t total{};for(const auto &[id,asset]:profile.assets.records())if(asset.definitionId==alpha_content::ammunition)total+=asset.quantity;
    return total;
}
}

TEST(ProfileUiCommandTest, PurchaseChangesOnlyThePrivateCandidateAndDuplicateTransactionDoesNotChargeAgain)
{
    const auto &content=publishedContentRegistry();
    const auto confirmed=makeNewAlphaProfile("ui-prepare-purchase",content);auto candidate=confirmed;
    const auto fingerprint=profileStateFingerprint(confirmed),rounds=looseRounds(confirmed);
    const auto price=content.item(alpha_content::ammunition).marketBuyPrice;ASSERT_GT(price,0U);
    const CommandContext context{candidate.revision,"ui-buy-ammo"};
    const auto prepared=prepareProfileUiCommand(candidate,content,EconomyCommand{PurchaseCommand{alpha_content::ammunition,2}},context);
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_TRUE(std::holds_alternative<EconomyReceipt>(prepared.result.receipt));
    EXPECT_EQ(candidate.currency,confirmed.currency-2*price);EXPECT_EQ(looseRounds(candidate),rounds+2);
    EXPECT_EQ(profileStateFingerprint(confirmed),fingerprint);
    const auto after=profileStateFingerprint(candidate);
    const auto repeated=prepareProfileUiCommand(candidate,content,EconomyCommand{PurchaseCommand{alpha_content::ammunition,2}},context);
    ASSERT_TRUE(repeated.canCommit)<<repeated.message;EXPECT_TRUE(profileUiReceiptAlreadyCommitted(repeated.result.receipt));
    EXPECT_EQ(profileStateFingerprint(candidate),after);
}

TEST(ProfileUiCommandTest, StaleTransactionCannotMutateTheCandidateOrAllocateAnIdentity)
{
    const auto &content=publishedContentRegistry();auto candidate=makeNewAlphaProfile("ui-prepare-stale",content);
    const auto fingerprint=profileStateFingerprint(candidate),next=candidate.assets.nextAssetId();
    const auto prepared=prepareProfileUiCommand(candidate,content,EconomyCommand{PurchaseCommand{alpha_content::ammunition,2}},
        {candidate.revision-1,"ui-stale"});
    EXPECT_FALSE(prepared.canCommit);EXPECT_EQ(prepared.error,DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(candidate),fingerprint);EXPECT_EQ(candidate.assets.nextAssetId(),next);
}

TEST(ProfileUiCommandTest, ModePreparationKeepsTheInstanceTruthAndDefersOnlyTheReleaseFact)
{
    const auto &content=publishedContentRegistry();const auto confirmed=makeNewAlphaProfile("ui-prepare-mode",content);auto candidate=confirmed;
    const auto weapon=firstUiAsset(candidate,alpha_content::rifle);ASSERT_NE(weapon,0U);
    const auto previous=confirmed.assets.find(weapon)->fireMode;ASSERT_NE(previous,WeaponFireMode::ThreeRoundBurst);
    const auto prepared=prepareProfileUiCommand(candidate,content,WeaponAmmoCommand{SetWeaponFireModeCommand{weapon,WeaponFireMode::ThreeRoundBurst}},
        {candidate.revision,"ui-burst"});
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_EQ(candidate.assets.find(weapon)->fireMode,WeaponFireMode::ThreeRoundBurst);
    EXPECT_EQ(confirmed.assets.find(weapon)->fireMode,previous);EXPECT_EQ(prepared.result.affectedAssetId,weapon);
    EXPECT_TRUE(prepared.result.facts.releaseWeaponTrigger);
    const auto *receipt=std::get_if<WeaponAmmoReceipt>(&prepared.result.receipt);ASSERT_NE(receipt,nullptr);
    EXPECT_EQ(receipt->result,WeaponAmmoResult::FireModeChanged);
}

TEST(ProfileUiCommandTest, PaidMedicalReturnsTheOriginalQuoteAndItsCompletionFactWithoutRepeatingCharges)
{
    const auto &content=publishedContentRegistry();auto candidate=makeNewAlphaProfile("ui-prepare-medical",content);
    candidate.currentHealth=55;candidate.currency=1000;const auto before=candidate;
    const auto quote=queryBaseMedicalService(candidate,content);ASSERT_TRUE(quote.canCommit)<<quote.message;
    const CommandContext context{candidate.revision,"ui-medical"};
    const auto prepared=prepareProfileUiCommand(candidate,content,BaseMedicalServiceCommand{},context);
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_EQ(candidate.currentHealth,100);
    EXPECT_EQ(candidate.currency,before.currency-quote.quotedCurrency);EXPECT_TRUE(prepared.result.facts.medicalCompleted);
    const auto after=profileStateFingerprint(candidate);
    const auto repeated=prepareProfileUiCommand(candidate,content,BaseMedicalServiceCommand{},context);
    EXPECT_TRUE(repeated.canCommit);EXPECT_FALSE(repeated.result.facts.medicalCompleted);EXPECT_EQ(profileStateFingerprint(candidate),after);
}

TEST(ProfileUiCommandTest, PlaceablePurchaseReturnsItsActualStableAssetWithoutGrantingAgainOnReplay)
{
    const auto &content=publishedContentRegistry();auto candidate=makeNewAlphaProfile("ui-prepare-placeable",content);candidate.currency=10000;
    const ItemDefinitionId crate{"item.container.base_storage_crate"};const auto expectedId=candidate.assets.nextAssetId();
    const auto money=candidate.currency;const CommandContext context{candidate.revision,"ui-buy-crate"};
    const auto prepared=prepareProfileUiCommand(candidate,content,ProfileUiPlaceablePurchaseCommand{crate},context);
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_EQ(prepared.result.affectedAssetId,expectedId);
    ASSERT_NE(candidate.assets.find(expectedId),nullptr);EXPECT_EQ(candidate.assets.find(expectedId)->definitionId,crate);
    EXPECT_EQ(candidate.currency,money-content.item(crate).marketBuyPrice);
    const auto after=profileStateFingerprint(candidate);
    const auto replay=prepareProfileUiCommand(candidate,content,ProfileUiPlaceablePurchaseCommand{crate},context);
    EXPECT_TRUE(replay.canCommit);EXPECT_TRUE(profileUiReceiptAlreadyCommitted(replay.result.receipt));EXPECT_EQ(profileStateFingerprint(candidate),after);
}

TEST(ProfileUiCommandTest, MigrationCannotDiscardAnArmedPhysicalRecoverySnapshot)
{
    const auto &content=publishedContentRegistry();auto candidate=makeNewAlphaProfile("ui-prepare-armed",content);
    const auto site=candidate.regionalOperations.technologyCore.baseSiteDefinitionId;
    BasePlayerEffectsRecovery effects;effects.scope={WeaponActionActivity::BaseDaily,site,std::string{site.value()}};
    effects.worldSize={12800,7200};effects.playerPosition={1000,1000};
    effects.grenade.fuse=GrenadeFuse{{{1000,1000},{1100,1000},RaidSpaceDefinitionId{"base_space."+std::string{site.value()}},
        *content.item(ItemDefinitionId{"item.throwable.frag_grenade_basic"}).grenadeUse,"ui-armed-grenade"},.2};
    candidate.basePlayerEffects=effects;ASSERT_TRUE(validateProfileState(candidate,content).valid);
    const auto before=profileStateFingerprint(candidate);
    const auto prepared=prepareProfileUiCommand(candidate,content,BaseMigrationCommand{RegionalBaseSiteDefinitionId{"regional_base_site.ashworks_logistics_yard"}},
        {candidate.revision,"ui-migrate-with-fuse"});
    EXPECT_FALSE(prepared.canCommit);EXPECT_EQ(profileStateFingerprint(candidate),before);
    ASSERT_TRUE(candidate.basePlayerEffects->grenade.fuse);EXPECT_EQ(candidate.basePlayerEffects->grenade.fuse->landing.effectIdentity,"ui-armed-grenade");
}

TEST(ProfileUiCommandTest, FortificationInstallationStillRequiresCurrentWorldRouteProof)
{
    const auto &content=publishedContentRegistry();auto candidate=makeNewAlphaProfile("ui-prepare-fort",content);candidate.baseConstruction.materialUnits=100;
    const auto built=prepareProfileUiCommand(candidate,content,FortificationCommand{BuildFortificationCommand{kWoodBarricadeDefinition}},
        {candidate.revision,"ui-build-fort"});
    ASSERT_TRUE(built.canCommit)<<built.message;const auto *builtReceipt=std::get_if<FortificationReceipt>(&built.result.receipt);ASSERT_NE(builtReceipt,nullptr);
    BaseWorld world;const auto positions=baseDefensePositionCandidates(world.layout(),world.plotId(),*content.findFortification(kWoodBarricadeDefinition));
    const auto available=std::find_if(positions.begin(),positions.end(),[](const auto &p){return p.available;});ASSERT_NE(available,positions.end());
    const InstallFortificationCommand install{builtReceipt->instance,available->key};const auto before=profileStateFingerprint(candidate);
    const auto missingWorld=prepareProfileUiCommand(candidate,content,install,{candidate.revision,"ui-install-no-world"});
    EXPECT_FALSE(missingWorld.canCommit);EXPECT_EQ(profileStateFingerprint(candidate),before);
    const auto prepared=prepareProfileUiCommand(candidate,content,install,{candidate.revision,"ui-install-fort"},&world);
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_EQ(candidate.baseFortifications.instances.at(builtReceipt->instance).slot,available->key);
}

TEST(ProfileUiCommandTest, HintsPrepareOnlyFlagsAndCannotInventACommittedFirstRaidResult)
{
    const auto &content=publishedContentRegistry();const auto confirmed=makeNewAlphaProfile("ui-prepare-hints",content);auto candidate=confirmed;
    const auto original=profileStateFingerprint(confirmed);const auto tutorial=confirmed.tutorial;
    const auto invalid=prepareProfileUiCommand(candidate,content,ProfileUiHintsCommand{true},{candidate.revision,"ui-hints-no-result"});
    EXPECT_FALSE(invalid.canCommit);EXPECT_EQ(profileStateFingerprint(candidate),original);
    const CommandContext context{candidate.revision,"ui-dismiss-hints"};
    const auto prepared=prepareProfileUiCommand(candidate,content,ProfileUiHintsCommand{false},context);
    ASSERT_TRUE(prepared.canCommit)<<prepared.message;EXPECT_TRUE(candidate.homeFounding.hintsDismissed);
    EXPECT_EQ(candidate.tutorial,tutorial);EXPECT_EQ(profileStateFingerprint(confirmed),original);
    const auto after=profileStateFingerprint(candidate);
    const auto replay=prepareProfileUiCommand(candidate,content,ProfileUiHintsCommand{false},context);
    EXPECT_TRUE(replay.canCommit);EXPECT_TRUE(profileUiReceiptAlreadyCommitted(replay.result.receipt));EXPECT_EQ(profileStateFingerprint(candidate),after);
}
