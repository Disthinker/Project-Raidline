#pragma once

#include <compare>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

template <typename Tag>
class DefinitionId
{
public:
    DefinitionId() = default;

    explicit DefinitionId(std::string value)
        : value_{std::move(value)}
    {
        if (!isValidValue(value_))
        {
            throw std::invalid_argument{
                "DefinitionId must be a lowercase namespaced identifier"};
        }
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return !value_.empty();
    }

    [[nodiscard]]
    std::string_view value() const noexcept
    {
        return value_;
    }

    friend auto operator<=>(
        const DefinitionId &,
        const DefinitionId &) = default;

private:
    static bool isValidValue(
        std::string_view value) noexcept
    {
        if (value.size() < 3 ||
            value.front() == '.' ||
            value.back() == '.' ||
            value.find('.') == std::string_view::npos)
        {
            return false;
        }

        bool previousWasDot{false};
        for (const char character : value)
        {
            if (character == '.')
            {
                if (previousWasDot)
                {
                    return false;
                }

                previousWasDot = true;
                continue;
            }

            previousWasDot = false;
            const bool lowercaseLetter =
                character >= 'a' && character <= 'z';
            const bool digit =
                character >= '0' && character <= '9';

            if (!lowercaseLetter &&
                !digit &&
                character != '_')
            {
                return false;
            }
        }

        return true;
    }

    std::string value_;
};

struct ItemDefinitionTag;
struct LootTableDefinitionTag;
struct EnemyDeploymentDefinitionTag;
struct MapDefinitionTag;
struct BasePriorityDefinitionTag;
struct RescueDefinitionTag;
struct BaseConstructionProjectDefinitionTag;

using ItemDefinitionId = DefinitionId<ItemDefinitionTag>;
using LootTableDefinitionId = DefinitionId<LootTableDefinitionTag>;
using EnemyDeploymentDefinitionId =
    DefinitionId<EnemyDeploymentDefinitionTag>;
using MapDefinitionId = DefinitionId<MapDefinitionTag>;
using BasePriorityDefinitionId = DefinitionId<BasePriorityDefinitionTag>;
using RescueDefinitionId = DefinitionId<RescueDefinitionTag>;
using BaseConstructionProjectDefinitionId =
    DefinitionId<BaseConstructionProjectDefinitionTag>;
