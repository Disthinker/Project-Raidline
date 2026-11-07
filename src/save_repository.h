#pragma once

#include <array>
#include <cstddef>
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

struct SaveWriteMetrics
{
    double validationMilliseconds{};
    double serializationMilliseconds{};
    double commitMilliseconds{};
    double totalMilliseconds{};
    std::size_t fullEnvelopeValidations{};
    std::size_t cachedEnvelopeProofs{};
};

struct SaveEnvelopeMetadata
{
    std::uint32_t schemaVersion{};
    std::string contentVersion;
};

struct SaveLoadResult
{
    SaveLoadStatus status{SaveLoadStatus::NotFound};
    std::optional<ProfileState> profile;
    std::string message;
    bool migratedChestRigLayout{};
    std::optional<SaveEnvelopeMetadata> sourceEnvelope;
};

class SaveRepository
{
public:
    explicit SaveRepository(std::filesystem::path directory);

    [[nodiscard]] bool primaryExists() const;

    [[nodiscard]] SaveWriteResult save(
        const ProfileState &profile,
        std::string_view contentVersion,
        SaveWriteMetrics *metrics = nullptr) const;

    // Preserve byte-exact, immutable legacy sources before a layout migration.
    [[nodiscard]] SaveWriteResult preserveStashMigrationSources(
        SaveLoadStatus loadedFrom) const;

    [[nodiscard]] SaveWriteResult preserveChestRigMigrationSources(
        SaveLoadStatus loadedFrom) const;

    [[nodiscard]] SaveLoadResult load(
        const ContentRegistry &content) const;

    [[nodiscard]] const std::filesystem::path &primaryPath() const noexcept;
    [[nodiscard]] const std::filesystem::path &backupPath() const noexcept;

private:
    // Instance-local, single-writer cache. Only exact raw-byte equality reuses
    // a proof validated against the immutable published content registry.
    struct VerifiedEnvelope
    {
        std::string bytes;
        std::optional<SaveEnvelopeMetadata> metadata;
    };
    [[nodiscard]] std::optional<SaveEnvelopeMetadata> inspectEnvelope(
        std::string_view bytes, SaveWriteMetrics *metrics = nullptr) const;
    void rememberEnvelope(std::string_view bytes,
        std::optional<SaveEnvelopeMetadata> metadata) const;
    mutable std::array<std::optional<VerifiedEnvelope>, 4> verifiedEnvelopes_;
    mutable std::size_t nextVerifiedEnvelope_{};
    [[nodiscard]] SaveWriteResult preserveLayoutMigrationSources(
        SaveLoadStatus loadedFrom, std::string_view marker) const;
    std::filesystem::path directory_;
    std::filesystem::path primaryPath_;
    std::filesystem::path backupPath_;
    std::filesystem::path temporaryPath_;
    std::filesystem::path backupTemporaryPath_;
};

[[nodiscard]] std::string serializeProfileEnvelope(
    const ProfileState &profile,
    std::string_view contentVersion,
    std::uint32_t schemaVersion = 60);

[[nodiscard]] SaveLoadResult deserializeProfileEnvelope(
    std::string_view text,
    const ContentRegistry &content);
