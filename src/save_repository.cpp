#include "save_repository.h"

#include <fstream>
#include <limits>
#include <sstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
using Json = nlohmann::json;

std::string checksum(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4U;
    }
    return result;
}

std::string orientationName(ItemOrientation orientation)
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
        return "0";
    case ItemOrientation::Degrees90:
        return "90";
    case ItemOrientation::Degrees180:
        return "180";
    case ItemOrientation::Degrees270:
        return "270";
    }
    return "invalid";
}

std::optional<ItemOrientation> parseOrientation(std::string_view value)
{
    if (value == "0") return ItemOrientation::Degrees0;
    if (value == "90") return ItemOrientation::Degrees90;
    if (value == "180") return ItemOrientation::Degrees180;
    if (value == "270") return ItemOrientation::Degrees270;
    return std::nullopt;
}

std::string slotName(EquipmentSlotKind slot)
{
    switch (slot)
    {
    case EquipmentSlotKind::PrimaryWeapon:
        return "primary_weapon";
    case EquipmentSlotKind::ChestRig:
        return "chest_rig";
    case EquipmentSlotKind::Backpack:
        return "backpack";
    }
    return "invalid";
}

std::optional<EquipmentSlotKind> parseSlot(std::string_view value)
{
    if (value == "primary_weapon") return EquipmentSlotKind::PrimaryWeapon;
    if (value == "chest_rig") return EquipmentSlotKind::ChestRig;
    if (value == "backpack") return EquipmentSlotKind::Backpack;
    return std::nullopt;
}

std::string tutorialName(TutorialProgress progress)
{
    switch (progress)
    {
    case TutorialProgress::FindStorage:
        return "find_storage";
    case TutorialProgress::PrepareLoadout:
        return "prepare_loadout";
    case TutorialProgress::FindRaidGate:
        return "find_raid_gate";
    case TutorialProgress::Complete:
        return "complete";
    }
    return "invalid";
}

std::optional<TutorialProgress> parseTutorial(std::string_view value)
{
    if (value == "find_storage") return TutorialProgress::FindStorage;
    if (value == "prepare_loadout") return TutorialProgress::PrepareLoadout;
    if (value == "find_raid_gate") return TutorialProgress::FindRaidGate;
    if (value == "complete") return TutorialProgress::Complete;
    return std::nullopt;
}

Json profilePayload(const ProfileState &profile)
{
    Json assets = Json::array();
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        Json location;
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset.location))
        {
            location = {
                {"type", "stored"},
                {"origin", {{"x", stored->origin.x}, {"y", stored->origin.y}}}};
            if (stored->container.kind == ProfileContainerKind::Stash)
            {
                location["container"] = {{"type", "stash"}};
            }
            else
            {
                location["container"] = {
                    {"type", "asset_compartment"},
                    {"owner_asset_id", stored->container.ownerAssetId},
                    {"compartment", stored->container.compartmentIndex}};
            }
        }
        else
        {
            location = {
                {"type", "equipped"},
                {"slot", slotName(
                    std::get<EquippedAssetLocation>(asset.location).slot)}};
        }

        Json value{
            {"instance_id", asset.instanceId},
            {"definition_id", asset.definitionId.value()},
            {"quantity", asset.quantity},
            {"orientation", orientationName(asset.orientation)},
            {"remaining_charges", asset.remainingCharges},
            {"location", std::move(location)}};
        if (asset.reliefBatchId.has_value())
        {
            value["relief_batch_id"] = *asset.reliefBatchId;
        }
        assets.push_back(std::move(value));
    }

    Json transactions = Json::array();
    for (const std::string &transaction : profile.committedTransactions)
    {
        transactions.push_back(transaction);
    }

    return Json{
        {"profile_id", profile.profileId},
        {"revision", profile.revision},
        {"currency", profile.currency},
        {"tutorial", tutorialName(profile.tutorial)},
        {"next_asset_id", profile.assets.nextAssetId()},
        {"committed_transactions", std::move(transactions)},
        {"assets", std::move(assets)}};
}

std::optional<std::string> readText(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
    {
        return std::nullopt;
    }
    return buffer.str();
}

bool writeText(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    return stream.good();
}

