#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "loot_table.h"

namespace
{
    class SequenceLootRandomSource final : public LootRandomSource
    {
    public:
        explicit SequenceLootRandomSource(
            std::vector<std::uint32_t> values)
            : values_{std::move(values)}
        {
        }

        std::uint32_t next(
            std::uint32_t upperExclusive) override
        {
            bounds_.push_back(upperExclusive);

            if (position_ >= values_.size())
            {
                throw std::runtime_error{
                    "Sequence loot random source is exhausted"};
            }

            return values_[position_++];
        }

        const std::vector<std::uint32_t> &bounds() const noexcept
        {
            return bounds_;
        }

    private:
        std::vector<std::uint32_t> values_;
        std::vector<std::uint32_t> bounds_;
        std::size_t position_{};
    };
}

TEST(LootTableTest, WeightedSelectionUsesStableHalfOpenBoundaries)
{
    const LootTable table{
        {
            {ItemId::Cola, 2, 1, 1},
            {ItemId::Medkit, 3, 1, 1},
        },
        4};
    SequenceLootRandomSource random{{0, 1, 2, 4}};

    const std::vector<LootStack> result =
        table.roll(random);

    EXPECT_EQ(
        result,
        (std::vector<LootStack>{
            {ItemId::Cola, 1},
            {ItemId::Cola, 1},
            {ItemId::Medkit, 1},
            {ItemId::Medkit, 1},
        }));
    EXPECT_EQ(
        random.bounds(),
        (std::vector<std::uint32_t>{5, 5, 5, 5}));
}

TEST(LootTableTest, QuantityDrawUsesInclusiveBoundsAndNormalizesStacks)
{
    const LootTable table{
        {
            {ItemId::Ammo9mm, 1, 10, 30},
        },
        3};
    SequenceLootRandomSource random{
        {0, 0, 0, 20, 0, 20}};

    const std::vector<LootStack> result =
        table.roll(random);

    EXPECT_EQ(
        result,
        (std::vector<LootStack>{
            {ItemId::Ammo9mm, 60},
            {ItemId::Ammo9mm, 10},
        }));
    EXPECT_EQ(
        random.bounds(),
        (std::vector<std::uint32_t>{
            1, 21,
            1, 21,
            1, 21,
        }));
}

TEST(LootTableTest, RejectsInvalidTables)
{
    EXPECT_THROW(
        (LootTable{{}, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Cola, 1, 1, 1}}, 0}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Cola, 0, 1, 1}}, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Ammo9mm, 1, 0, 1}}, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Ammo9mm, 1, 20, 10}}, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Ammo9mm, 1, 1, 61}}, 1}),
        std::invalid_argument);
    EXPECT_THROW(
        (LootTable{{{ItemId::Count, 1, 1, 1}}, 1}),
        std::invalid_argument);
}

TEST(LootTableTest, RejectsWeightOverflow)
{
    EXPECT_THROW(
        (LootTable{
            {
                {
                    ItemId::Cola,
                    std::numeric_limits<std::uint32_t>::max(),
                    1,
                    1},
                {ItemId::Medkit, 1, 1, 1},
            },
            1}),
        std::invalid_argument);
}

TEST(LootTableTest, RejectsOutOfRangeRandomValue)
{
    const LootTable table{
        {{ItemId::Cola, 5, 1, 1}},
        1};
    SequenceLootRandomSource random{{5}};

    EXPECT_THROW(
        static_cast<void>(
            table.roll(random)),
        std::out_of_range);
}

TEST(LootTableTest, SameSeedProducesSameInRangeSequence)
{
    SeededLootRandomSource first{0xC0FFEEu};
    SeededLootRandomSource second{0xC0FFEEu};

    for (int index = 0; index < 20; ++index)
    {
        const std::uint32_t firstValue = first.next(7);
        const std::uint32_t secondValue = second.next(7);

        EXPECT_LT(firstValue, 7U);
        EXPECT_EQ(firstValue, secondValue);
    }

    EXPECT_THROW(
        static_cast<void>(
            first.next(0)),
        std::invalid_argument);
}

TEST(LootTableTest, DefaultCabinetTableHasFrozenWeek20Contract)
{
    const LootTable &table =
        defaultStorageCabinetLootTable();

    EXPECT_EQ(table.rollCount(), 3U);
    EXPECT_EQ(table.totalWeight(), 100U);
    ASSERT_EQ(table.entries().size(), 5U);
    EXPECT_EQ(
        table.entries().back(),
        (LootTableEntry{
            ItemId::Ammo9mm,
            32,
            10,
            30}));
}
