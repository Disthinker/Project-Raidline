#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base_workforce_domain.h"

#include "alpha_content_ids.h"
#include "base_morale_domain.h"
#include "base_population_domain.h"
#include "profile_state.h"
#include "raid_lifecycle.h"

TEST(ProfileStateTest, NewAlphaProfileCreatesContractAssets)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        content);

    EXPECT_EQ(profile.currency, 200U);
    EXPECT_EQ(profile.revision, 1U);
    EXPECT_EQ(profile.assets.records().size(), 20U);
    EXPECT_EQ(profile.assets.nextAssetId(), 21U);
    EXPECT_TRUE(profile.committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_TRUE(validateProfileState(
        profile,
        content).valid);
    EXPECT_EQ(profile.regionalOperations.technologyCore.instanceId,
              "technology_core.primary");
    EXPECT_EQ(profile.regionalOperations.technologyCore.baseSiteDefinitionId,
              RegionalBaseSiteDefinitionId{
                  "regional_base_site.greyline_yard"});
    EXPECT_TRUE(profile.baseConstruction.facilities.contains(
        BaseFacilityDefinitionId{"base_facility.warehouse"}));
    EXPECT_FALSE(profile.baseConstruction.facilities.contains(
        BaseFacilityDefinitionId{"base_facility.kitchen_water"}));

    std::uint64_t ammunition{};
    std::size_t magazines{};
    std::size_t medkits{};
    std::size_t protectiveGear{};
    std::size_t fieldMedical{};
    std::size_t maintenanceKits{};
    std::size_t pistols{};
    std::size_t pistolMagazines{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == alpha_content::ammunition)
        {
            ammunition += asset.quantity;
        }
        if (asset.definitionId == alpha_content::magazine)
        {
            ++magazines;
        }
        if (asset.definitionId == alpha_content::medkit)
        {
            ++medkits;
            EXPECT_EQ(asset.remainingCharges, 3U);
        }
        if (asset.definitionId == alpha_content::helmet ||
            asset.definitionId == alpha_content::bodyArmor)
        {
            ++protectiveGear;
            EXPECT_GT(asset.currentMaximumDurability, 0U);
            EXPECT_EQ(asset.currentDurability, asset.currentMaximumDurability);
        }
        if (asset.definitionId == alpha_content::bandage ||
            asset.definitionId == alpha_content::tourniquet ||
            asset.definitionId == alpha_content::painkiller)
        {
            ++fieldMedical;
        }
        if (asset.definitionId == alpha_content::rifle)
        {
            EXPECT_EQ(asset.currentMaximumDurability, 10000U);
            EXPECT_EQ(asset.currentDurability, 10000U);
            EXPECT_EQ(asset.weaponMalfunction, WeaponMalfunctionType::None);
        }
        if (asset.definitionId == alpha_content::pistol)
        {
            ++pistols;
            EXPECT_EQ(asset.currentMaximumDurability, 10000U);
            EXPECT_EQ(asset.currentDurability, 10000U);
            EXPECT_EQ(asset.weaponMalfunction, WeaponMalfunctionType::None);
        }
        if (asset.definitionId == alpha_content::pistolMagazine)
        {
            ++pistolMagazines;
        }
        if (asset.definitionId == alpha_content::weaponMaintenanceKit)
        {
            ++maintenanceKits;
            EXPECT_EQ(asset.remainingCharges, 2500U);
        }
        if (asset.definitionId == alpha_content::armorMaintenanceKit)
        {
            ++maintenanceKits;
            EXPECT_EQ(asset.remainingCharges, 5000U);
        }
    }
    EXPECT_EQ(ammunition, 90U);
    EXPECT_EQ(magazines, 3U);
    EXPECT_EQ(medkits, 2U);
    EXPECT_EQ(protectiveGear, 2U);
    EXPECT_EQ(fieldMedical, 3U);
    EXPECT_EQ(maintenanceKits, 2U);
    EXPECT_EQ(pistols, 1U);
    EXPECT_EQ(pistolMagazines, 2U);
}

