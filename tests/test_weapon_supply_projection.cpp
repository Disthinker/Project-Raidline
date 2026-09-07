#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>

#include "maintenance_domain.h"
#include "medical_domain.h"
#include "raid_action.h"
#include "weapon_supply_projection.h"

namespace {
constexpr const char *rifleId = "item.weapon.rifle_5_45_service";
constexpr const char *magazineId = "item.magazine.5_45x39_30";
constexpr const char *roundId = "item.ammunition.5_45x39_standard";
constexpr const char *enhancedRoundId = "item.ammunition.5_45x39_enhanced";

struct Loadout {
  const ContentRegistry &content{publishedContentRegistry()};
  ProfileState profile;
  AssetInstanceId weapon{};
  AssetInstanceId rig{};
  AssetInstanceId backpack{};

  Loadout() {
    profile.profileId = "weapon-supply-test";
    weapon = create(rifleId,
                    EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
    rig = create("item.container.chest_rig_assault",
                 EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    backpack = create("item.container.backpack_field",
                      EquippedAssetLocation{EquipmentSlotKind::Backpack});
  }

  AssetInstanceId create(const char *definition, AssetLocation location,
                         std::uint32_t quantity = 1U) {
    return profile.assets.create(content.item(ItemDefinitionId{definition}),
                                 location, quantity);
  }

  StoredAssetLocation pack(int x = 0, int y = 0) const {
    return {ProfileContainerId::compartment(backpack, 0U), {x, y}};
  }

  StoredAssetLocation pocket(std::uint32_t index) const {
    return {ProfileContainerId::compartment(rig, index), {0, 0}};
  }

  AssetInstanceId magazine(AssetLocation location, std::uint32_t count) {
    const AssetInstanceId id = create(magazineId, location);
    profile.assets.findMutable(id)->magazineRounds.assign(
        count, MagazineRoundRecord{ItemDefinitionId{roundId}, std::nullopt});
    return id;
  }

  WeaponSupplyReadiness readiness(bool includeStash = false) const {
    const WeaponSupplyProjection projection =
        projectWeaponSupply(profile, content, includeStash);
    EXPECT_EQ(projection.weapons.size(), 1U);
    return projection.weapons.front();
  }
};

const WeaponSupplySuggestion *suggestion(const WeaponSupplyReadiness &readiness,
                                         WeaponSupplySuggestionKind kind) {
  const auto found =
      std::find_if(readiness.suggestions.begin(), readiness.suggestions.end(),
                   [kind](const auto &entry) { return entry.kind == kind; });
  return found == readiness.suggestions.end() ? nullptr : &*found;
}
} // namespace

TEST(WeaponSupplyProjectionTest, NoWeaponDoesNotClaimReadinessOrChangeProfile) {
  const ContentRegistry &content = publishedContentRegistry();
  ProfileState profile;
  const std::uint64_t fingerprint = profileStateFingerprint(profile);
  const auto projection = projectWeaponSupply(profile, content);
  EXPECT_TRUE(projection.weapons.empty());
  EXPECT_FALSE(projection.anyWeaponCanFire);
  EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(WeaponSupplyProjectionTest,
     LooseAmmunitionDoesNotMakeUnloadedWeaponFireable) {
  Loadout fixture;
  fixture.create(roundId, fixture.pack(), 40U);
  fixture.create("item.ammunition.9mm_basic", fixture.pack(1), 60U);
  fixture.create(roundId,
                 StoredAssetLocation{ProfileContainerId::stash(), {0, 0}}, 60U);
  const auto readiness = fixture.readiness();
  EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::Dry);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_FALSE(readiness.installedMagazineId.has_value());
  EXPECT_EQ(readiness.carriedLooseRounds, 40U);
  EXPECT_EQ(readiness.otherLoadedSpareMagazineRounds, 0U);
  EXPECT_TRUE(readiness.suggestions.empty());
}

TEST(WeaponSupplyProjectionTest,
     InstalledLoadedMagazineWithEmptyChamberNeedsChambering) {
  Loadout fixture;
  const auto magazine =
      fixture.magazine(InstalledMagazineLocation{fixture.weapon}, 20U);
  const auto readiness = fixture.readiness();
  EXPECT_EQ(readiness.installedMagazineId, magazine);
  EXPECT_EQ(readiness.installedMagazineRounds, 20U);
  EXPECT_EQ(readiness.chamberedRounds, 0U);
  EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::Chambered);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_NE(suggestion(readiness, WeaponSupplySuggestionKind::ChamberWeapon),
            nullptr);
  EXPECT_EQ(readiness.otherLoadedSpareMagazineCount, 0U);
}

TEST(WeaponSupplyProjectionTest,
     AChamberedRoundIsFireableWithoutAnyMagazineOrSpare) {
  Loadout fixture;
  fixture.profile.assets.findMutable(fixture.weapon)->chamberedRound =
      MagazineRoundRecord{ItemDefinitionId{roundId}, std::nullopt};
  const auto projection = projectWeaponSupply(fixture.profile, fixture.content);
  ASSERT_EQ(projection.weapons.size(), 1U);
  const auto &readiness = projection.weapons.front();
  EXPECT_TRUE(projection.anyWeaponCanFire);
  EXPECT_TRUE(readiness.canFireNow);
  EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::Fired);
  EXPECT_EQ(readiness.chamberedRounds, 1U);
  EXPECT_FALSE(readiness.installedMagazineId.has_value());
  EXPECT_FALSE(readiness.quickReloadMagazineId.has_value());
  EXPECT_EQ(readiness.otherLoadedSpareMagazineRounds, 0U);
}

