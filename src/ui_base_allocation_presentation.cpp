#include "ui_base_allocation_presentation.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <fmt/format.h>

namespace {
using C = UiAllocationControl;
using T = UiAllocationTab;
std::uint32_t add(std::uint32_t a, std::uint32_t b) {
    return b > std::numeric_limits<std::uint32_t>::max() - a ? std::numeric_limits<std::uint32_t>::max() : a + b;
}
std::optional<UiAllocationAsset> snapshot(const ProfileState &p, const ContentRegistry &c,
    AssetInstanceId id, BaseSupplyCategory category, UiLanguage language) {
    const auto *asset = p.assets.find(id);
    if (!asset || !assetIsBaseAccessible(p, id)) return std::nullopt;
    const auto &definition = c.item(asset->definitionId);
    const auto unit = baseSupplyContribution(definition, category);
    UiAllocationAsset result;
    result.id=id; result.definitionId=asset->definitionId; result.quantity=asset->quantity;
    result.orientation=asset->orientation;
    result.contribution=static_cast<std::uint32_t>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(unit)*asset->quantity, std::numeric_limits<std::uint32_t>::max()));
    result.name=compactUiItemName(definition, language);
    std::set<AssetInstanceId> visited;
    auto current=id;
    while (current && visited.insert(current).second) {
        const auto *owner=p.assets.find(current);
        if (!owner) break;
        result.ownership.emplace_back(current, owner->location);
        if (const auto *stored=std::get_if<StoredAssetLocation>(&owner->location)) {
            if (stored->container.kind==ProfileContainerKind::Stash) {
                result.source=fmt::format("{} {} · {},{}",localizeUiText(language,"STASH"),
                    stored->origin.y/8*3+stored->origin.x/8+1,stored->origin.x%8+1,stored->origin.y%8+1); break;
            }
            if (stored->container.kind==ProfileContainerKind::BaseIntake) {
                result.source=localizeUiText(language,"BASE INTAKE"); break;
            }
            current=stored->container.ownerAssetId;
        } else if (const auto *equipped=std::get_if<EquippedAssetLocation>(&owner->location)) {
            result.source=localizeUiText(language,equipped->slot==EquipmentSlotKind::Backpack?"BACKPACK":
                equipped->slot==EquipmentSlotKind::ChestRig?"CHEST RIG":"EQUIPPED"); break;
        } else if (const auto *mag=std::get_if<InstalledMagazineLocation>(&owner->location)) current=mag->weaponAssetId;
        else if (const auto *component=std::get_if<InstalledWeaponComponentLocation>(&owner->location)) current=component->weaponAssetId;
        else if (const auto *battery=std::get_if<InstalledDeviceBatteryLocation>(&owner->location)) current=battery->deviceAssetId;
        else break;
    }
    if(result.ownership.size()>1)if(const auto *stored=std::get_if<StoredAssetLocation>(&result.ownership.front().second))
        result.source+=fmt::format(" / {}:{},{}",stored->container.compartmentIndex+1,stored->origin.x+1,stored->origin.y+1);
    return result;
}
const UiAllocationRow *selectedSupply(const UiAllocationProjection &p) {
    const auto i=std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return p.supplySelection && r.definitionId==*p.supplySelection;});
    return i==p.rows.end()?nullptr:&*i;
}
}

bool sameUiAllocationAsset(const UiAllocationAsset &a, const UiAllocationAsset &b) {
    return a.id==b.id && a.definitionId==b.definitionId && a.quantity==b.quantity &&
        a.contribution==b.contribution && a.orientation==b.orientation && a.ownership==b.ownership;
}

