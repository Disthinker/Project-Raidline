#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "profile_state.h"

enum class SaveLoadStatus
{
    NotFound,
    LoadedPrimary,
    RecoveredBackup,
    Failed
};

struct SaveWriteResult
{
    bool succeeded{};
    std::string message;
};

struct SaveLoadResult
{
    SaveLoadStatus status{SaveLoadStatus::NotFound};
    std::optional<ProfileState> profile;
    std::string message;
};

class SaveRepository
{
public:
    explicit SaveRepository(std::filesystem::path directory);

    [[nodiscard]] bool primaryExists() const;

    [[nodiscard]] SaveWriteResult save(
        const ProfileState &profile,
        std::string_view contentVersion) const;

    [[nodiscard]] SaveLoadResult load(
        const ContentRegistry &content) const;

    [[nodiscard]] const std::filesystem::path &primaryPath() const noexcept;
    [[nodiscard]] const std::filesystem::path &backupPath() const noexcept;

private:
    std::filesystem::path directory_;
    std::filesystem::path primaryPath_;
    std::filesystem::path backupPath_;
    std::filesystem::path temporaryPath_;
    std::filesystem::path backupTemporaryPath_;
};

[[nodiscard]] std::string serializeProfileEnvelope(
    const ProfileState &profile,
    std::string_view contentVersion,
    std::uint32_t schemaVersion = 3);

[[nodiscard]] SaveLoadResult deserializeProfileEnvelope(
    std::string_view text,
    const ContentRegistry &content);