TEST(WeaponSupplyProjectionTest,
     QuickReloadMatchesMagazineOnlyPocketsNotPackOrGeneralPocket) {
  Loadout fixture;
  const auto quick = fixture.magazine(fixture.pocket(0U), 10U);
  fixture.magazine(fixture.pocket(1U), 5U);
  fixture.magazine(fixture.pocket(4U), 29U);
  fixture.magazine(fixture.pack(), 30U);
  const auto readiness = fixture.readiness();
  EXPECT_EQ(readiness.quickReloadMagazineId, quick);
  EXPECT_EQ(readiness.quickReloadMagazineId,
            selectRaidReloadMagazine(fixture.profile, fixture.content,
                                     fixture.weapon));
  EXPECT_TRUE(readiness.quickReloadCanInstall);
  EXPECT_EQ(readiness.quickReloadMagazineRounds, 10U);
  EXPECT_EQ(readiness.quickReloadLoadedMagazineCount, 2U);
  EXPECT_EQ(readiness.quickReloadLoadedRounds, 15U);
  EXPECT_EQ(readiness.otherLoadedSpareMagazineCount, 2U);
  EXPECT_EQ(readiness.otherLoadedSpareMagazineRounds, 59U);
  EXPECT_FALSE(readiness.canFireNow);
}

TEST(WeaponSupplyProjectionTest,
     EmptyQuickReloadCandidateIsNotReportedAsLoaded) {
  Loadout fixture;
  const auto empty = fixture.magazine(fixture.pocket(0U), 0U);
  const auto readiness = fixture.readiness();
  EXPECT_EQ(readiness.quickReloadMagazineId, empty);
  EXPECT_TRUE(readiness.quickReloadCanInstall);
  EXPECT_EQ(readiness.quickReloadMagazineRounds, 0U);
  EXPECT_EQ(readiness.quickReloadLoadedMagazineCount, 0U);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_EQ(suggestion(readiness, WeaponSupplySuggestionKind::InstallMagazine),
            nullptr);
}