UiAllocationProjection projectUiAllocation(const ProfileState &p, const ContentRegistry &c,
    UiLanguage language, const UiAllocationState &s) {
    UiAllocationProjection r;
    r.profileId=p.profileId; r.revision=p.revision; r.worldMinute=p.worldClock.elapsedWorldMinutes; r.language=language;
    r.clock=projectWorldClock(p.worldClock); r.demand=populationAdjustedDailyDemand(p.basePopulation);
    r.lastShortfall=p.baseResources.lastShortfall; r.readiness=projectBaseSupplyReadiness(p,c,r.demand);
    r.operations=projectBaseOperations(p.baseResources,c.baseOperations(),r.demand);
    const std::array stock{r.readiness.pool.food,r.readiness.pool.hygiene,r.readiness.pool.morale,r.readiness.pool.security};
    const std::array needs{r.demand.food,r.demand.hygiene,r.demand.morale,r.demand.security};
    for(std::size_t i=0;i<4;++i)r.tiers[i]=projectBaseResourceTier(stock[i],needs[i],c.baseOperations());
    r.morale=p.baseMorale; const auto &event=c.baseCommunityEvent(p.baseCommunityEvent.definitionId);
    r.eventName=event.displayName; r.eventEffect=event.moraleEffect;
    const auto next=(p.baseCommunityEvent.cycleIndex+1)*c.baseMorale().eventCycleDays;
    r.eventDays=next>r.clock.completedDays?next-r.clock.completedDays:0;
    r.productionPercent=baseManufacturingDurationPercent(p.baseMorale.tier,c.baseMorale());
    r.cycle=p.basePriority.cycleIndex; r.focus=p.basePriority.focus;
    r.missedWishes=p.basePriority.missedCycleCount; r.frozenPopulation=p.basePriority.frozenPopulation;
    const auto cycleMinutes=c.basePriorityCycleMinutes();
    const auto elapsed=r.worldMinute>kInitialWorldMinute?r.worldMinute-kInitialWorldMinute:0;
    r.remainingWishMinutes=cycleMinutes-elapsed%cycleMinutes;
    r.wishes=projectBasePriorities(p,c);
    if(s.wish && s.wish->cycleIndex==r.cycle && std::any_of(r.wishes.begin(),r.wishes.end(),[&](const auto &w){return w.definitionId==s.wish->definitionId;}))r.selectedWish=s.wish;
    else if(!r.wishes.empty())r.selectedWish=BaseWishInstanceId{r.cycle,r.wishes.front().definitionId};
    r.wishExpired=s.wish && s.wish!=r.selectedWish;
    r.focusPlan=queryBaseWishFocus(p,r.selectedWish); r.clearFocusPlan=queryBaseWishFocus(p,std::nullopt);
    r.materialStock=p.baseConstruction.materialUnits; r.materialMaximum=c.maximumBaseConstructionMaterials();
    const auto wish=std::find_if(r.wishes.begin(),r.wishes.end(),[&](const auto &w){return r.selectedWish && w.definitionId==r.selectedWish->definitionId;});
    const auto wishCategory=wish==r.wishes.end()?s.category:wish->category;
    r.requiredContribution=wish==r.wishes.end()?0:wish->requiredContribution;
    std::map<AssetInstanceId,UiAllocationAsset> eligible;
    if(wish!=r.wishes.end())for(const auto &a:wish->eligibleAssets) {
        if(auto value=snapshot(p,c,a.assetId,wishCategory,language))eligible.emplace(a.assetId,std::move(*value));
    }
    r.selectionExpired=r.wishExpired && !s.contributions.empty();
    for(const auto &a:s.contributions) {
        const auto found=eligible.find(a.id);
        if(found==eligible.end() || !sameUiAllocationAsset(a,found->second))r.selectionExpired=true;
    }
    if(!r.selectionExpired)for(const auto &a:s.contributions) {
        const auto &live=eligible.at(a.id); r.chosen.push_back(live);
        r.selectedContribution=add(r.selectedContribution,live.contribution);
    }
    if(r.selectedWish) {
        std::vector<AssetInstanceId> ids; for(const auto &a:r.chosen)ids.push_back(a.id);
        r.submission=queryBasePrioritySubmission(p,c,{r.selectedWish->definitionId,ids});
    }
    if(s.material) {
        auto live=snapshot(p,c,s.material->id,s.category,language);
        // Construction identity does not depend on the previously browsed supply category.
        if(live)live->contribution=s.material->contribution;
        r.materialExpired=!live || !sameUiAllocationAsset(*s.material,*live);
        if(!r.materialExpired) {
            r.material=std::move(live);
            r.processing=queryConstructionMaterialContribution(p,c,{r.material->id});
            r.materialOutput=static_cast<std::uint64_t>(c.item(r.material->definitionId).baseConstructionMaterialValue)*r.material->quantity;
        }
    }
    if(s.tab==T::Supply) {
        for(const auto &d:c.items()) {
            const auto unit=baseSupplyContribution(d,s.category);
            if(!unit)continue;
            UiAllocationRow row; row.definitionId=d.definitionId; row.name=compactUiItemName(d,language); row.unit=unit;
            const auto assignment=p.baseSupplyPolicy.assignments.find(d.definitionId);
            if(assignment!=p.baseSupplyPolicy.assignments.end())row.assignment=assignment->second;
            for(const auto &[id,a]:p.assets.records())if(a.definitionId==d.definitionId) {
                if(auto value=snapshot(p,c,id,s.category,language)) { row.quantity+=value->quantity; row.assets.push_back(std::move(*value)); }
            }
            if(row.quantity || row.assignment==s.category)r.rows.push_back(std::move(row));
        }
        if(s.supplySelection && std::any_of(r.rows.begin(),r.rows.end(),[&](const auto &a){return a.definitionId==*s.supplySelection;}))r.supplySelection=s.supplySelection;
        if(const auto *row=selectedSupply(r)) {
            r.currentAssignment=row->assignment;
            r.targetAssignment=row->assignment==s.category?std::nullopt:std::optional{s.category};
            r.assignment=queryBaseSupplyAssignment(p,c,{row->definitionId,r.targetAssignment});
        }
    } else if(s.tab==T::Wishes) {
        for(const auto &[id,a]:eligible) {
            auto found=std::find_if(r.rows.begin(),r.rows.end(),[&](const auto &row){return row.definitionId==a.definitionId;});
            if(found==r.rows.end()) { UiAllocationRow row; row.definitionId=a.definitionId; row.name=a.name; row.unit=baseSupplyContribution(c.item(a.definitionId),wishCategory); r.rows.push_back(std::move(row)); found=std::prev(r.rows.end()); }
            found->quantity+=a.quantity; found->assets.push_back(a);
            if(std::any_of(r.chosen.begin(),r.chosen.end(),[&](const auto &chosen){return chosen.id==id;}))found->selectedQuantity+=a.quantity;
        }
    } else if(s.tab==T::Materials) {
        for(const auto &[id,a]:p.assets.records()) {
            const auto &d=c.item(a.definitionId);
            if(!d.baseConstructionMaterialValue || !d.baseContribution)continue;
            if(auto value=snapshot(p,c,id,s.category,language)) {
                UiAllocationRow row; row.definitionId=d.definitionId; row.instanceId=id; row.name=value->name;
                row.source=value->source; row.quantity=value->quantity; row.unit=d.baseConstructionMaterialValue;
                row.assets.push_back(std::move(*value)); r.rows.push_back(std::move(row));
            }
        }
    }
    return r;
}

