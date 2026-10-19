#include "chest_rig_layout_migration.h"

#include <stdexcept>

namespace {
std::vector<GridPosition> offsets(const ItemDefinitionId &id) {
    if (id.value() == "item.container.chest_rig_small")
        return {{0,0},{1,0},{2,0},{2,1}};
    if (id.value() == "item.container.chest_rig_patrol")
        return {{0,0},{1,0},{2,0},{3,0},{0,2},{2,2}};
    if (id.value() == "item.container.chest_rig_assault")
        return {{0,0},{1,0},{2,0},{3,0},{0,2},{2,2},{4,0},{4,2}};
    return {};
}
void transform(AssetRegistry &assets, const ContentRegistry &content, bool toLegacy) {
    for (const auto &[id, record] : assets.records()) {
        const auto *stored = std::get_if<StoredAssetLocation>(&record.location);
        if (!stored || stored->container.kind != ProfileContainerKind::AssetCompartment) continue;
        const auto *owner = assets.find(stored->container.ownerAssetId);
        if (!owner) throw std::invalid_argument("chest rig owner is missing");
        const auto positions = offsets(owner->definitionId);
        if (positions.empty()) continue;
        const auto &definition = content.item(owner->definitionId);
        auto next = *stored;
        if (!toLegacy) {
            if (stored->container.compartmentIndex >= positions.size())
                throw std::invalid_argument("legacy chest rig pocket is invalid");
            const auto delta = positions[stored->container.compartmentIndex];
            next.origin = {stored->origin.x + delta.x, stored->origin.y + delta.y};
            next.container.compartmentIndex = 0;
        } else {
            const auto pockets = legacyChestRigCompartments(definition);
            const auto shape = inventoryFootprint(content.item(record.definitionId), record.orientation);
            bool found{};
            for (std::size_t i=0; i<positions.size(); ++i) {
                const auto o=positions[i]; const auto &p=pockets[i];
                if (stored->origin.x>=o.x && stored->origin.y>=o.y &&
                    stored->origin.x+shape.width<=o.x+p.width &&
                    stored->origin.y+shape.height<=o.y+p.height &&
                    (p.pocketKind==ContainerPocketKind::General ||
                     content.item(record.definitionId).category==ItemCategory::Magazine)) {
                    next.container.compartmentIndex=static_cast<std::uint32_t>(i);
                    next.origin={stored->origin.x-o.x,stored->origin.y-o.y};found=true;break;
                }
            }
            if (!found) throw std::invalid_argument("unified chest rig requires schema 56");
        }
        assets.findMutable(id)->location=next;
    }
}
}

std::vector<ContainerCompartmentDefinition> legacyChestRigCompartments(const ItemDefinition &d) {
    using P=ContainerPocketKind;
    if (d.definitionId.value()=="item.container.chest_rig_small")
        return {{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{1,1,P::General},{1,1,P::General}};
    if (d.definitionId.value()=="item.container.chest_rig_patrol")
        return {{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{2,1,P::General},{2,1,P::General}};
    if (d.definitionId.value()=="item.container.chest_rig_assault")
        return {{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{1,2,P::MagazineOnly},{2,2,P::General},{2,2,P::General},{1,2,P::General},{1,2,P::General}};
    return {};
}
ContainerCompartmentDefinition profileCompartmentDefinition(
    const ProfileState &p,const ItemDefinition &d,std::uint32_t index) {
    if (p.chestRigLayoutVersion==0) {
        const auto old=legacyChestRigCompartments(d);
        if (!old.empty()) return old.at(index);
    }
    return d.containerCompartments.at(index);
}
ProfileValidationResult migrateLegacyChestRigLayout(ProfileState &profile,const ContentRegistry &content) {
    if (profile.chestRigLayoutVersion==1) return {true,{}};
    if (profile.chestRigLayoutVersion!=0) return {false,"unsupported chest rig layout version"};
    const auto old=validateProfileState(profile,content);
    if (!old.valid) return old;
    ProfileState candidate=profile;
    try {
        transform(candidate.assets,content,false);
        if (candidate.pendingRaid && candidate.pendingRaid->startingAssets)
            transform(*candidate.pendingRaid->startingAssets,content,false);
        candidate.chestRigLayoutVersion=1;
        const auto valid=validateProfileState(candidate,content);
        if (!valid.valid) return valid;
    } catch (const std::exception &e) { return {false,e.what()}; }
    profile=std::move(candidate);
    return {true,{}};
}
ProfileState legacyChestRigSnapshot(const ProfileState &profile) {
    ProfileState copy=profile;
    if (copy.chestRigLayoutVersion==1) {
        transform(copy.assets,publishedContentRegistry(),true);
        if (copy.pendingRaid && copy.pendingRaid->startingAssets)
            transform(*copy.pendingRaid->startingAssets,publishedContentRegistry(),true);
        copy.chestRigLayoutVersion=0;
    }
    return copy;
}
