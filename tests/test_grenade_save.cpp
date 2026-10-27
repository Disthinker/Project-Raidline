#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "economy_domain.h"
#include "game_session.h"
#include "grenade_domain.h"
#include "noise_decoy_fixture.h"
#include "raid_lifecycle.h"
#include "save_repository.h"
#include "weapon_component_domain.h"

namespace
{
const ItemDefinitionId grenade{"item.throwable.frag_grenade_basic"};
class GrenadeSaveDirectory
{
public:
    GrenadeSaveDirectory() : path_{std::filesystem::temp_directory_path()/
        ("raidline-grenade-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))}
    { std::filesystem::create_directories(path_); }
    ~GrenadeSaveDirectory() { std::error_code error;std::filesystem::remove_all(path_,error); }
    const auto &path() const { return path_; }
private:
    std::filesystem::path path_;
};
ContentRegistry content71()
{
    auto root=nlohmann::json::parse(publishedContentJson());
    root["content_version"]="weapon-suppressor-acoustics-content-71";
    auto &items=root["items"];
    items.erase(std::remove_if(items.begin(),items.end(),[](const auto &item) {
        return item.at("id").template get<std::string>()==std::string{grenade.value()};
    }),items.end());
    return ContentRegistry::fromJson(root.dump());
}
std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream input{path,std::ios::binary};
    return {std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
}
void writeBytes(const std::filesystem::path &path,std::string_view bytes)
{
    std::ofstream output{path,std::ios::binary|std::ios::trunc};
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));ASSERT_TRUE(output.good());
}
std::string sourceChecksum(std::string_view bytes)
{
    std::uint64_t value=1469598103934665603ULL;
    for(unsigned char byte:bytes){value^=byte;value*=1099511628211ULL;}
    constexpr char digits[]="0123456789abcdef";
    std::string result(16,'0');
    for(int index=15;index>=0;--index){result[static_cast<std::size_t>(index)]=digits[value&0xfU];value>>=4U;}
    return result;
}
std::filesystem::path archive(const std::filesystem::path &path,bool backup,std::string_view bytes)
{
    return path/(std::string{backup?"profile.backup.pre-":"profile.pre-"}+
        "raid-fragmentation-grenade-v1."+sourceChecksum(bytes)+".json");
}
std::vector<std::filesystem::path> archives(const std::filesystem::path &path)
{
    std::vector<std::filesystem::path> result;
    for(const auto &entry:std::filesystem::directory_iterator{path})
        if(entry.path().filename().string().find(".pre-raid-fragmentation-grenade-v1.")!=std::string::npos)
            result.push_back(entry.path());
    std::sort(result.begin(),result.end());return result;
}
AssetInstanceId find(const ProfileState &profile,const ItemDefinitionId &definition)
{
    for(const auto &[id,asset]:profile.assets.records())if(asset.definitionId==definition)return id;
    return 0;
}
AssetInstanceId addGrenade(ProfileState &profile,std::uint32_t quantity=2)
{
    const auto &content=publishedContentRegistry();equipDecoyTestRig(profile,content);
    const auto rig=equippedAsset(profile,EquipmentSlotKind::ChestRig);if(!rig)return 0;
    const auto &definition=content.item(grenade);
    const auto &pockets=content.item(profile.assets.find(*rig)->definitionId).containerCompartments;
    for(std::uint32_t index{};index<pockets.size();++index)if(pockets[index].pocketKind==ContainerPocketKind::General) {
        const auto container=ProfileContainerId::compartment(*rig,index);
        if(const auto fit=findFirstProfileFit(profile,content,container,definition,ItemOrientation::Degrees0))
            return profile.assets.create(definition,StoredAssetLocation{container,*fit},quantity);
    }
    return 0;
}
}

TEST(GrenadeSaveTest, NewAndCompatibilityCatalogsNeverGrantGrenades)
{
    const auto &content=publishedContentRegistry();
    auto profile=makeNewPublishedProfile("grenade-no-grant",content);
    EXPECT_EQ(find(profile,grenade),0U);EXPECT_EQ(profile.assets.records().size(),59U);
    profile.committedTransactions.erase("bootstrap.warehouse_catalog.content_54");
    const auto grant=grantPublishedWarehouseCatalog(profile,content);
    ASSERT_TRUE(grant.succeeded)<<grant.message;EXPECT_EQ(find(profile,grenade),0U);
    auto developer=makeNewAlphaProfile("grenade-no-developer-grant",content);
    const auto all=grantDeveloperWarehouseCatalog(developer,content);
    ASSERT_TRUE(all.succeeded)<<all.message;EXPECT_EQ(find(developer,grenade),0U);
    const auto before=profileStateFingerprint(developer);
    EXPECT_TRUE(grantDeveloperWarehouseCatalog(developer,content).alreadyGranted);
    EXPECT_EQ(profileStateFingerprint(developer),before);
}

