#include <gtest/gtest.h>

#include "economy_domain.h"
#include "weapon_component_domain.h"

namespace
{
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
const ItemDefinitionId suppressor{"item.component.suppressor_5_45"};
const ItemDefinitionId barrel{"item.component.precision_barrel"};
const auto &content() { return publishedContentRegistry(); }

AssetInstanceId find(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

AssetInstanceId looseSuppressor(const ProfileState &profile)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == suppressor &&
            std::holds_alternative<StoredAssetLocation>(asset.location)) return id;
    return 0;
}

ProfileState supplied()
{
    auto profile = makeNewPublishedProfile("suppressor-domain", content());
    profile.currency = 1000;
    const auto receipt = executeEconomy(profile, content(), PurchaseCommand{suppressor, 1},
        {profile.revision, "purchase-suppressor"});
    EXPECT_TRUE(receipt.succeeded) << receipt.message;
    return profile;
}

InventoryReceipt change(ProfileState &profile, AssetInstanceId weapon,
    WeaponComponentSlot slot, AssetInstanceId component, const std::string &transaction)
{
    return executeWeaponComponentChange(profile, content(), {weapon, slot, component},
        {profile.revision, transaction});
}
}

TEST(WeaponSuppressorAcousticsTest, UnmodifiedWeaponsRetainTheirOriginalAcoustics)
{
    const auto profile = makeNewPublishedProfile("ordinary-acoustics", content());
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto &definition = content().item(asset.definitionId);
        if (!definition.weaponUse) continue;
        const auto actual = effectiveWeaponUse(profile, content(), id);
        EXPECT_EQ(actual, *definition.weaponUse);
        EXPECT_EQ(actual.acoustics, WeaponAcousticsDefinition{});
    }
    EXPECT_EQ(find(profile, suppressor), 0U) << "Tactical component must not be a catalog gift";
}

TEST(WeaponSuppressorAcousticsTest, DefaultAndPrecisionBarrelsAcceptTheSameMuzzleWithoutARail)
{
    for (bool precision : {false, true})
    {
        SCOPED_TRACE(precision ? "precision barrel" : "default barrel");
        auto profile = supplied();
        const auto weapon = find(profile, rifle), part = looseSuppressor(profile);
        ASSERT_NE(weapon, 0U);
        ASSERT_NE(part, 0U);
        if (precision)
            ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Barrel,
                find(profile, barrel), "install-barrel").succeeded);

        const auto before = effectiveWeaponUse(profile, content(), weapon);
        const auto fingerprint = profileStateFingerprint(profile);
        const auto plan = queryWeaponComponentChange(profile, content(),
            {weapon, WeaponComponentSlot::Muzzle, part});
        ASSERT_TRUE(plan.canCommit) << plan.message;
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
        auto expected = before;
        expected.handlingSpeed -= 8U;
        expected.acoustics = {0.5F, 0.55F, 0.25F};
        EXPECT_EQ(plan.after, expected);
        ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, part,
            "install-suppressor").succeeded);
        EXPECT_EQ(effectiveWeaponUse(profile, content(), weapon), expected);
        EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(
            profile.assets.find(part)->location).weaponAssetId, weapon);
        EXPECT_TRUE(validateProfileState(profile, content()).valid);
    }
}

TEST(WeaponSuppressorAcousticsTest, WrongWeaponAndWrongSlotRejectWithoutChangingEitherAsset)
{
    auto profile = supplied();
    const auto part = looseSuppressor(profile);
    ASSERT_NE(part, 0U);
    const auto original = profileStateFingerprint(profile);
    const auto pistol = find(profile, ItemDefinitionId{"item.weapon.pistol_basic"});
    ASSERT_NE(pistol, 0U);
    EXPECT_FALSE(change(profile, pistol, WeaponComponentSlot::Muzzle, part,
        "wrong-platform").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), original);
    EXPECT_FALSE(change(profile, find(profile, rifle), WeaponComponentSlot::Barrel,
        part, "wrong-slot").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), original);
    EXPECT_FALSE(change(profile, find(profile, rifle), static_cast<WeaponComponentSlot>(99),
        part, "invalid-slot").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), original);
}

