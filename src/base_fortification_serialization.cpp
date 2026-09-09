#include "base_fortification_serialization.h"
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
std::uint64_t unsignedValue(const nlohmann::json &value)
{
    if (!value.is_number_unsigned() &&
        !(value.is_number_integer() && value.get<std::int64_t>() >= 0))
        throw std::invalid_argument("Fortification integer must be nonnegative and integral");
    return value.get<std::uint64_t>();
}
std::uint32_t boundedValue(const nlohmann::json &value)
{
    const auto result = unsignedValue(value);
    if (result > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Fortification integer overflow");
    return static_cast<std::uint32_t>(result);
}
} // namespace

nlohmann::json baseFortificationsJson(const BaseFortificationState &state)
{
    auto records = nlohmann::json::array();
    for (const auto &[id, record] : state.instances)
    {
        nlohmann::json slot = nullptr;
        if (record.slot)
            slot = {{"site", record.slot->site.value()},
                    {"plot", record.slot->plot},
                    {"side", static_cast<std::uint32_t>(record.slot->side)}};
        records.push_back({{"id", id.value},
                           {"definition", record.definition.value()},
                           {"durability", record.durability},
                           {"slot", std::move(slot)}});
    }
    return {{"next_instance_id", state.nextInstanceId}, {"instances", std::move(records)}};
}

BaseFortificationState parseBaseFortificationsJson(const nlohmann::json &value)
{
    BaseFortificationState result;
    result.nextInstanceId = unsignedValue(value.at("next_instance_id"));
    const auto &records = value.at("instances");
    if (!records.is_array() || records.size() > 64)
        throw std::invalid_argument("Fortification reserve is not a bounded array");
    for (const auto &item : records)
    {
        const FortificationInstanceId id{unsignedValue(item.at("id"))};
        FortificationRecord record{
            FortificationDefinitionId{item.at("definition").get<std::string>()},
            boundedValue(item.at("durability")),
            {}};
        const auto &slot = item.at("slot");
        if (!slot.is_null())
        {
            const auto side = boundedValue(slot.at("side"));
            if (side > 3)
                throw std::invalid_argument("Unknown defense side");
            record.slot =
                DefenseSlotKey{RegionalBaseSiteDefinitionId{slot.at("site").get<std::string>()},
                               slot.at("plot").get<std::string>(), static_cast<DefenseSide>(side)};
        }
        if (id.value == 0 || id.value >= result.nextInstanceId ||
            !result.instances.emplace(id, std::move(record)).second)
            throw std::invalid_argument("Duplicate or invalid fortification instance");
    }
    if (result.nextInstanceId == 0)
        throw std::invalid_argument("Fortification high water is zero");
    return result;
}