TEST(WeaponSupplyProjectionTest, BrokenAndFaultedWeaponsDoNotClaimFireability) {
  Loadout fixture;
  fixture.profile.assets.findMutable(fixture.weapon)->chamberedRound =
      MagazineRoundRecord{ItemDefinitionId{roundId}, std::nullopt};
  fixture.profile.assets.findMutable(fixture.weapon)->currentDurability = 0U;
  auto readiness = fixture.readiness();
  EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::Broken);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_EQ(suggestion(readiness, WeaponSupplySuggestionKind::MaintainWeapon),
            nullptr);

  const auto kit =
      fixture.create("item.maintenance.weapon_kit_basic", fixture.pack());
  readiness = fixture.readiness();
  const auto *repair =
      suggestion(readiness, WeaponSupplySuggestionKind::MaintainWeapon);
  ASSERT_NE(repair, nullptr);
  EXPECT_EQ(repair->sourceAssetId, kit);
  EXPECT_TRUE(queryWeaponMaintenance(
                  fixture.profile, fixture.content,
                  WeaponMaintenanceCommand{kit, fixture.weapon,
                                           MaintenanceAccess::CarriedOnly,
                                           MaintenanceLocation::Base})
                  .canCommit);

  fixture.profile.assets.findMutable(fixture.weapon)->currentDurability = 5000U;
  fixture.profile.assets.findMutable(fixture.weapon)->weaponMalfunction =
      WeaponMalfunctionType::Stovepipe;
  readiness = fixture.readiness();
  EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::BlockedByMalfunction);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_NE(suggestion(readiness, WeaponSupplySuggestionKind::ClearMalfunction),
            nullptr);
}

TEST(WeaponSupplyProjectionTest,
     LoadAdviceHasCompatibleAccessibleParticipantsAndNoMutation) {
  Loadout fixture;
  const auto magazine = fixture.magazine(fixture.pack(), 0U);
  fixture.create("item.ammunition.9mm_basic", fixture.pack(1), 60U);
  const auto ammo = fixture.create(roundId, fixture.pack(2), 10U);
  const auto before = profileStateFingerprint(fixture.profile);
  const auto revision = fixture.profile.revision;
  const auto highWater = fixture.profile.assets.nextAssetId();
  const auto readiness = fixture.readiness();
  const auto *load =
      suggestion(readiness, WeaponSupplySuggestionKind::LoadMagazine);
  ASSERT_NE(load, nullptr);
  EXPECT_EQ(load->sourceAssetId, ammo);
  EXPECT_EQ(load->targetAssetId, magazine);
  EXPECT_TRUE(queryWeaponAmmo(fixture.profile, fixture.content,
                              LoadMagazineCommand{magazine, ammo, 0U})
                  .canCommit);
  EXPECT_EQ(fixture.profile.revision, revision);
  EXPECT_EQ(fixture.profile.assets.nextAssetId(), highWater);
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}

TEST(WeaponSupplyProjectionTest,
     StashAccessChangesSuggestionsNotCarriedCounts) {
  Loadout fixture;
  const auto magazine = fixture.magazine(fixture.pack(), 0U);
  const auto ammo = fixture.create(
      roundId, StoredAssetLocation{ProfileContainerId::stash(), {0, 0}}, 60U);
  const auto before = profileStateFingerprint(fixture.profile);
  const auto disconnected = fixture.readiness();
  const auto connected = fixture.readiness(true);
  EXPECT_EQ(suggestion(disconnected, WeaponSupplySuggestionKind::LoadMagazine),
            nullptr);
  const auto *load =
      suggestion(connected, WeaponSupplySuggestionKind::LoadMagazine);
  ASSERT_NE(load, nullptr);
  EXPECT_EQ(load->sourceAssetId, ammo);
  EXPECT_EQ(load->targetAssetId, magazine);
  EXPECT_EQ(connected.carriedLooseRounds, 0U);
  EXPECT_EQ(disconnected.carriedLooseRounds, 0U);
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}

