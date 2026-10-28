#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace raidline::test {

// Test-only ownership: a name is never owned until create_directory succeeds.
// Declare this before App/SaveRepository members and release those consumers
// before explicit cleanup so both normal and failed SetUp paths are safe.
class TemporaryDirectory {
public:
    TemporaryDirectory() = default;
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    TemporaryDirectory(TemporaryDirectory &&) = delete;
    TemporaryDirectory &operator=(TemporaryDirectory &&) = delete;

    ~TemporaryDirectory() noexcept {
        try { cleanup(); } catch (...) { /* Explicit fixture cleanup reports errors. */ }
    }

    void claim(std::string_view prefix, std::string_view testName) {
        std::string name;
        for (const char c : testName) name += safeCharacter(c) ? c : '_';
        if (name.empty()) name = "test";
        name += '-' + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        claimWithSeed(std::filesystem::temp_directory_path(), prefix, name);
    }

    // Deterministic seed/root injection is only for this helper's collision and
    // I/O regressions. A custom root must still resolve within system TEMP.
    void claimWithSeed(const std::filesystem::path &root, std::string_view prefix,
                       std::string_view seed, unsigned attempts = 128) {
        if (owns_) throw std::logic_error{"temporary directory already owned"};
        if (!prefix.starts_with("raidline-") || !safeName(prefix) || !safeName(seed))
            throw std::invalid_argument{"unsafe temporary directory prefix or seed"};
        if (attempts == 0 || attempts > 128)
            throw std::invalid_argument{"invalid temporary directory attempt limit"};

        const auto tempRoot = std::filesystem::canonical(std::filesystem::temp_directory_path());
        const auto resolvedRoot = std::filesystem::canonical(root);
        if (!within(resolvedRoot, tempRoot))
            throw std::invalid_argument{"test directory root is outside system TEMP"};
        if (!std::filesystem::is_directory(resolvedRoot))
            throw std::filesystem::filesystem_error{
                "test directory root is not a directory", resolvedRoot,
                std::make_error_code(std::errc::not_a_directory)};

        const auto stem = std::string{prefix} + std::string{seed} + '-';
        for (unsigned attempt = 0; attempt < attempts; ++attempt) {
            const auto candidate = resolvedRoot / (stem + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                root_ = resolvedRoot;
                prefix_ = prefix;
                path_ = candidate;
                owns_ = true;
                return;
            }
            // Existing directories normally return false without error; an
            // existing file may report file_exists, including on Windows.
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error{
                    "could not claim test directory", candidate, error};
        }
        throw std::runtime_error{"test directory claim attempts exhausted"};
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

    void cleanup() {
        if (!owns_) return;
        // Resolve the captured root again and reject replacements that redirect
        // cleanup. remove_all never follows nested directory symlinks.
        if (std::filesystem::canonical(root_) != root_ ||
            path_.parent_path() != root_ || !path_.filename().string().starts_with(prefix_))
            throw std::runtime_error{"refusing test directory cleanup outside owned root"};
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path_, error);
        if (status.type() == std::filesystem::file_type::not_found &&
            (!error || error == std::errc::no_such_file_or_directory)) {
            release();
            return;
        }
        if (error)
            throw std::filesystem::filesystem_error{"could not inspect owned test directory", path_, error};
        if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status) ||
            std::filesystem::canonical(path_) != path_)
            throw std::runtime_error{"refusing cleanup of replaced test directory"};
        std::filesystem::remove_all(path_, error);
        if (error)
            throw std::filesystem::filesystem_error{"could not remove owned test directory", path_, error};
        release();
    }

private:
    static bool safeCharacter(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_';
    }
    static bool safeName(std::string_view name) noexcept {
        if (name.empty()) return false;
        for (const char c : name) if (!safeCharacter(c)) return false;
        return true;
    }
    static bool within(const std::filesystem::path &path, const std::filesystem::path &root) {
        auto p = path.begin();
        for (auto r = root.begin(); r != root.end(); ++r, ++p)
            if (p == path.end() || *p != *r) return false;
        return true;
    }
    void release() noexcept { owns_ = false; path_.clear(); }

    std::filesystem::path root_, path_;
    std::string prefix_;
    bool owns_{};
};

} // namespace raidline::test
