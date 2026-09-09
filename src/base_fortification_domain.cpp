#include "base_fortification_domain.h"
#include "home_founding_types.h"
#include <limits>
#include <set>
#include <type_traits>

namespace
{
constexpr std::size_t maximumInstances = 64;
FortificationInstanceId commandInstance(const FortificationCommand &command)
{
    return std::visit(
        [](const auto &value) -> FortificationInstanceId {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, BuildFortificationCommand>)
                return {};
            else
                return value.instance;
        },
        command);
}
} // namespace

ProfileValidationResult validateBaseFortifications(const ProfileState &profile,
                                                   const ContentRegistry &content)
{
    const auto &state = profile.baseFortifications;
    if (state.nextInstanceId == 0 || state.instances.size() > maximumInstances ||
        (!profile.homeFounding.established && !state.instances.empty()))
        return {false, "Invalid fortification high water or capacity"};
    std::set<DefenseSlotKey> occupied;
    for (const auto &[id, record] : state.instances)
    {
        const auto *definition = content.findFortification(record.definition);
        if (id.value == 0 || id.value >= state.nextInstanceId || !definition ||
            record.durability > definition->maximumDurability)
            return {false, "Invalid fortification identity, definition or durability"};
        if (record.slot)
        {
            const auto &slot = *record.slot;
            const auto &activeSite = profile.regionalOperations.technologyCore.baseSiteDefinitionId;
            const auto plot = profile.homeFounding.plots.find(activeSite);
            const std::string expectedPlot =
                plot == profile.homeFounding.plots.end() ? "" : plot->second;
            if (!slot.site.valid() || slot.site != activeSite || slot.plot != expectedPlot ||
                static_cast<std::uint32_t>(slot.side) > 3 || !occupied.insert(slot).second)
                return {false, "Invalid or duplicate fortification slot"};
        }
    }
    return {true, {}};
}

FortificationPlan queryFortificationCommand(const ProfileState &profile,
                                            const ContentRegistry &content,
                                            const FortificationCommand &command)
{
    FortificationPlan plan;
    plan.revision = profile.revision;
    plan.instance = commandInstance(command);
    auto fail = [&](DomainErrorCode error, const char *message) {
        plan.error = error;
        plan.message = message;
        return plan;
    };
    if (!profile.homeFounding.established || profile.pendingRaid || profile.activeBaseDefense)
        return fail(DomainErrorCode::IllegalDestination,
                    "Fortifications require an idle established Base");
    if (profile.baseSiege.warningActive &&
        !std::holds_alternative<RepairFortificationCommand>(command))
        return fail(DomainErrorCode::IllegalDestination,
                    "Defense warning freezes fortification layout and construction");
    if (!validateBaseFortifications(profile, content).valid)
        return fail(DomainErrorCode::InvalidProfile, "Invalid fortification state");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return fail(DomainErrorCode::RevisionOverflow, "Profile revision cannot advance");
    if (const auto *build = std::get_if<BuildFortificationCommand>(&command))
    {
        const auto *definition = content.findFortification(build->definition);
        if (!definition)
            return fail(DomainErrorCode::IllegalDestination, "Unknown fortification definition");
        const auto &state = profile.baseFortifications;
        if (state.instances.size() >= maximumInstances ||
            state.nextInstanceId == std::numeric_limits<std::uint64_t>::max())
            return fail(DomainErrorCode::Capacity, "Fortification reserve capacity reached");
        plan.instance = {state.nextInstanceId};
        plan.materialCost = definition->buildMaterialUnits;
        plan.durabilityAfter = definition->maximumDurability;
    }
    else
    {
        const auto found = profile.baseFortifications.instances.find(plan.instance);
        if (found == profile.baseFortifications.instances.end())
            return fail(DomainErrorCode::MissingAsset, "Fortification instance no longer exists");
        const auto &record = found->second;
        const auto &definition = *content.findFortification(record.definition);
        plan.durabilityAfter = record.durability;
        if (std::holds_alternative<RepairFortificationCommand>(command))
        {
            const auto missing = definition.maximumDurability - record.durability;
            if (missing == 0)
                return fail(DomainErrorCode::InvalidQuantity, "Fortification does not need repair");
            plan.materialCost = 1 + (missing - 1) / definition.repairPerMaterialUnit;
            plan.durabilityAfter = definition.maximumDurability;
        }
        else if (!record.slot)
            return fail(DomainErrorCode::IllegalDestination, "Fortification is already in reserve");
    }
    if (profile.baseConstruction.materialUnits < plan.materialCost)
        return fail(DomainErrorCode::Capacity, "Not enough public construction materials");
    plan.canCommit = true;
    return plan;
}