TEST(WeaponSupplyProjectionTest,
     MovingLoadedSpareToRigAdviceHasALegalExactCell) {
  Loadout fixture;
  const auto magazine = fixture.magazine(fixture.pack(), 15U);
  const auto readiness = fixture.readiness();
  const auto *move =
      suggestion(readiness, WeaponSupplySuggestionKind::MoveSpareToChestRig);
  ASSERT_NE(move, nullptr);
  ASSERT_TRUE(move->destination.has_value());
  EXPECT_EQ(move->sourceAssetId, magazine);
  EXPECT_EQ(move->targetAssetId, fixture.rig);
  EXPECT_TRUE(
      queryInventory(fixture.profile, fixture.content,
                     InventoryMoveCommand{magazine, 0U, *move->destination,
                                          move->destinationOrientation})
          .canCommit);
  EXPECT_NE(suggestion(readiness, WeaponSupplySuggestionKind::InstallMagazine),
            nullptr);
}

TEST(WeaponSupplyProjectionTest,
     FullRigDoesNotSuggestAnImpossibleEmptySlotMove) {
  Loadout fixture;
  for (std::uint32_t index{}; index < 4U; ++index)
    fixture.magazine(fixture.pocket(index), 15U);
  fixture.magazine(fixture.pack(), 20U);
  const auto readiness = fixture.readiness();
  EXPECT_EQ(
      suggestion(readiness, WeaponSupplySuggestionKind::MoveSpareToChestRig),
      nullptr);
}

TEST(WeaponSupplyProjectionTest, AnotherWeaponsInstalledMagazineIsNotASpare) {
  Loadout fixture;
  const auto otherWeapon = fixture.create(
      rifleId, EquippedAssetLocation{EquipmentSlotKind::SecondaryWeapon});
  fixture.magazine(InstalledMagazineLocation{otherWeapon}, 30U);
  const auto projection = projectWeaponSupply(fixture.profile, fixture.content);
  ASSERT_EQ(projection.weapons.size(), 2U);
  EXPECT_EQ(projection.weapons.front().otherLoadedSpareMagazineRounds, 0U);
  EXPECT_EQ(projection.weapons.front().quickReloadLoadedRounds, 0U);
  EXPECT_TRUE(projection.weapons.front().suggestions.empty());
  EXPECT_EQ(projection.weapons[1].installedMagazineRounds, 30U);
}

TEST(WeaponSupplyProjectionTest,
     ProjectionPreservesMixedRoundOrderAndReliefIdentity) {
  Loadout fixture;
  const auto magazine =
      fixture.magazine(InstalledMagazineLocation{fixture.weapon}, 0U);
  const std::vector<MagazineRoundRecord> rounds{
      {ItemDefinitionId{roundId}, std::string{"relief-1"}},
      {ItemDefinitionId{enhancedRoundId}, std::nullopt},
      {ItemDefinitionId{roundId}, std::nullopt}};
  fixture.profile.assets.findMutable(magazine)->magazineRounds = rounds;
  const auto before = profileStateFingerprint(fixture.profile);
  for (int iteration{}; iteration < 5; ++iteration) {
    const auto readiness = fixture.readiness();
    EXPECT_EQ(readiness.fireResult, WeaponAmmoResult::Chambered);
    EXPECT_EQ(readiness.installedMagazineRounds, rounds.size());
  }
  EXPECT_EQ(fixture.profile.assets.find(magazine)->magazineRounds, rounds);
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}

TEST(WeaponSupplyProjectionTest, UnknownEquippedDefinitionIsSafeAndNotReady) {
  Loadout fixture;
  fixture.profile.assets.findMutable(fixture.weapon)->definitionId =
      ItemDefinitionId{"item.weapon.unavailable_test"};
  const auto before = profileStateFingerprint(fixture.profile);
  const auto readiness = fixture.readiness();
  EXPECT_FALSE(readiness.definitionKnown);
  EXPECT_FALSE(readiness.canFireNow);
  EXPECT_TRUE(readiness.suggestions.empty());
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}

