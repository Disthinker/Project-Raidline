#include "raid_corpse_search.h"
#include "raid_lifecycle.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <vector>

namespace
{
const auto &content = publishedContentRegistry();

ProfileState deployedCorpseProfile()
{
    // Deployment performs the cold, complete validation once. Each test then
    // owns a copy, as the simulation-only entry requires a validated baseline.
    static const ProfileState baseline = []
    {
        auto profile = makeNewAlphaProfile("grenade-corpse-runtime", content);
        const auto receipt = executeDeploy(profile, content,
            {"grenade-corpse-raid", "grenade-corpse-settle", 9917,
             MapDefinitionId{"map.raid.frontier_exchange"}}, {profile.revision, "deploy"});
        EXPECT_TRUE(receipt.succeeded) << receipt.message;
        return profile;
    }();
    return baseline;
}

RaidLootSnapshot &lootFor(ProfileState &profile, AssetInstanceId root)
{
    return *std::find_if(profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
        [root](const auto &loot) { return loot.corpseContainerAssetId == root; });
}

class GrenadeCorpseRuntimeTest : public testing::Test
{
protected:
    ProfileState profile;
    std::vector<AssetInstanceId> bodies;
    RaidCorpseDeathFact fact;

    void SetUp() override
    {
        profile = deployedCorpseProfile();
        ASSERT_TRUE(profile.pendingRaid);
        for (const auto &[id, asset] : profile.assets.records())
            if (std::holds_alternative<RaidCorpseContainerAssetLocation>(asset.location)) bodies.push_back(id);
        ASSERT_EQ(bodies.size(), 3U);
        fact = factFor(bodies.front(), {1000, 1000});
    }

    RaidCorpseDeathFact factFor(AssetInstanceId root, Vec2 position) const
    {
        const auto &location = std::get<RaidCorpseContainerAssetLocation>(profile.assets.find(root)->location);
        return {location.sourceEnemyId, location.spaceId, position};
    }

    template <typename Change>
    void rejectsMutation(const char *label, Change change)
    {
        SCOPED_TRACE(label);
        auto invalid = profile;
        change(invalid);
        const auto before = profileStateFingerprint(invalid);
        const auto highWater = invalid.assets.nextAssetId();
        EXPECT_FALSE(revealRaidCorpseInSimulation(invalid, content, fact));
        EXPECT_EQ(profileStateFingerprint(invalid), before);
        EXPECT_EQ(invalid.assets.nextAssetId(), highWater);
    }
};

TEST_F(GrenadeCorpseRuntimeTest, ThreeSuccessfulDeathsMatchStrictFingerprintsAndPreserveFrozenLoot)
{
    auto strict = profile;
    const auto highWater = profile.assets.nextAssetId();
    const auto frozenLoot = profile.pendingRaid->loot;
    const auto startingRevision = profile.revision;
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
        const auto death = factFor(bodies[i], {1000 + static_cast<float>(i) * 75, 1000});
        ASSERT_TRUE(revealRaidCorpse(strict, content, death));
        ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, death));
        EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
        EXPECT_EQ(profile.revision, startingRevision + i + 1U);
        EXPECT_EQ(profile.assets.nextAssetId(), highWater);
        EXPECT_EQ(profile.pendingRaid->loot, frozenLoot);
    }
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(GrenadeCorpseRuntimeTest, InclusiveMapEdgesMatchStrictReveals)
{
    auto strict = profile;
    const auto size = content.map(profile.pendingRaid->mapDefinitionId).worldSize;
    for (const auto death : {factFor(bodies[0], {0, 0}), factFor(bodies[1], size)})
    {
        ASSERT_TRUE(revealRaidCorpse(strict, content, death));
        ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, death));
        EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
    }
}