TEST(WeaponSuppressorAcousticsTest, RetryingAndStaleCommandsCannotDuplicateOrMoveTheMuzzle)
{
    auto profile = supplied();
    const auto weapon = find(profile, rifle), part = looseSuppressor(profile);
    const auto revision = profile.revision;
    const WeaponComponentCommand command{weapon, WeaponComponentSlot::Muzzle, part};
    ASSERT_TRUE(executeWeaponComponentChange(profile, content(), command,
        {revision, "install-once"}).succeeded);
    const auto installed = profileStateFingerprint(profile);
    const auto retry = executeWeaponComponentChange(profile, content(), command,
        {revision, "install-once"});
    EXPECT_TRUE(retry.succeeded);
    EXPECT_TRUE(retry.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), installed);
    EXPECT_FALSE(executeWeaponComponentChange(profile, content(),
        {weapon, WeaponComponentSlot::Muzzle, 0}, {revision, "stale-remove"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), installed);
}

TEST(WeaponSuppressorAcousticsTest, CarriedMassCountsTheInstalledChildOnceAndRemovalRestoresStats)
{
    auto profile = supplied();
    const auto weapon = find(profile, rifle), part = looseSuppressor(profile);
    ASSERT_TRUE(executeInventory(profile, content(),
        InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "equip"}).succeeded);
    const auto baseMass = carriedWeightGrams(profile, content());
    const auto baseUse = effectiveWeaponUse(profile, content(), weapon);
    const auto highWater = profile.assets.nextAssetId();
    ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, part,
        "install").succeeded);
    EXPECT_EQ(carriedWeightGrams(profile, content()), baseMass + 350U);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, 0,
        "remove").succeeded);
    EXPECT_EQ(carriedWeightGrams(profile, content()), baseMass);
    EXPECT_EQ(effectiveWeaponUse(profile, content(), weapon), baseUse);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(part)->location));
}

TEST(WeaponSuppressorAcousticsTest, FullStashRejectsRemovalAtomically)
{
    auto profile = supplied();
    const auto weapon = find(profile, rifle), part = looseSuppressor(profile);
    ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, part,
        "install").succeeded);
    const auto &filler = content().item(ItemDefinitionId{"item.component.vertical_grip"});
    while (auto position = findFirstProfileFit(profile, content(),
        ProfileContainerId::stash(), filler, ItemOrientation::Degrees0))
        static_cast<void>(profile.assets.create(filler,
            StoredAssetLocation{ProfileContainerId::stash(), *position}));
    const auto full = profileStateFingerprint(profile);
    const auto denied = change(profile, weapon, WeaponComponentSlot::Muzzle, 0,
        "remove-full");
    EXPECT_FALSE(denied.succeeded);
    EXPECT_EQ(denied.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    EXPECT_EQ(effectiveWeaponUse(profile, content(), weapon).acoustics,
        (WeaponAcousticsDefinition{0.5F, 0.55F, 0.25F}));
}

TEST(WeaponSuppressorAcousticsTest, ReplacingMuzzleReturnsTheOldInstanceWithoutStackingItsEffect)
{
    auto profile = supplied();
    const auto weapon = find(profile, rifle), original = looseSuppressor(profile);
    ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, original,
        "install-first").succeeded);
    ASSERT_TRUE(executeEconomy(profile, content(), PurchaseCommand{suppressor, 1},
        {profile.revision, "buy-replacement"}).succeeded);
    const auto replacement = looseSuppressor(profile);
    ASSERT_NE(replacement, 0U);
    ASSERT_NE(original, replacement);
    const auto use = effectiveWeaponUse(profile, content(), weapon);
    const auto plan = queryWeaponComponentChange(profile, content(),
        {weapon, WeaponComponentSlot::Muzzle, replacement});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.returned.size(), 1U);
    EXPECT_EQ(plan.returned.front().assetId, original);
    ASSERT_TRUE(change(profile, weapon, WeaponComponentSlot::Muzzle, replacement,
        "replace").succeeded);
    EXPECT_EQ(effectiveWeaponUse(profile, content(), weapon), use);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(original)->location));
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(
        profile.assets.find(replacement)->location).weaponAssetId, weapon);
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}