bool atomicReplace(
    const std::filesystem::path &temporary,
    const std::filesystem::path &destination)
{
#ifdef _WIN32
    return MoveFileExW(
               temporary.c_str(),
               destination.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool isValidEnvelope(
    const std::filesystem::path &path,
    const ContentRegistry &content)
{
    const auto text = readText(path);
    return text.has_value() &&
           deserializeProfileEnvelope(*text, content).profile.has_value();
}
}

std::string serializeProfileEnvelope(
    const ProfileState &profile,
    std::string_view contentVersion)
{
    Json payload = profilePayload(profile);
    const std::string payloadText = payload.dump();
    Json envelope{
        {"schema_version", 1},
        {"profile_id", profile.profileId},
        {"revision", profile.revision},
        {"content_version", contentVersion},
        {"payload_checksum", checksum(payloadText)},
        {"payload", std::move(payload)}};
    return envelope.dump(2);
}

SaveLoadResult deserializeProfileEnvelope(
    std::string_view text,
    const ContentRegistry &content)
{
    try
    {
        const Json envelope = Json::parse(text.begin(), text.end());
        if (!envelope.is_object() ||
            envelope.at("schema_version").get<int>() != 1 ||
            !envelope.at("content_version").is_string() ||
            envelope.at("content_version").get<std::string>() !=
                content.contentVersion())
        {
            return {SaveLoadStatus::Failed, std::nullopt, "unsupported save envelope"};
        }
        const Json &payload = envelope.at("payload");
        if (!payload.is_object() ||
            checksum(payload.dump()) !=
                envelope.at("payload_checksum").get<std::string>())
        {
            return {SaveLoadStatus::Failed, std::nullopt, "save checksum mismatch"};
        }

        ProfileState profile;
        profile.profileId = payload.at("profile_id").get<std::string>();
        profile.revision = payload.at("revision").get<ProfileRevision>();
        profile.currency = payload.at("currency").get<std::uint32_t>();
        const auto tutorial = parseTutorial(
            payload.at("tutorial").get<std::string>());
        if (!tutorial.has_value() ||
            envelope.at("profile_id").get<std::string>() != profile.profileId ||
            envelope.at("revision").get<ProfileRevision>() != profile.revision)
        {
            return {SaveLoadStatus::Failed, std::nullopt, "save header does not match payload"};
        }
        profile.tutorial = *tutorial;
        profile.assets.setNextAssetIdForLoad(
            payload.at("next_asset_id").get<AssetInstanceId>());

        for (const Json &transaction : payload.at("committed_transactions"))
        {
            const std::string value = transaction.get<std::string>();
            if (value.empty() || !profile.committedTransactions.insert(value).second)
            {
                return {SaveLoadStatus::Failed, std::nullopt, "transaction history is invalid"};
            }
        }

        for (const Json &value : payload.at("assets"))
        {
            AssetRecord asset;
            asset.instanceId = value.at("instance_id").get<AssetInstanceId>();
            asset.definitionId = ItemDefinitionId{
                value.at("definition_id").get<std::string>()};
            asset.quantity = value.at("quantity").get<std::uint32_t>();
            const auto orientation = parseOrientation(
                value.at("orientation").get<std::string>());
            if (!orientation.has_value())
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset orientation is invalid"};
            }
            asset.orientation = *orientation;
            asset.remainingCharges =
                value.at("remaining_charges").get<std::uint32_t>();
            if (value.contains("relief_batch_id"))
            {
                asset.reliefBatchId =
                    value.at("relief_batch_id").get<std::string>();
                if (asset.reliefBatchId->empty())
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "relief batch ID is empty"};
                }
            }

            const Json &location = value.at("location");
            const std::string locationType =
                location.at("type").get<std::string>();
            if (locationType == "equipped")
            {
                const auto slot = parseSlot(
                    location.at("slot").get<std::string>());
                if (!slot.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "equipment slot is invalid"};
                }
                asset.location = EquippedAssetLocation{*slot};
            }
            else if (locationType == "stored")
            {
                const Json &container = location.at("container");
                ProfileContainerId containerId;
                const std::string containerType =
                    container.at("type").get<std::string>();
                if (containerType == "stash")
                {
                    containerId = ProfileContainerId::stash();
                }
                else if (containerType == "asset_compartment")
                {
                    containerId = ProfileContainerId::compartment(
                        container.at("owner_asset_id").get<AssetInstanceId>(),
                        container.at("compartment").get<std::uint32_t>());
                }
                else
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "container type is invalid"};
                }
                const Json &origin = location.at("origin");
                asset.location = StoredAssetLocation{
                    containerId,
                    GridPosition{
                        origin.at("x").get<int>(),
                        origin.at("y").get<int>()}};
            }
            else
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset location type is invalid"};
            }

            if (!profile.assets.insertLoaded(std::move(asset)))
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset ID is duplicated"};
            }
        }

        const ProfileValidationResult validation =
            validateProfileState(profile, content);
        if (!validation.valid)
        {
            return {SaveLoadStatus::Failed, std::nullopt, validation.message};
        }
        return {SaveLoadStatus::LoadedPrimary, std::move(profile), {}};
    }
    catch (const std::exception &error)
    {
        return {SaveLoadStatus::Failed, std::nullopt, error.what()};
    }
}