TEST_F(GrenadeCorpseRuntimeTest, DuplicateFactsNeverMoveTheBodyOrAdvanceRevisionEvenAtOverflow)
{
    auto strict = profile;
    ASSERT_TRUE(revealRaidCorpse(strict, content, fact));
    ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, fact));
    for (const auto position : {Vec2{1500, 1500}, Vec2{-1, -1}})
    {
        const auto before = profileStateFingerprint(profile);
        const RaidCorpseDeathFact repeated{fact.sourceEnemyId, fact.spaceId, position};
        EXPECT_TRUE(revealRaidCorpse(strict, content, repeated));
        EXPECT_TRUE(revealRaidCorpseInSimulation(profile, content, repeated));
        EXPECT_EQ(profileStateFingerprint(profile), before);
        EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
    }
    strict.revision = profile.revision = std::numeric_limits<ProfileRevision>::max();
    const auto before = profileStateFingerprint(profile);
    EXPECT_TRUE(revealRaidCorpse(strict, content, fact));
    EXPECT_TRUE(revealRaidCorpseInSimulation(profile, content, fact));
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
}

TEST_F(GrenadeCorpseRuntimeTest, CollectedAndConsumedFrozenLootDoesNotBreakTheOnceOnlyRule)
{
    ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, fact));
    auto &loot = lootFor(profile, bodies.front());
    loot.collected = true;
    ASSERT_TRUE(profile.assets.erase(loot.assetId));
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    auto strict = profile;
    const auto before = profileStateFingerprint(profile);
    EXPECT_TRUE(revealRaidCorpse(strict, content, fact));
    EXPECT_TRUE(revealRaidCorpseInSimulation(profile, content, fact));
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
}

TEST_F(GrenadeCorpseRuntimeTest, InvalidFactsMatchStrictRejectionWithoutAnyMutation)
{
    const auto size = content.map(profile.pendingRaid->mapDefinitionId).worldSize;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    const std::vector<RaidCorpseDeathFact> invalid{
        {"", fact.spaceId, fact.position}, {"unknown", fact.spaceId, fact.position},
        {fact.sourceEnemyId, RaidSpaceDefinitionId{"raid_space.unknown"}, fact.position},
        {fact.sourceEnemyId, fact.spaceId, {nan, 0}},
        {fact.sourceEnemyId, fact.spaceId, {0, infinity}},
        {fact.sourceEnemyId, fact.spaceId, {-1, 0}},
        {fact.sourceEnemyId, fact.spaceId, {0, -1}},
        {fact.sourceEnemyId, fact.spaceId, {size.x + 1, size.y}},
        {fact.sourceEnemyId, fact.spaceId, {size.x, size.y + 1}}};
    auto strict = profile;
    const auto before = profileStateFingerprint(profile);
    for (const auto &death : invalid)
    {
        EXPECT_FALSE(revealRaidCorpse(strict, content, death));
        EXPECT_FALSE(revealRaidCorpseInSimulation(profile, content, death));
        EXPECT_EQ(profileStateFingerprint(profile), before);
        EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(strict));
    }
}

TEST_F(GrenadeCorpseRuntimeTest, MissingRaidWrongVersionAndRevisionOverflowAreAtomic)
{
    rejectsMutation("no pending raid", [](auto &p) { p.pendingRaid.reset(); });
    rejectsMutation("wrong corpse rules", [](auto &p) { p.pendingRaid->rulesVersion = "content-beta-loot-economy-29"; });
    rejectsMutation("wrong map", [](auto &p) { p.pendingRaid->mapDefinitionId = MapDefinitionId{"map.unknown"}; });
    rejectsMutation("missing rollback baseline", [](auto &p) { p.pendingRaid->startingAssets.reset(); });
    rejectsMutation("revision exhausted", [](auto &p) { p.revision = std::numeric_limits<ProfileRevision>::max(); });
}

TEST_F(GrenadeCorpseRuntimeTest, WrongRootIdentityCountSourceAndRaidCannotBeCommitted)
{
    const auto root = bodies.front();
    rejectsMutation("root instance identity", [root](auto &p) { p.assets.findMutable(root)->instanceId = 0; });
    rejectsMutation("root count", [root](auto &p) { p.assets.findMutable(root)->quantity = 2; });
    rejectsMutation("wrong raid", [root](auto &p) { std::get<RaidCorpseContainerAssetLocation>(p.assets.findMutable(root)->location).raidId = "another-raid"; });
    rejectsMutation("wrong source", [root](auto &p) { std::get<RaidCorpseContainerAssetLocation>(p.assets.findMutable(root)->location).sourceEnemyId = "unknown"; });
    rejectsMutation("missing root", [root](auto &p) { static_cast<void>(p.assets.erase(root)); });
    rejectsMutation("wrong root definition", [root](auto &p) { p.assets.findMutable(root)->definitionId = ItemDefinitionId{"item.throwable.frag_grenade_basic"}; });
    rejectsMutation("enemy source binding", [&](auto &p)
    {
        for (auto &enemy : p.pendingRaid->enemies)
            if (enemy.sourceEnemyId == fact.sourceEnemyId) { enemy.sourceEnemyId = "wrong-source"; break; }
    });
    rejectsMutation("duplicate source root", [&](auto &p)
    {
        std::get<RaidCorpseContainerAssetLocation>(p.assets.findMutable(bodies.back())->location).sourceEnemyId = fact.sourceEnemyId;
    });
}