TEST(GrenadeSaveTest, Content71PreservesInstalledSuppressorAndFrozenRaidWithoutNewItems)
{
    const auto old=content71();const auto &content=publishedContentRegistry();
    auto profile=makeNewPublishedProfile("grenade-old-frozen",old);profile.currency=1000;
    const ItemDefinitionId suppressor{"item.component.suppressor_5_45"}, rifle{"item.weapon.rifle_5_45_service"};
    ASSERT_TRUE(executeEconomy(profile,old,PurchaseCommand{suppressor,1},{profile.revision,"old-part"}).succeeded);
    ASSERT_TRUE(executeWeaponComponentChange(profile,old,{find(profile,rifle),WeaponComponentSlot::Muzzle,find(profile,suppressor)},
        {profile.revision,"old-install"}).succeeded);
    ASSERT_TRUE(executeDeploy(profile,old,DeployCommand{"grenade-old-raid","grenade-old-result",9151U,
        MapDefinitionId{"map.raid.frontier_exchange"}},{profile.revision,"old-deploy"}).succeeded);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(profile,old.contentVersion()),content);
    ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid)<<loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(profile));
    EXPECT_EQ(loaded.profile->assets.nextAssetId(),profile.assets.nextAssetId());EXPECT_EQ(find(*loaded.profile,grenade),0U);
    EXPECT_FLOAT_EQ(effectiveWeaponUse(*loaded.profile,content,find(profile,rifle)).acoustics.noiseRadiusMultiplier,0.5F);
}

TEST(GrenadeSaveTest, Content71UpgradeKeepsBothByteExactSourcesImmutable)
{
    const auto &content=publishedContentRegistry();GrenadeSaveDirectory directory;SaveRepository repository{directory.path()};
    auto profile=makeNewAlphaProfile("grenade-upgrade",content);
    const auto primary=serializeProfileEnvelope(profile,"weapon-suppressor-acoustics-content-71");
    ++profile.revision;const auto backup=serializeProfileEnvelope(profile,"weapon-suppressor-acoustics-content-71");
    writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
    const auto loaded=repository.load(content);ASSERT_TRUE(loaded.profile)<<loaded.message;profile=*loaded.profile;
    ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    ++profile.revision;ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);const auto immutable=archives(directory.path());
    for(int i{};i<3;++i){++profile.revision;ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);}
    EXPECT_EQ(archives(directory.path()),immutable);
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    const auto current=nlohmann::json::parse(readBytes(repository.primaryPath()));
    EXPECT_EQ(current.at("schema_version"),57U);EXPECT_EQ(current.at("content_version"),content.contentVersion());
}

TEST(GrenadeSaveTest, PendingMirrorPreservesOldSourcesAndDoesNotRerollFrozenLoot)
{
    const auto old=content71();const auto &content=publishedContentRegistry();
    GrenadeSaveDirectory directory;SaveRepository repository{directory.path()};auto profile=makeNewPublishedProfile("grenade-mirror",old);
    ASSERT_TRUE(executeDeploy(profile,old,DeployCommand{"grenade-mirror-raid","grenade-mirror-result",9152U,
        MapDefinitionId{"map.raid.frontier_exchange"}},{profile.revision,"deploy"}).succeeded);
    const auto primary=serializeProfileEnvelope(profile,old.contentVersion());++profile.revision;
    const auto backup=serializeProfileEnvelope(profile,old.contentVersion());writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
    const auto loaded=repository.load(content);ASSERT_TRUE(loaded.profile && loaded.profile->pendingRaid)<<loaded.message;
    ASSERT_TRUE(repository.save(*loaded.profile,content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    EXPECT_EQ(readBytes(repository.primaryPath()),readBytes(repository.backupPath()));const auto reopened=repository.load(content);
    ASSERT_TRUE(reopened.profile && reopened.profile->pendingRaid)<<reopened.message;
    EXPECT_EQ(profileStateFingerprint(*reopened.profile),profileStateFingerprint(*loaded.profile));EXPECT_EQ(find(*reopened.profile,grenade),0U);
}

TEST(GrenadeSaveTest, ArchiveConflictAndCreationFailureDoNotReplaceRecoverySources)
{
    const auto &content=publishedContentRegistry();
    for(int mode{};mode<3;++mode) {
        SCOPED_TRACE(mode);GrenadeSaveDirectory directory;SaveRepository repository{directory.path()};
        auto profile=makeNewAlphaProfile("grenade-archive-failure",content);
        const auto primary=serializeProfileEnvelope(profile,"weapon-suppressor-acoustics-content-71");++profile.revision;
        const auto backup=serializeProfileEnvelope(profile,"weapon-suppressor-acoustics-content-71");
        writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);
        const auto blocked=archive(directory.path(),mode==1,mode==1?backup:primary);
        if(mode==2)std::filesystem::create_directory(blocked);else writeBytes(blocked,"different immutable bytes");
        EXPECT_FALSE(repository.save(profile,content.contentVersion()).succeeded);
        EXPECT_EQ(readBytes(repository.primaryPath()),primary);EXPECT_EQ(readBytes(repository.backupPath()),backup);
        if(mode!=2)EXPECT_EQ(readBytes(blocked),"different immutable bytes");else EXPECT_TRUE(std::filesystem::is_directory(blocked));
        EXPECT_FALSE(std::filesystem::exists(directory.path()/"profile.tmp.json"));
    }
}

TEST(GrenadeSaveTest, OldBackupRecoveryPreservesCorruptPrimaryAndCurrentSavesDoNotArchive)
{
    const auto &content=publishedContentRegistry();GrenadeSaveDirectory directory;SaveRepository repository{directory.path()};
    auto profile=makeNewAlphaProfile("grenade-corrupt-main",content);
    const auto backup=serializeProfileEnvelope(profile,"weapon-suppressor-acoustics-content-71");const std::string primary{"{corrupt source\r\n"};
    writeBytes(repository.primaryPath(),primary);writeBytes(repository.backupPath(),backup);const auto recovered=repository.load(content);
    ASSERT_EQ(recovered.status,SaveLoadStatus::RecoveredBackup);ASSERT_TRUE(recovered.profile)<<recovered.message;
    ASSERT_TRUE(repository.save(*recovered.profile,content.contentVersion()).succeeded);
    EXPECT_EQ(readBytes(archive(directory.path(),false,primary)),primary);EXPECT_EQ(readBytes(archive(directory.path(),true,backup)),backup);
    ++profile.revision;ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);const auto immutable=archives(directory.path());
    ++profile.revision;ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);EXPECT_EQ(archives(directory.path()),immutable);
}