SaveRepository::SaveRepository(std::filesystem::path directory)
    : directory_{std::move(directory)},
      primaryPath_{directory_ / "profile.json"},
      backupPath_{directory_ / "profile.backup.json"},
      temporaryPath_{directory_ / "profile.tmp.json"},
      backupTemporaryPath_{directory_ / "profile.backup.tmp.json"}
{
}

bool SaveRepository::primaryExists() const
{
    std::error_code error;
    if (std::filesystem::is_regular_file(primaryPath_, error))
    {
        return true;
    }
    error.clear();
    return std::filesystem::is_regular_file(backupPath_, error);
}

SaveWriteResult SaveRepository::save(
    const ProfileState &profile,
    std::string_view contentVersion) const
{
    const ProfileValidationResult validation =
        validateProfileState(profile, publishedContentRegistry());
    if (!validation.valid)
    {
        return {false, validation.message};
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error)
    {
        return {false, "save directory could not be created"};
    }

    const std::string text = serializeProfileEnvelope(profile, contentVersion);
    if (!writeText(temporaryPath_, text))
    {
        return {false, "temporary save could not be written"};
    }
    const auto readBack = readText(temporaryPath_);
    if (!readBack.has_value() ||
        !deserializeProfileEnvelope(*readBack, publishedContentRegistry())
             .profile.has_value())
    {
        std::filesystem::remove(temporaryPath_, error);
        return {false, "temporary save failed read-back validation"};
    }

    const bool validPrimary =
        isValidEnvelope(primaryPath_, publishedContentRegistry());
    const bool validBackup =
        isValidEnvelope(backupPath_, publishedContentRegistry());
    bool installInitialBackup = false;
    if (validPrimary)
    {
        std::filesystem::copy_file(
            primaryPath_,
            backupTemporaryPath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !isValidEnvelope(
                         backupTemporaryPath_,
                         publishedContentRegistry()))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "safe backup could not be updated"};
        }
        if (!atomicReplace(backupTemporaryPath_, backupPath_))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "safe backup could not be atomically replaced"};
        }
    }
    else if (!validBackup)
    {
        error.clear();
        std::filesystem::copy_file(
            temporaryPath_,
            backupTemporaryPath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !isValidEnvelope(
                         backupTemporaryPath_,
                         publishedContentRegistry()))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "initial safe backup could not be created"};
        }
        installInitialBackup = true;
    }

    if (!atomicReplace(temporaryPath_, primaryPath_))
    {
        std::filesystem::remove(temporaryPath_, error);
        std::filesystem::remove(backupTemporaryPath_, error);
        return {false, "primary save could not be atomically replaced"};
    }
    if (installInitialBackup &&
        !atomicReplace(backupTemporaryPath_, backupPath_))
    {
        std::filesystem::remove(backupTemporaryPath_, error);
        return {true, "primary save committed; recovery backup could not be installed"};
    }
    return {true, {}};
}

SaveLoadResult SaveRepository::load(const ContentRegistry &content) const
{
    const auto primaryText = readText(primaryPath_);
    if (primaryText.has_value())
    {
        SaveLoadResult result = deserializeProfileEnvelope(*primaryText, content);
        if (result.profile.has_value())
        {
            result.status = SaveLoadStatus::LoadedPrimary;
            return result;
        }
    }

    const auto backupText = readText(backupPath_);
    if (backupText.has_value())
    {
        SaveLoadResult result = deserializeProfileEnvelope(*backupText, content);
        if (result.profile.has_value())
        {
            result.status = SaveLoadStatus::RecoveredBackup;
            result.message = "primary save was invalid; recovered safe backup";
            return result;
        }
    }

    if (!primaryText.has_value() && !backupText.has_value())
    {
        return {SaveLoadStatus::NotFound, std::nullopt, {}};
    }
    return {SaveLoadStatus::Failed, std::nullopt, "no valid primary or backup save"};
}

const std::filesystem::path &SaveRepository::primaryPath() const noexcept
{
    return primaryPath_;
}

const std::filesystem::path &SaveRepository::backupPath() const noexcept
{
    return backupPath_;
}
