#include <gtest/gtest.h>

#include <limits>

#include "base_population_domain.h"
#include "raid_rescue_domain.h"

namespace
{
const OrdinarySurvivorAdmissionCommand kGreylineRescue{
    RescueDefinitionId{"rescue.ordinary.greyline_depot"},
    1U};
const OrdinarySurvivorAdmissionCommand kAshworksRescue{
    RescueDefinitionId{"rescue.ordinary.ashworks_yard"},
    1U,
    1U};
}

TEST(RaidRescueDomainTest, QueryProjectsResidentsBedsAndDailyRations)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-query", publishedContentRegistry());
    profile.basePopulation = BasePopulationState{10U, 10U};
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const OrdinarySurvivorAdmissionPlan plan =
        queryOrdinarySurvivorAdmission(profile, kGreylineRescue);

    EXPECT_TRUE(plan.canCommit);
    EXPECT_FALSE(plan.alreadyCommitted);
    EXPECT_EQ(plan.residentsBefore, 10U);
    EXPECT_EQ(plan.residentsAfter, 11U);
    EXPECT_EQ(plan.bedCapacity, 10U);
    EXPECT_EQ(plan.bedShortfallAfter, 1U);
    EXPECT_EQ(plan.dailyRationsAfter, 11U);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidRescueDomainTest, ExecuteCommitsExactlyOnceWithStableRescueId)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-commit", publishedContentRegistry());
    const ProfileRevision revision = profile.revision;

    const OrdinarySurvivorAdmissionReceipt admitted =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            kGreylineRescue,
            CommandContext{revision, "rescue-commit-1"});

    ASSERT_TRUE(admitted.succeeded) << admitted.message;
    EXPECT_FALSE(admitted.alreadyCommitted);
    EXPECT_EQ(admitted.admittedResidents, 1U);
    EXPECT_EQ(admitted.residentsAfter, 9U);
    EXPECT_EQ(profile.revision, revision + 1U);
    EXPECT_TRUE(profile.committedRescues.contains(
        kGreylineRescue.rescueDefinitionId));
    const std::uint64_t committedFingerprint =
        profileStateFingerprint(profile);

    const OrdinarySurvivorAdmissionReceipt repeated =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            kGreylineRescue,
            CommandContext{profile.revision, "rescue-commit-2"});

    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(repeated.admittedResidents, 0U);
    EXPECT_EQ(profileStateFingerprint(profile), committedFingerprint);
}

TEST(RaidRescueDomainTest, AshworksAdmissionCarriesPublishedInjuryFact)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-injured-admission", publishedContentRegistry());

    const OrdinarySurvivorAdmissionReceipt admitted =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            kAshworksRescue,
            CommandContext{profile.revision, "rescue-injured-admission"});

    ASSERT_TRUE(admitted.succeeded) << admitted.message;
    EXPECT_EQ(admitted.admittedResidents, 1U);
    EXPECT_EQ(admitted.admittedInjuredResidents, 1U);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, 9U);
    EXPECT_EQ(profile.basePopulation.injuredResidents, 1U);
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(RaidRescueDomainTest, RejectionsPreserveProfileFingerprint)
{
    ProfileState profile = makeNewAlphaProfile(
        "rescue-reject", publishedContentRegistry());
    const std::uint64_t startingFingerprint =
        profileStateFingerprint(profile);

    const OrdinarySurvivorAdmissionReceipt stale =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            kGreylineRescue,
            CommandContext{profile.revision + 1U, "rescue-stale"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, RaidRescueError::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), startingFingerprint);

    const OrdinarySurvivorAdmissionCommand zeroResidents{
        kGreylineRescue.rescueDefinitionId,
        0U};
    const OrdinarySurvivorAdmissionReceipt invalid =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            zeroResidents,
            CommandContext{profile.revision, "rescue-invalid"});
    EXPECT_FALSE(invalid.succeeded);
    EXPECT_EQ(invalid.error, RaidRescueError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), startingFingerprint);

    const OrdinarySurvivorAdmissionReceipt mismatchedContent =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            OrdinarySurvivorAdmissionCommand{
                kGreylineRescue.rescueDefinitionId,
                2U},
            CommandContext{profile.revision, "rescue-mismatched-count"});
    EXPECT_FALSE(mismatchedContent.succeeded);
    EXPECT_EQ(mismatchedContent.error, RaidRescueError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), startingFingerprint);

    const OrdinarySurvivorAdmissionReceipt unknownDefinition =
        executeOrdinarySurvivorAdmission(
            profile,
            publishedContentRegistry(),
            OrdinarySurvivorAdmissionCommand{
                RescueDefinitionId{"rescue.ordinary.unknown"},
                1U},
            CommandContext{profile.revision, "rescue-unknown"});
    EXPECT_FALSE(unknownDefinition.succeeded);
    EXPECT_EQ(unknownDefinition.error, RaidRescueError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), startingFingerprint);
}

TEST(RaidRescueDomainTest, PopulationAndRevisionBoundsAreExplicit)
{
    ProfileState full = makeNewAlphaProfile(
        "rescue-full", publishedContentRegistry());
    full.basePopulation.ordinaryResidents = kMaximumOrdinaryResidents;
    const OrdinarySurvivorAdmissionPlan overflow =
        queryOrdinarySurvivorAdmission(full, kGreylineRescue);
    EXPECT_FALSE(overflow.canCommit);
    EXPECT_EQ(overflow.error, RaidRescueError::PopulationOverflow);

    ProfileState finalRevision = makeNewAlphaProfile(
        "rescue-final-revision", publishedContentRegistry());
    finalRevision.revision = std::numeric_limits<ProfileRevision>::max();
    const OrdinarySurvivorAdmissionPlan revisionOverflow =
        queryOrdinarySurvivorAdmission(finalRevision, kGreylineRescue);
    EXPECT_FALSE(revisionOverflow.canCommit);
    EXPECT_EQ(revisionOverflow.error, RaidRescueError::RevisionOverflow);
}