TEST(ProfileStateTest, NewPublishedProfileCreatesCompleteWarehouseCatalog)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewPublishedProfile(
        "published-profile-test", content);

    EXPECT_EQ(profile.revision, 1U);
    EXPECT_EQ(profile.assets.records().size(), content.items().size() + 5U);
    EXPECT_EQ(profile.assets.nextAssetId(), content.items().size() + 6U);
    EXPECT_TRUE(profile.committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    for (const ItemDefinition &definition : content.items())
    {
        const auto stored = std::find_if(
            profile.assets.records().begin(),
            profile.assets.records().end(),
            [&definition](const auto &entry)
            {
                const auto *location =
                    std::get_if<StoredAssetLocation>(&entry.second.location);
                return entry.second.definitionId == definition.definitionId &&
                    location != nullptr &&
                    location->container == ProfileContainerId::stash() &&
                    (!entry.second.reliefBatchId.has_value() ||
                     definition.maxStackSize == 1U);
            });
        ASSERT_NE(stored, profile.assets.records().end())
            << definition.definitionId.value();
        if (definition.maxStackSize > 1U)
        {
            EXPECT_EQ(stored->second.quantity, definition.maxStackSize)
                << definition.definitionId.value();
        }
    }
}

TEST(ProfileStateTest, ContentBetaLoadoutWeightSeparatesLightAndHeavyExtraction)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile(
        "content-beta-loadout-weight",
        content);
    const auto find = [&profile](const ItemDefinitionId &definitionId)
    {
        const auto found = std::find_if(
            profile.assets.records().begin(),
            profile.assets.records().end(),
            [&definitionId](const auto &entry)
            {
                return entry.second.definitionId == definitionId;
            });
        return found == profile.assets.records().end() ? 0U : found->first;
    };
    const auto equip = [&profile, &content](
        AssetInstanceId assetId,
        EquipmentSlotKind slot,
        std::string transaction)
    {
        return executeInventory(
            profile,
            content,
            InventoryEquipCommand{assetId, slot},
            CommandContext{profile.revision, std::move(transaction)});
    };

    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.protective_gear.helmet_scout"}),
        EquipmentSlotKind::Helmet,
        "equip-light-helmet").succeeded);
    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.protective_gear.body_armor_light"}),
        EquipmentSlotKind::BodyArmor,
        "equip-light-armor").succeeded);
    ASSERT_TRUE(equip(
        find(alpha_content::chestRig),
        EquipmentSlotKind::ChestRig,
        "equip-light-rig").succeeded);
    ASSERT_TRUE(equip(
        find(alpha_content::backpack),
        EquipmentSlotKind::Backpack,
        "equip-light-pack").succeeded);
    EXPECT_EQ(carriedWeightGrams(profile, content), 7050U);
    EXPECT_LT(carriedWeightGrams(profile, content), 22000U);

    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.protective_gear.helmet_heavy"}),
        EquipmentSlotKind::Helmet,
        "equip-heavy-helmet").succeeded);
    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.protective_gear.body_armor_heavy"}),
        EquipmentSlotKind::BodyArmor,
        "equip-heavy-armor").succeeded);
    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.container.chest_rig_assault"}),
        EquipmentSlotKind::ChestRig,
        "equip-heavy-rig").succeeded);
    ASSERT_TRUE(equip(
        find(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquipmentSlotKind::Backpack,
        "equip-heavy-pack").succeeded);
    EXPECT_EQ(carriedWeightGrams(profile, content), 22700U);
    EXPECT_GT(carriedWeightGrams(profile, content), 22000U);
}