TEST_F(GrenadeCorpseRuntimeTest, FrozenLootOriginsCountsAndActualOwnershipAreRechecked)
{
    const auto root = bodies.front();
    rejectsMutation("missing frozen binding", [root](auto &p) { lootFor(p, root).corpseContainerAssetId = 0; });
    rejectsMutation("duplicate frozen binding", [root](auto &p) { p.pendingRaid->loot.push_back(lootFor(p, root)); });
    rejectsMutation("wrong source point", [root](auto &p) { lootFor(p, root).resourcePointInstanceId = "unknown-point"; });
    rejectsMutation("mixed scene origin", [root](auto &p) { lootFor(p, root).sceneContainerAssetId = root; });
    rejectsMutation("collected before first reveal", [root](auto &p) { lootFor(p, root).collected = true; });
    rejectsMutation("zero frozen quantity", [root](auto &p) { lootFor(p, root).quantity = 0; });
    rejectsMutation("mismatched actual quantity", [root](auto &p) { p.assets.findMutable(lootFor(p, root).assetId)->quantity += 1U; });
    rejectsMutation("wrong actual definition", [root](auto &p) { p.assets.findMutable(lootFor(p, root).assetId)->definitionId = ItemDefinitionId{"item.throwable.frag_grenade_basic"}; });
    rejectsMutation("wrong actual identity", [root](auto &p) { p.assets.findMutable(lootFor(p, root).assetId)->instanceId = 0; });
    rejectsMutation("wrong actual owner", [root](auto &p)
    {
        p.assets.findMutable(lootFor(p, root).assetId)->location = StoredAssetLocation{ProfileContainerId::stash(), {0, 0}};
    });
    rejectsMutation("wrong compartment", [root](auto &p)
    {
        std::get<StoredAssetLocation>(p.assets.findMutable(lootFor(p, root).assetId)->location).container.compartmentIndex = 1;
    });
}

TEST_F(GrenadeCorpseRuntimeTest, CorruptFrozenPositionsAndStorageBoundsRejectWithoutPartialReveal)
{
    const auto root = bodies.front();
    rejectsMutation("nonfinite frozen origin", [root](auto &p) { lootFor(p, root).position.x = std::numeric_limits<float>::infinity(); });
    rejectsMutation("moved frozen origin", [root](auto &p) { lootFor(p, root).position.x += 1; });
    rejectsMutation("wrong frozen slot", [root](auto &p) { ++lootFor(p, root).slotIndex; });
    rejectsMutation("invalid storage x", [root](auto &p)
    {
        std::get<StoredAssetLocation>(p.assets.findMutable(lootFor(p, root).assetId)->location).origin.x = -1;
    });
    rejectsMutation("invalid storage extent", [root](auto &p)
    {
        std::get<StoredAssetLocation>(p.assets.findMutable(lootFor(p, root).assetId)->location).origin.y = std::numeric_limits<int>::max();
    });
    rejectsMutation("invalid orientation", [root](auto &p) { p.assets.findMutable(lootFor(p, root).assetId)->orientation = static_cast<ItemOrientation>(-1); });
    rejectsMutation("extra unrevealed loot", [root](auto &p)
    {
        static_cast<void>(p.assets.create(content.item(ItemDefinitionId{"item.throwable.frag_grenade_basic"}),
            StoredAssetLocation{ProfileContainerId::compartment(root, 0), {8, 7}}));
    });
}
}