FortificationReceipt executeFortificationCommand(ProfileState &profile,
                                                 const ContentRegistry &content,
                                                 const FortificationCommand &command,
                                                 const CommandContext &context)
{
    FortificationReceipt receipt;
    receipt.revision = profile.revision;
    receipt.instance = commandInstance(command);
    auto fail = [&](DomainErrorCode error, std::string message) {
        receipt.error = error;
        receipt.message = std::move(message);
        return receipt;
    };
    if (context.transactionId.empty())
        return fail(DomainErrorCode::InvalidTransaction, "Transaction ID is required");
    if (profile.committedTransactions.contains(context.transactionId))
    {
        // Existing transaction ledger stores acceptance, not historical receipts.
        // Do not invent an instance ID or claim a second material spend on retry.
        receipt.succeeded = receipt.alreadyCommitted = true;
        receipt.instance = {};
        return receipt;
    }
    if (context.expectedRevision != profile.revision)
        return fail(DomainErrorCode::StaleRevision, "Refresh the fortification preview");
    const auto plan = queryFortificationCommand(profile, content, command);
    if (!plan.canCommit)
        return fail(plan.error, plan.message);
    ProfileState candidate = profile;
    candidate.baseConstruction.materialUnits -= plan.materialCost;
    auto &state = candidate.baseFortifications;
    if (const auto *build = std::get_if<BuildFortificationCommand>(&command))
    {
        state.instances.emplace(plan.instance,
                                FortificationRecord{build->definition, plan.durabilityAfter, {}});
        ++state.nextInstanceId;
    }
    else
    {
        auto &record = state.instances.at(plan.instance);
        record.durability = plan.durabilityAfter;
        if (std::holds_alternative<StoreFortificationCommand>(command))
            record.slot.reset();
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return fail(DomainErrorCode::InvalidProfile, validation.message);
    profile = std::move(candidate);
    receipt.succeeded = true;
    receipt.revision = profile.revision;
    receipt.instance = plan.instance;
    receipt.materialSpent = plan.materialCost;
    return receipt;
}

void storeAllBaseFortifications(BaseFortificationState &state) noexcept
{
    for (auto &[id, record] : state.instances)
        record.slot.reset();
}

std::uint64_t baseFortificationFingerprint(const BaseFortificationState &state) noexcept
{
    std::uint64_t h = 14695981039346656037ULL;
    const auto number = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i)
        {
            h ^= value & 255U;
            h *= 1099511628211ULL;
            value >>= 8U;
        }
    };
    const auto text = [&](std::string_view value) {
        number(value.size());
        for (unsigned char c : value)
        {
            h ^= c;
            h *= 1099511628211ULL;
        }
    };
    number(state.nextInstanceId);
    number(state.instances.size());
    for (const auto &[id, record] : state.instances)
    {
        number(id.value);
        text(record.definition.value());
        number(record.durability);
        number(record.slot.has_value());
        if (record.slot)
        {
            text(record.slot->site.value());
            text(record.slot->plot);
            number(static_cast<std::uint32_t>(record.slot->side));
        }
    }
    return h;
}