TEST(ProfileStateTest, WarehouseCatalogGrantIsAtomicAndIdempotent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("catalog-grant", content);
    profile.committedTransactions.erase(
        "bootstrap.warehouse_catalog.content_54");
    const ItemDefinitionId missingDefinition{
        "item.weapon.lmg_7_62x51_service"};
    std::vector<AssetInstanceId> removed;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        if (asset.definitionId == missingDefinition)
        {
            removed.push_back(assetId);
        }
    }
    ASSERT_EQ(removed.size(), 1U);
    ASSERT_TRUE(profile.assets.erase(removed.front()));
    const ProfileRevision beforeRevision = profile.revision;

    const WarehouseCatalogGrantReceipt granted =
        grantPublishedWarehouseCatalog(profile, content);
    ASSERT_TRUE(granted.succeeded) << granted.message;
    EXPECT_FALSE(granted.alreadyGranted);
    EXPECT_EQ(granted.addedDefinitionCount, 1U);
    EXPECT_EQ(profile.revision, beforeRevision + 1U);
    EXPECT_TRUE(profile.committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
    EXPECT_TRUE(std::any_of(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&missingDefinition](const auto &entry)
        {
            const auto *location =
                std::get_if<StoredAssetLocation>(&entry.second.location);
            return entry.second.definitionId == missingDefinition &&
                location != nullptr &&
                location->container == ProfileContainerId::stash();
        }));

    const std::uint64_t grantedFingerprint =
        profileStateFingerprint(profile);
    const WarehouseCatalogGrantReceipt replay =
        grantPublishedWarehouseCatalog(profile, content);
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyGranted);
    EXPECT_EQ(replay.addedDefinitionCount, 0U);
    EXPECT_EQ(profileStateFingerprint(profile), grantedFingerprint);
}

TEST(ProfileStateTest, FullWarehouseRejectsCatalogGrantWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("catalog-capacity", content);
    profile.committedTransactions.erase(
        "bootstrap.warehouse_catalog.content_54");
    std::vector<AssetInstanceId> existing;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        existing.push_back(assetId);
    }
    for (AssetInstanceId assetId : existing)
    {
        ASSERT_TRUE(profile.assets.erase(assetId));
    }
    const ItemDefinition &ammunition =
        content.item(alpha_content::ammunition);
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 24; ++x)
        {
            static_cast<void>(profile.assets.create(
                ammunition,
                StoredAssetLocation{
                    ProfileContainerId::stash(), GridPosition{x, y}},
                1U));
        }
    }
    const std::uint64_t beforeFingerprint =
        profileStateFingerprint(profile);
    const AssetInstanceId beforeHighWater = profile.assets.nextAssetId();

    const WarehouseCatalogGrantReceipt rejected =
        grantPublishedWarehouseCatalog(profile, content);
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_FALSE(rejected.alreadyGranted);
    EXPECT_TRUE(rejected.capacityBlocked);
    EXPECT_EQ(rejected.revision, profile.revision);
    EXPECT_EQ(profileStateFingerprint(profile), beforeFingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), beforeHighWater);
    EXPECT_FALSE(profile.committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
}

TEST(ProfileStateTest, BackwardHighWaterMarkIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        publishedContentRegistry());
    profile.assets.setNextAssetIdForLoad(2);

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("high-water"), std::string::npos);
}

TEST(ProfileStateTest, FingerprintChangesWithAuthoritativeState)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-test",
        publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);
    ++profile.currency;
    EXPECT_NE(profileStateFingerprint(profile), before);

    const std::uint64_t afterCurrency = profileStateFingerprint(profile);
    ++profile.worldClock.elapsedWorldMinutes;
    EXPECT_NE(profileStateFingerprint(profile), afterCurrency);

    const std::uint64_t afterClock = profileStateFingerprint(profile);
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[baseProfessionIndex(
        BaseResidentProfession::General)];
    EXPECT_NE(profileStateFingerprint(profile), afterClock);

    const std::uint64_t afterPopulation = profileStateFingerprint(profile);
    profile.baseMorale.pendingPositiveEventCount = 1U;
    EXPECT_NE(profileStateFingerprint(profile), afterPopulation);
}

