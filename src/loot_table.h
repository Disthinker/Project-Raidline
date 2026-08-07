#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "item_definition.h"

// Loot 逻辑只依赖这个窄接口。测试可以提供确定序列，
// 正常游戏使用 SeededLootRandomSource。
class LootRandomSource
{
public:
    virtual ~LootRandomSource() = default;

    [[nodiscard]]
    virtual std::uint32_t next(
        std::uint32_t upperExclusive) = 0;
};

class SeededLootRandomSource final : public LootRandomSource
{
public:
    SeededLootRandomSource();

    explicit SeededLootRandomSource(
        std::uint32_t seed);

    [[nodiscard]]
    std::uint32_t next(
        std::uint32_t upperExclusive) override;

private:
    std::mt19937 engine_;
};

struct LootTableEntry
{
    ItemId definitionId{};
    std::uint32_t weight{};
    std::uint32_t minimumQuantity{1};
    std::uint32_t maximumQuantity{1};

    friend bool operator==(
        const LootTableEntry &,
        const LootTableEntry &) = default;
};

// 抽取结果尚未拥有 ItemInstanceId；GameplayWorld 只为最终 placement
// 分配稳定 ID。
struct LootStack
{
    ItemId definitionId{};
    std::uint32_t quantity{};

    friend bool operator==(
        const LootStack &,
        const LootStack &) = default;
};

class LootTable
{
public:
    LootTable(
        std::vector<LootTableEntry> entries,
        std::uint32_t rollCount);

    [[nodiscard]]
    std::vector<LootStack> roll(
        LootRandomSource &random) const;

    [[nodiscard]]
    std::uint32_t rollCount() const noexcept;

    [[nodiscard]]
    std::uint32_t totalWeight() const noexcept;

    [[nodiscard]]
    const std::vector<LootTableEntry> &entries() const noexcept;

private:
    std::vector<LootTableEntry> entries_;
    std::uint32_t rollCount_{};
    std::uint32_t totalWeight_{};
};

[[nodiscard]]
const LootTable &defaultStorageCabinetLootTable();