TEST(GrenadeSaveTest, PartialStackSurvivesExtractionAndSchema57DiskRoundTrip)
{
    const auto &content=publishedContentRegistry();auto profile=makeNewPublishedProfile("grenade-partial",content);
    const auto id=addGrenade(profile);ASSERT_NE(id,0U);const auto location=profile.assets.find(id)->location;
    ASSERT_TRUE(executeDeploy(profile,content,DeployCommand{"grenade-partial-raid","grenade-partial-result",9153U,
        MapDefinitionId{"map.v0.test"}},{profile.revision,"deploy"}).succeeded);
    const CommandContext context{profile.revision,"throw-one"};ASSERT_TRUE(consumeGrenade(profile,content,id,context).succeeded);
    EXPECT_TRUE(consumeGrenade(profile,content,id,context).alreadyCommitted);EXPECT_EQ(profile.assets.find(id)->quantity,1U);
    ASSERT_TRUE(settlePendingRaid(profile,content,"grenade-partial-result",RaidResultOutcome::Extracted).succeeded);
    GrenadeSaveDirectory directory;SaveRepository repository{directory.path()};ASSERT_TRUE(repository.save(profile,content.contentVersion()).succeeded);
    const auto loaded=repository.load(content);ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(profile));
    ASSERT_NE(loaded.profile->assets.find(id),nullptr);EXPECT_EQ(loaded.profile->assets.find(id)->quantity,1U);EXPECT_EQ(loaded.profile->assets.find(id)->location,location);
    EXPECT_EQ(nlohmann::json::parse(readBytes(repository.primaryPath())).at("schema_version"),57U);
}

TEST(GrenadeSaveTest, UnfinishedRaidReopenRestoresPreRaidQuantityWithoutSavingTransientGrenade)
{
    const auto &content=publishedContentRegistry();GrenadeSaveDirectory directory;
    auto profile=makeNewPublishedProfile("grenade-unfinished",content);profile.tutorial=TutorialProgress::Complete;
    const auto id=addGrenade(profile);ASSERT_NE(id,0U);const auto location=profile.assets.find(id)->location;
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile,content.contentVersion()).succeeded);
    {
        GameSession session;session.configurePersistence(directory.path());ASSERT_TRUE(session.continueProfile());
        ASSERT_TRUE(session.deployAlpha(9154U,MapDefinitionId{"map.v0.test"}))<<session.persistenceMessage();
        const auto p=session.world().player().position();
        const auto receipt=session.throwThrowable(id,{p.x+250,p.y+16},{session.profile().revision,"throw-transient"});
        ASSERT_TRUE(receipt.succeeded)<<receipt.message;EXPECT_EQ(session.profile().assets.find(id)->quantity,1U);
        EXPECT_TRUE(session.world().grenade().active());
    }
    GameSession reopened;reopened.configurePersistence(directory.path());ASSERT_TRUE(reopened.continueProfile());
    ASSERT_NE(reopened.profile().assets.find(id),nullptr);EXPECT_EQ(reopened.profile().assets.find(id)->quantity,2U);
    EXPECT_EQ(reopened.profile().assets.find(id)->location,location);EXPECT_FALSE(reopened.profile().pendingRaid);
}