std::string uiAllocationViewKey(const UiAllocationState &s) {
    auto key=fmt::format("{}:{}:{}:{}:{}:{}:{}",static_cast<int>(s.tab),static_cast<int>(s.category),s.focusOnly,
        s.supplySelection?s.supplySelection->value():"",s.wish?s.wish->cycleIndex:0,s.wish?s.wish->definitionId.value():"",s.material?s.material->id:0);
    for(const auto &a:s.contributions)key+=fmt::format(":{}:{}",a.id,a.quantity);
    return key;
}
std::string uiAllocationActionKey(const UiAllocationHit &h,const UiAllocationState &s,const UiAllocationProjection &p) {
    auto key=fmt::format("{}:{}:{}:{}:{}:{}:{}:{}:{}",p.profileId,static_cast<int>(h.control),h.index,
        h.definitionId.value(),h.assetId,static_cast<int>(s.tab),static_cast<int>(s.category),s.focusOnly,p.cycle);
    if(h.wish)key+=fmt::format(":{}:{}",h.wish->cycleIndex,h.wish->definitionId.value());
    if(h.control==C::Main || h.control==C::Focus || h.control==C::ClearFocus || h.control==C::Row || h.control==C::Wish) {
        key+=fmt::format(":{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",p.revision,p.worldMinute,
            uiAllocationEnabled(h.control,s,p),p.selectedWish?p.selectedWish->definitionId.value():"",
            p.selectedContribution,p.requiredContribution,p.materialStock,p.materialMaximum,p.materialOutput,
            p.currentAssignment?static_cast<int>(*p.currentAssignment):-1,p.focus?p.focus->cycleIndex:0,p.focus?p.focus->definitionId.value():"");
        if(p.supplySelection)key+=fmt::format(":{}",p.supplySelection->value());
    }
    return key;
}
std::vector<UiAllocationAsset> uiAllocationSubjects(const UiAllocationHit &h,const UiAllocationProjection &p) {
    if(h.control==C::Row) {
        const auto row=std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return r.definitionId==h.definitionId && r.instanceId==h.assetId;});
        return row==p.rows.end()?std::vector<UiAllocationAsset>{}:row->assets;
    }
    if(h.control==C::Main) {
        if(p.material)return {*p.material};
        if(p.supplySelection) {const auto *row=selectedSupply(p); return row?row->assets:std::vector<UiAllocationAsset>{};}
        return p.chosen;
    }
    return {};
}
bool uiAllocationEnabled(C c,const UiAllocationState &s,const UiAllocationProjection &p) {
    if(c==C::Previous)return s.pages[static_cast<int>(s.tab)]>0;
    if(c==C::Next)return s.pages[static_cast<int>(s.tab)]+1<uiAllocationPageCount(p.rows.size(),s.tab);
    if(c==C::Main) {
        if(s.tab==T::Supply)return p.supplySelection && p.assignment.canCommit;
        if(s.tab==T::Wishes)return p.selectedWish && p.submission.canCommit;
        if(s.tab==T::Materials)return p.material && p.processing.canCommit;
        return false;
    }
    if(c==C::Focus)return p.selectedWish && p.focus!=p.selectedWish && p.focusPlan.canCommit;
    if(c==C::ClearFocus)return p.focus && p.clearFocusPlan.canCommit;
    if(c==C::Filter)return p.focus.has_value();
    return true;
}
std::string uiAllocationUnavailable(C c,const UiAllocationState &s,const UiAllocationProjection &p) {
    if(c==C::Main) {
        if(s.tab==T::Supply)return p.supplySelection?p.assignment.message:"SELECT A SUPPLY TYPE";
        if(s.tab==T::Wishes)return p.selectedWish?p.submission.message:"SELECT AN ACTIVE BASE WISH";
        if(s.tab==T::Materials)return p.material?p.processing.message:"SELECT ONE MATERIAL STACK";
    }
    if(c==C::Focus)return p.focusPlan.message;
    if(c==C::Filter)return "No active wish focus";
    return {};
}
std::size_t uiAllocationPageSize(T tab) noexcept {return tab==T::Wishes?5:6;}
std::size_t uiAllocationPageCount(std::size_t n,T tab) noexcept {return std::max<std::size_t>(1,(n+uiAllocationPageSize(tab)-1)/uiAllocationPageSize(tab));}
SDL_FRect uiAllocationControlRect(C c) noexcept {
    if(c>=C::Overview && c<=C::Materials)return {64+292.0F*static_cast<int>(c),130,276,38};
    if(c>=C::Food && c<=C::Security)return {64+140.0F*(static_cast<int>(c)-static_cast<int>(C::Food)),186,128,32};
    switch(c) {
    case C::Close:return {1116,80,100,36};
    case C::Details:return {64,550,280,42};
    case C::Previous:return {80,510,142,30};
    case C::Next:return {454,510,142,30};
    case C::Main:return {852,550,364,42};
    case C::Focus:return {64,550,180,42};
    case C::ClearFocus:return {260,550,180,42};
    case C::Filter:return {456,550,180,42};
    default:return {};
    }
}
SDL_FRect uiAllocationListRect(T tab) noexcept {return tab==T::Wishes?SDL_FRect{64,314,548,226}:SDL_FRect{64,224,548,316};}
SDL_FRect uiAllocationRowRect(T tab,std::size_t i) noexcept {return tab==T::Wishes?SDL_FRect{80,318+38.0F*i,516,34}:SDL_FRect{80,232+46.0F*i,516,42};}
SDL_FRect uiAllocationWishRect(std::size_t i) noexcept {return {64,186+42.0F*i,548,36};}
SDL_FRect uiAllocationDetailsRect(T tab) noexcept {
    if(tab==T::Overview)return {84,426,1112,104};
    if(tab==T::Wishes)return {652,280,544,208};
    return {652,294,544,238};
}
SDL_FRect uiAllocationFeedbackRect() noexcept {return {64,610,1152,50};}
std::optional<UiAllocationHit> hitUiAllocation(Vec2 point,const UiAllocationState &s,const UiAllocationProjection &p) {
    for(auto c:{C::Overview,C::Supply,C::Wishes,C::Materials,C::Close})if(uiContains(uiAllocationControlRect(c),point))return UiAllocationHit{c};
    if(s.tab==T::Overview) {
        if(uiContains(uiAllocationControlRect(C::Details),point))return UiAllocationHit{C::Details};
        return {};
    }
    if(s.tab==T::Supply)for(auto c:{C::Food,C::Medical,C::Recreation,C::Security})if(uiContains(uiAllocationControlRect(c),point))return UiAllocationHit{c};
    if(s.tab==T::Wishes) {
        for(std::size_t i=0;i<p.wishes.size();++i)if(uiContains(uiAllocationWishRect(i),point))return UiAllocationHit{C::Wish,i,{},0,BaseWishInstanceId{p.cycle,p.wishes[i].definitionId}};
        for(auto c:{C::Focus,C::ClearFocus,C::Filter})if(uiContains(uiAllocationControlRect(c),point))return UiAllocationHit{c};
    }
    for(auto c:{C::Main,C::Previous,C::Next})if(uiContains(uiAllocationControlRect(c),point))return UiAllocationHit{c};
    const auto first=s.pages[static_cast<int>(s.tab)]*uiAllocationPageSize(s.tab);
    for(std::size_t i=0;i<uiAllocationPageSize(s.tab) && first+i<p.rows.size();++i)
        if(uiContains(uiAllocationRowRect(s.tab,i),point)) {
            const auto &row=p.rows[first+i]; return UiAllocationHit{C::Row,0,row.definitionId,row.instanceId};
        }
    return {};
}