TEST(ProfileStateTest, InvalidMoraleOrRerolledEventIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-morale-invalid",
        publishedContentRegistry());
    profile.baseMorale.supportedRecoveryDays =
        publishedContentRegistry().baseMorale().recoveryDaysFromLow;
    ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("morale"), std::string::npos);

    profile = makeNewAlphaProfile(
        "profile-event-invalid",
        publishedContentRegistry());
    profile.baseCommunityEvent.definitionId =
        BaseCommunityEventDefinitionId{"base_event.shared_meal"};
    if (profile.baseCommunityEvent.definitionId ==
        selectBaseCommunityEvent(
            profile.profileId,
            profile.baseCommunityEvent.cycleIndex,
            publishedContentRegistry()))
    {
        profile.baseCommunityEvent.definitionId =
            BaseCommunityEventDefinitionId{"base_event.restless_nights"};
    }
    result = validateProfileState(profile, publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("morale"), std::string::npos);
}

TEST(ProfileStateTest, DemandCycleCannotAdvanceAheadOfWorldClock)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-world-clock-invalid",
        publishedContentRegistry());
    profile.baseResources.resolvedDemandCycleCount = 1U;

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("world clock"), std::string::npos);
}

TEST(ProfileStateTest, InvalidMedicalTimerCombinationIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-medical-invalid",
        publishedContentRegistry());
    profile.medicalStatus.bleeding = BleedingSeverity::Heavy;

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("medical"), std::string::npos);
}

TEST(ProfileStateTest, PopulationStateHasBoundedAggregateCounts)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-population-invalid",
        publishedContentRegistry());
    profile.basePopulation.ordinaryResidents =
        kMaximumOrdinaryResidents + 1U;

    const ProfileValidationResult result = validateProfileState(
        profile,
        publishedContentRegistry());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("population"), std::string::npos);
}

TEST(ProfileStateTest, PendingRescueSecuredFlagMustMatchCommittedLedger)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-rescue-invalid", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "profile-rescue-raid",
            "profile-rescue-settlement",
            9917U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "profile-rescue-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_TRUE(profile.pendingRaid->rescue.has_value());
    const RescueDefinitionId rescueId =
        profile.pendingRaid->rescue->definitionId;

    profile.pendingRaid->rescue->secured = true;
    EXPECT_FALSE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    profile.committedRescues.insert(rescueId);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    profile.pendingRaid->rescue->secured = false;
    EXPECT_FALSE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(ProfileStateTest, PendingRaidRejectsInvalidStartingFacilityOwnership)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "profile-pending-facility-invalid", content);
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "profile-pending-facility-raid",
            "profile-pending-facility-settlement",
            9918U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "profile-pending-facility-deploy"})
                    .succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());

    profile.pendingRaid->travel.startingBaseConstruction.facilities.erase(
        BaseFacilityDefinitionId{"base_facility.warehouse"});
    const ProfileValidationResult result = validateProfileState(profile, content);

    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("travel snapshot"), std::string::npos);
}

TEST(ProfileStateTest, UnknownCommittedRescueDefinitionIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-unknown-rescue", publishedContentRegistry());
    profile.committedRescues.insert(
        RescueDefinitionId{"rescue.ordinary.unknown"});

    const ProfileValidationResult validation = validateProfileState(
        profile, publishedContentRegistry());

    EXPECT_FALSE(validation.valid);
    EXPECT_NE(validation.message.find("unknown"), std::string::npos);
}

TEST(ProfileStateTest, InvalidBaseConstructionStateIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-invalid-construction",
        publishedContentRegistry());
    profile.baseConstruction.dormitoryLevel = 3U;
    EXPECT_FALSE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    profile.baseConstruction.dormitoryLevel = 2U;
    EXPECT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    profile.baseConstruction.activeProject =
        ActiveBaseConstructionProject{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            4U,
            3U,
            profile.worldClock.elapsedWorldMinutes,
            profile.worldClock.elapsedWorldMinutes + 360U};
    EXPECT_FALSE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);
}

