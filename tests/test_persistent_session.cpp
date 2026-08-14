#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "alpha_content_ids.h"
#include "base_world.h"
#include "game_session.h"

namespace
{
class SessionSaveDirectory
{
public:
    SessionSaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-session-test-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))}
    {
    }

    ~SessionSaveDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

AssetInstanceId findDefinition(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            return id;
        }
    }
    return 0;
}
}

TEST(PersistentSessionTest, AutosavedInventoryCommandSurvivesNewProcessSession)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-profile"));
    const AssetInstanceId rifle = findDefinition(
        first.profile(),
        alpha_content::rifle);
    ASSERT_NE(rifle, 0U);

    const InventoryReceipt receipt = first.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "persistent-equip");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const std::uint64_t fingerprint = profileStateFingerprint(first.profile());

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), fingerprint);
    EXPECT_EQ(
        equippedAsset(reopened.profile(), EquipmentSlotKind::PrimaryWeapon),
        rifle);
}

TEST(PersistentSessionTest, EnvironmentObjectiveAdvancesWithoutBlockingPlay)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("tutorial-profile"));

    session.noteBaseFacility(BaseFacilityKind::Storage);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::PrepareLoadout);

    const AssetInstanceId rifle = findDefinition(
        session.profile(),
        alpha_content::rifle);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "tutorial-equip").succeeded);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::FindRaidGate);

    session.noteBaseFacility(BaseFacilityKind::RaidGate);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::Complete);
}

TEST(PersistentSessionTest, SaveFailureDoesNotSwapCandidateIntoMemory)
{
    SessionSaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }

    GameSession session;
    session.configurePersistence(invalidDirectory);
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const AssetInstanceId rifle = findDefinition(
        session.profile(),
        alpha_content::rifle);

    const InventoryReceipt receipt = session.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}
