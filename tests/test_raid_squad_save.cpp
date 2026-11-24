#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "base_workforce_domain.h"
#include "raid_squad_save.h"
#include "save_repository.h"
#include "test_temp_directory.h"

TEST(RaidSquadSaveTest, OldBaseProfileKeepsExactSchema75EncodingAndFingerprint)
{
    const auto &content = publishedContentRegistry();
    const auto original = makeNewAlphaProfile("solo",content);
    auto previousBytes = serializeProfileEnvelope(original,content.contentVersion(),75);
    ASSERT_EQ(previousBytes,serializeProfileEnvelope(original,content.contentVersion()));
    auto loaded = deserializeProfileEnvelope(previousBytes,content);
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_FALSE(hasRaidSquadSaveData(*loaded.profile));
    EXPECT_EQ(profileStateFingerprint(original), profileStateFingerprint(*loaded.profile));
    EXPECT_EQ(previousBytes, serializeProfileEnvelope(*loaded.profile,content.contentVersion()));
}

TEST(RaidSquadSaveTest, NewRoleSurvivesDiskRoundTripAndRejectsLossyDowngrade)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("squad-reopen",content);
    raidline::test::TemporaryDirectory temp;
    temp.claim("raidline-squad-save-","migration");
    SaveRepository repository(temp.path());
    ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);
    profile.squadRoster[RaidSquadRole::Support] = {RaidSquadResidentStatus::Ready,0,2};
    ++profile.basePopulation.ordinaryResidents;
    ++profile.basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::Combat)];
    const auto fingerprint = profileStateFingerprint(profile);
    const auto bytes = serializeProfileEnvelope(profile,content.contentVersion());
    ASSERT_EQ(nlohmann::json::parse(bytes).at("schema_version"),76);
    EXPECT_THROW(serializeProfileEnvelope(profile,content.contentVersion(),75),std::invalid_argument);
    ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);
    const auto primary = SaveRepository(temp.path()).load(content);
    ASSERT_TRUE(primary.profile.has_value()) << primary.message;
    EXPECT_EQ(primary.profile->squadRoster,profile.squadRoster);
    EXPECT_EQ(profileStateFingerprint(*primary.profile),fingerprint);
    auto changed = profile;
    ++changed.squadRoster.at(RaidSquadRole::Support).generation;
    EXPECT_NE(profileStateFingerprint(changed),fingerprint);
}

TEST(RaidSquadSaveTest, RejectsDuplicatedRoleMalformedPatientAndOverflowBeforePublishing)
{
    auto p = makeNewAlphaProfile("malformed",publishedContentRegistry());
    p.squadRoster[RaidSquadRole::Sniper] = {RaidSquadResidentStatus::Ready,0,1};
    const auto original = writeRaidSquadSave(p);
    const auto rejects = [](const nlohmann::json &invalid) {
        auto target = makeNewAlphaProfile("target",publishedContentRegistry());
        // Decode from real JSON text to exercise the unsigned integer representation on disk.
        EXPECT_THROW(readRaidSquadSave(nlohmann::json::parse(invalid.dump()),target),std::invalid_argument);
    };
    auto duplicate = original;
    duplicate["roster"].push_back(duplicate.at("roster").front());
    rejects(duplicate);
    auto invalid = original;
    invalid["roster"][0]["generation"] = 4294967296ULL;
    rejects(invalid);
    invalid = original;
    invalid["patient"] = "sniper";
    rejects(invalid);
}

TEST(RaidSquadSaveTest, PreservesLoanCoordinatesAndOrientationForEachMember)
{
    auto p = makeNewAlphaProfile("squad-placements",publishedContentRegistry());
    auto &raid = p.pendingRaid.emplace();
    raid.specialistRescue = RaidSquadRole::Support;
    raid.squadOrder = {RaidSquadRole::Sniper,RaidSquadRole::Fire,RaidSquadRole::Support};
    raid.fireSupportHealth = 30;
    raid.fireSupportCargo.push_back({123,{3,7},ItemOrientation::Degrees90});
    raid.squadMembers = {{RaidSquadRole::Sniper,200,{201,{5,9},ItemOrientation::Degrees270},{},41},
        {RaidSquadRole::Support,300,{301,{10,2},ItemOrientation::Degrees180},{{302,{4,1},ItemOrientation::Degrees0}},100}};
    auto copy = makeNewAlphaProfile("squad-placements",publishedContentRegistry());
    copy.pendingRaid.emplace();
    readRaidSquadSave(nlohmann::json::parse(writeRaidSquadSave(p).dump()),copy);
    EXPECT_EQ(copy.pendingRaid->squadMembers,raid.squadMembers);
    EXPECT_EQ(copy.pendingRaid->fireSupportCargo,raid.fireSupportCargo);
    EXPECT_EQ(copy.pendingRaid->specialistRescue,raid.specialistRescue);
    EXPECT_EQ(copy.pendingRaid->squadOrder,raid.squadOrder);
    EXPECT_EQ(profileStateFingerprint(copy),profileStateFingerprint(p));
}