TEST(ProfileStateTest, TechnologyCoreAndFacilityOwnershipMustStayConsistent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "profile-invalid-base-ownership", content);

    profile.regionalOperations.technologyCore.baseSiteDefinitionId =
        RegionalBaseSiteDefinitionId{
            "regional_base_site.ashworks_logistics_yard"};
    EXPECT_FALSE(validateProfileState(profile, content).valid);

    profile = makeNewAlphaProfile(
        "profile-invalid-facility-ownership", content);
    profile.baseConstruction.facilities.erase(
        BaseFacilityDefinitionId{"base_facility.warehouse"});
    EXPECT_FALSE(validateProfileState(profile, content).valid);

    profile = makeNewAlphaProfile(
        "profile-invalid-kitchen-ownership", content);
    profile.baseConstruction.kitchenWaterLevel = 1U;
    EXPECT_FALSE(validateProfileState(profile, content).valid);

    profile.baseConstruction.facilities[
        BaseFacilityDefinitionId{"base_facility.kitchen_water"}] =
        BaseConstructionState::FacilityPlacement::Installed;
    EXPECT_TRUE(validateProfileState(profile, content).valid);

    const BaseFacilityDefinitionId workshop{"base_facility.workshop"};
    profile.baseConstruction.facilities[workshop] =
        BaseConstructionState::FacilityPlacement::Reserve;
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    profile.baseConstruction.facilityReserveStartedWorldMinutes[workshop] =
        profile.worldClock.elapsedWorldMinutes;
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(ProfileStateTest, SupplyPolicyRequiresKnownCompatibleContribution)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-invalid-supply-policy",
        publishedContentRegistry());
    profile.baseSupplyPolicy.assignments.emplace(
        alpha_content::lootCola,
        BaseSupplyCategory::Medical);
    EXPECT_FALSE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    profile.baseSupplyPolicy.assignments.clear();
    profile.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.loot.unknown"},
        BaseSupplyCategory::Food);
    EXPECT_FALSE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);
}

TEST(ProfileStateTest, CarriedWeightCountsNestedAssetsAndLoadedRoundsOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "profile-weight",
        publishedContentRegistry());
    AssetRecord *rifle{};
    AssetRecord *chestRig{};
    AssetRecord *backpack{};
    AssetRecord *magazine{};
    AssetRecord *ammunition{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        AssetRecord *mutableAsset = profile.assets.findMutable(id);
        if (asset.definitionId == alpha_content::rifle && rifle == nullptr)
        {
            rifle = mutableAsset;
        }
        else if (asset.definitionId == alpha_content::chestRig)
        {
            chestRig = mutableAsset;
        }
        else if (asset.definitionId == alpha_content::backpack)
        {
            backpack = mutableAsset;
        }
        else if (asset.definitionId == alpha_content::magazine &&
                 magazine == nullptr)
        {
            magazine = mutableAsset;
        }
        else if (asset.definitionId == alpha_content::ammunition &&
                 ammunition == nullptr)
        {
            ammunition = mutableAsset;
        }
    }
    ASSERT_NE(rifle, nullptr);
    ASSERT_NE(chestRig, nullptr);
    ASSERT_NE(backpack, nullptr);
    ASSERT_NE(magazine, nullptr);
    ASSERT_NE(ammunition, nullptr);

    rifle->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
    chestRig->location = EquippedAssetLocation{EquipmentSlotKind::ChestRig};
    backpack->location = EquippedAssetLocation{EquipmentSlotKind::Backpack};
    magazine->location = InstalledMagazineLocation{rifle->instanceId};
    ammunition->quantity = 10U;
    ammunition->location = StoredAssetLocation{
        ProfileContainerId::compartment(backpack->instanceId, 0U),
        GridPosition{0, 0}};
    magazine->magazineRounds = {
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt},
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt}};
    rifle->chamberedRound =
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt};

    EXPECT_EQ(
        carriedWeightGrams(profile, publishedContentRegistry()),
        6606U);

    ammunition->location = StoredAssetLocation{
        ProfileContainerId::stash(),
        GridPosition{0, 0}};
    EXPECT_EQ(
        carriedWeightGrams(profile, publishedContentRegistry()),
        6486U);
}