TEST(WeaponSupplyProjectionTest,
     MedicalRecommendationCountsSuppliesEvenAtFullHealth) {
  Loadout fixture;
  fixture.profile.currentHealth = 100;
  const auto medkit =
      fixture.create("item.medical.medkit_alpha", fixture.pack());
  const auto before = profileStateFingerprint(fixture.profile);
  auto projection = projectWeaponSupply(fixture.profile, fixture.content);
  EXPECT_TRUE(projection.hasCarriedMedical);
  EXPECT_TRUE(projection.hasChestRig);
  EXPECT_TRUE(projection.hasBackpack);
  EXPECT_FALSE(projection.hasBodyArmor);
  EXPECT_FALSE(queryMedicalUse(fixture.profile, fixture.content, medkit,
                               MedicalAccess::CarriedOnly)
                   .canCommit);
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);

  fixture.profile.assets.findMutable(medkit)->remainingCharges = 0U;
  fixture.create("item.medical.medkit_alpha",
                 StoredAssetLocation{ProfileContainerId::stash(), {0, 0}});
  projection = projectWeaponSupply(fixture.profile, fixture.content, true);
  EXPECT_FALSE(projection.hasCarriedMedical);
}

TEST(WeaponSupplyProjectionTest,
     ThousandAssetsAndFullMagazinesStayWithinInventoryBudget) {
  Loadout fixture;
  for (std::uint32_t index{}; index < 4U; ++index)
    fixture.magazine(fixture.pocket(index), 30U);
  for (int index{}; index < 48; ++index) {
    fixture.magazine(
        StoredAssetLocation{ProfileContainerId::stash(), {index, 0}}, 30U);
    fixture.create(roundId,
                   StoredAssetLocation{ProfileContainerId::stash(), {index, 2}},
                   60U);
  }
  while (fixture.profile.assets.records().size() < 1000U) {
    const auto index =
        static_cast<int>(fixture.profile.assets.records().size());
    fixture.create(
        "item.loot.cola_basic",
        StoredAssetLocation{ProfileContainerId::stash(), {index, 3}});
  }
  const auto before = profileStateFingerprint(fixture.profile);
  std::array<double, 5> elapsed{};
  for (double &sample : elapsed) {
    const auto start = std::chrono::steady_clock::now();
    const auto projection =
        projectWeaponSupply(fixture.profile, fixture.content, true);
    sample = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - start)
                 .count();
    ASSERT_EQ(projection.weapons.size(), 1U);
    const auto &readiness = projection.weapons.front();
    EXPECT_EQ(readiness.quickReloadLoadedMagazineCount, 4U);
    EXPECT_EQ(readiness.carriedLooseRounds, 0U);
    EXPECT_EQ(suggestion(readiness, WeaponSupplySuggestionKind::LoadMagazine),
              nullptr);
    EXPECT_EQ(
        suggestion(readiness, WeaponSupplySuggestionKind::MoveSpareToChestRig),
        nullptr);
  }
  std::sort(elapsed.begin(), elapsed.end());
  // Median tolerates unrelated CI scheduling pauses while guarding the old
  // 48 magazines x 48 stacks registry-copy amplification on every refresh.
  EXPECT_LT(elapsed[2], 50.0) << "median inventory projection milliseconds";
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}

TEST(WeaponSupplyProjectionTest,
     FullRigWithEmptyMagazinesDoesNotRepeatImpossiblePlacement) {
  Loadout fixture;
  for (std::uint32_t index{}; index < 4U; ++index)
    fixture.magazine(fixture.pocket(index), 0U);
  for (int index{}; index < 128; ++index)
    fixture.magazine(
        StoredAssetLocation{ProfileContainerId::stash(), {index, 0}}, 30U);
  const auto before = profileStateFingerprint(fixture.profile);
  const auto readiness = fixture.readiness(true);
  EXPECT_TRUE(readiness.quickReloadMagazineId.has_value());
  EXPECT_EQ(readiness.quickReloadLoadedMagazineCount, 0U);
  EXPECT_EQ(readiness.otherLoadedSpareMagazineCount, 0U);
  EXPECT_EQ(
      suggestion(readiness, WeaponSupplySuggestionKind::MoveSpareToChestRig),
      nullptr);
  EXPECT_NE(suggestion(readiness, WeaponSupplySuggestionKind::InstallMagazine),
            nullptr);
  EXPECT_EQ(profileStateFingerprint(fixture.profile), before);
}
