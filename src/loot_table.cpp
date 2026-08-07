#include "loot_table.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    void appendLootQuantity(
        std::vector<LootStack> &stacks,
        ItemId definitionId,
        std::uint32_t quantity)
    {
        const std::uint32_t maximumStackSize =
            itemDefinition(definitionId).maxStackSize;

        for (LootStack &stack : stacks)
        {
            if (stack.definitionId != definitionId ||
                stack.quantity >= maximumStackSize)
            {
                continue;
            }

            const std::uint32_t available =
                maximumStackSize - stack.quantity;
            const std::uint32_t transferred =
                quantity < available ? quantity : available;

            stack.quantity += transferred;
            quantity -= transferred;

            if (quantity == 0)
            {
                return;
            }
        }

        while (quantity > 0)
        {
            const std::uint32_t stackQuantity =
                quantity < maximumStackSize
                    ? quantity
                    : maximumStackSize;

            stacks.push_back(
                LootStack{
                    definitionId,
                    stackQuantity});

            quantity -= stackQuantity;
        }
    }

    std::uint32_t checkedDraw(
        LootRandomSource &random,
        std::uint32_t upperExclusive)
    {
        const std::uint32_t result =
            random.next(upperExclusive);

        if (result >= upperExclusive)
        {
            throw std::out_of_range{
                "Loot random source returned an out-of-range value"};
        }

        return result;
    }
}

SeededLootRandomSource::SeededLootRandomSource()
    : SeededLootRandomSource{
          std::random_device{}()}
{
}

SeededLootRandomSource::SeededLootRandomSource(
    std::uint32_t seed)
    : engine_{seed}
{
}

std::uint32_t SeededLootRandomSource::next(
    std::uint32_t upperExclusive)
{
    if (upperExclusive == 0)
    {
        throw std::invalid_argument{
            "Loot random upper bound must be positive"};
    }

    std::uniform_int_distribution<std::uint32_t> distribution{
        0,
        upperExclusive - 1};

    return distribution(engine_);
}

LootTable::LootTable(
    std::vector<LootTableEntry> entries,
    std::uint32_t rollCount)
    : entries_{std::move(entries)},
      rollCount_{rollCount}
{
    if (entries_.empty() || rollCount_ == 0)
    {
        throw std::invalid_argument{
            "Loot table requires entries and at least one roll"};
    }

    std::uint64_t totalWeight{};

    for (const LootTableEntry &entry : entries_)
    {
        const ItemDefinition *definition{};

        try
        {
            definition = &itemDefinition(entry.definitionId);
        }
        catch (const std::out_of_range &)
        {
            throw std::invalid_argument{
                "Loot table contains an invalid item definition"};
        }

        if (!definition->visualAssetsPublished ||
            entry.weight == 0 ||
            entry.minimumQuantity == 0 ||
            entry.minimumQuantity > entry.maximumQuantity ||
            entry.maximumQuantity > definition->maxStackSize)
        {
            throw std::invalid_argument{
                "Loot table entry is not valid for its item definition"};
        }

        totalWeight += entry.weight;

        if (totalWeight >
            std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument{
                "Loot table total weight exceeds the random source range"};
        }
    }

    totalWeight_ = static_cast<std::uint32_t>(
        totalWeight);
}

std::vector<LootStack> LootTable::roll(
    LootRandomSource &random) const
{
    std::vector<LootStack> result;
    result.reserve(rollCount_);

    for (std::uint32_t rollIndex = 0;
         rollIndex < rollCount_;
         ++rollIndex)
    {
        const std::uint32_t ticket =
            checkedDraw(random, totalWeight_);

        std::uint32_t cumulativeWeight{};
        const LootTableEntry *selected{};

        for (const LootTableEntry &entry : entries_)
        {
            cumulativeWeight += entry.weight;

            if (ticket < cumulativeWeight)
            {
                selected = &entry;
                break;
            }
        }

        if (selected == nullptr)
        {
            throw std::logic_error{
                "Validated loot table did not select an entry"};
        }

        std::uint32_t quantity =
            selected->minimumQuantity;

        if (selected->minimumQuantity !=
            selected->maximumQuantity)
        {
            const std::uint32_t quantityRange =
                selected->maximumQuantity -
                selected->minimumQuantity + 1;

            quantity += checkedDraw(
                random,
                quantityRange);
        }

        appendLootQuantity(
            result,
            selected->definitionId,
            quantity);
    }

    return result;
}

std::uint32_t LootTable::rollCount() const noexcept
{
    return rollCount_;
}

std::uint32_t LootTable::totalWeight() const noexcept
{
    return totalWeight_;
}

const std::vector<LootTableEntry> &
LootTable::entries() const noexcept
{
    return entries_;
}

const LootTable &defaultStorageCabinetLootTable()
{
    static const LootTable table{
        {
            {ItemId::Cola, 24, 1, 1},
            {ItemId::Medkit, 20, 1, 1},
            {ItemId::Pistol, 16, 1, 1},
            {ItemId::Rifle, 8, 1, 1},
            {ItemId::Ammo9mm, 32, 10, 30},
        },
        3};

    return table;
}
