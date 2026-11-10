#include "ui_quick_wheel_presentation.h"
#include "noise_decoy_domain.h"
#include "grenade_domain.h"
#include "weapon_ammo_domain.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <set>

namespace {
std::string_view roundCode(const ContentRegistry &content, const ItemDefinitionId &id) {
    if (id == kExpanding545AmmunitionId) return "HP";
    const auto &ammo = content.item(id).ammunitionUse;
    if (!ammo) return "?";
    return ammo->tier == AmmunitionTier::Enhanced ? "E" : "S";
}

bool inWornRig(const ProfileState &p,const ContentRegistry &c,const AssetRecord &a,UiQuickWheelKind kind) {
    const auto rig=equippedAsset(p,EquipmentSlotKind::ChestRig);
    const auto *stored=std::get_if<StoredAssetLocation>(&a.location);
    if(!rig || !stored || stored->container.kind!=ProfileContainerKind::AssetCompartment ||
       stored->container.ownerAssetId!=*rig)return false;
    const auto *owner=p.assets.find(*rig);if(!owner)return false;
    const auto &pockets=c.item(owner->definitionId).containerCompartments;
    if(stored->container.compartmentIndex>=pockets.size() ||
       pockets[stored->container.compartmentIndex].pocketKind!=ContainerPocketKind::General)return false;
    const auto &d=c.item(a.definitionId);
    return kind==UiQuickWheelKind::Medical ? d.medicalUse.has_value() && a.remainingCharges>0 :
        (d.noiseDecoyUse.has_value() || d.grenadeUse.has_value()) && a.quantity>0;
}
}
std::vector<AssetInstanceId> quickWheelCandidates(const ProfileState &p,const ContentRegistry &c,UiQuickWheelKind kind,
    std::optional<AssetInstanceId> weaponId) {
    if(kind==UiQuickWheelKind::Magazine) {
        std::vector<AssetInstanceId> result;
        if(weaponId)for(const auto &choice:queryRaidReloadMagazines(p,c,*weaponId))
            result.push_back(choice.magazineAssetId);
        return result;
    }
    std::map<ItemDefinitionId,AssetInstanceId> groups;
    for(const auto &[id,a]:p.assets.records())if(inWornRig(p,c,a,kind)) {
        auto [it,inserted]=groups.emplace(a.definitionId,id);
        if(!inserted) {
            const auto *old=p.assets.find(it->second);
            if(kind==UiQuickWheelKind::Medical && a.remainingCharges<old->remainingCharges)it->second=id;
        }
    }
    std::vector<AssetInstanceId> result;for(const auto &[definition,id]:groups)result.push_back(id);
    if(kind==UiQuickWheelKind::Medical)std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){
        return c.item(p.assets.find(a)->definitionId).medicalUse->effect < c.item(p.assets.find(b)->definitionId).medicalUse->effect;
    });
    return result;
}
std::optional<std::size_t> quickWheelSector(Vec2 offset) noexcept {
    if(offset.x*offset.x+offset.y*offset.y<900)return std::nullopt;
    constexpr float pi=3.14159265358979323846F;
    const float angle=std::atan2(offset.y,offset.x)+pi/2;
    const int sector=static_cast<int>(std::floor((angle+pi/8)/(pi/4)));
    return static_cast<std::size_t>((sector%8+8)%8);
}
Vec2 moveQuickWheelPointer(Vec2 offset,Vec2 delta) noexcept {
    offset.x+=delta.x;offset.y+=delta.y;
    const float length=std::hypot(offset.x,offset.y);
    if(length>200) {offset.x*=200/length;offset.y*=200/length;}
    return offset;
}
std::vector<UiQuickWheelEntry> projectUiQuickWheel(
    const ProfileState &profile,const ContentRegistry &content,
    const std::vector<AssetInstanceId> &ids,UiQuickWheelKind kind,
    std::optional<AssetInstanceId> weaponId) {
    std::vector<UiQuickWheelEntry> result;
    std::set<ItemDefinitionId> seen;
    const auto *weapon = kind == UiQuickWheelKind::Magazine && weaponId
        ? profile.assets.find(*weaponId) : nullptr;
    const bool show545Details = weapon &&
        content.ammunitionFitsWeapon(kExpanding545AmmunitionId, weapon->definitionId);
    const auto chamber = show545Details && weapon->chamberedRound
        ? roundCode(content, weapon->chamberedRound->definitionId) : std::string_view{"–"};
    const auto magazines=kind==UiQuickWheelKind::Magazine && weaponId ?
        queryRaidReloadMagazines(profile,content,*weaponId):std::vector<RaidReloadMagazineChoice>{};
    for(const auto id:ids) {
        UiQuickWheelEntry entry{id,"ITEM NO LONGER AVAILABLE","","ITEM STATE CHANGED",false};
        if(const auto *asset=profile.assets.find(id)) {
            if(kind!=UiQuickWheelKind::Magazine && !seen.insert(asset->definitionId).second)continue;
            entry.definitionId=asset->definitionId;entry.name=content.item(asset->definitionId).displayName;
            if(kind==UiQuickWheelKind::Magazine) {
                const auto choice=std::find_if(magazines.begin(),magazines.end(),
                    [id](const auto &value){return value.magazineAssetId==id;});
                entry.quantity=1;
                entry.remainingUses=magazineRoundCount(profile,id);
                const bool details = show545Details &&
                    content.ammunitionFitsMagazine(kExpanding545AmmunitionId, asset->definitionId);
                if (details) {
                    entry.chamberRound = chamber;
                    entry.magazineTopRound = asset->magazineRounds.empty() ? "–" :
                        roundCode(content, asset->magazineRounds.front().definitionId);
                }
                if (!asset->magazineRounds.empty()) {
                    const auto &top = asset->magazineRounds.front().definitionId;
                    entry.mixedAmmunition = std::any_of(asset->magazineRounds.begin(), asset->magazineRounds.end(),
                        [&](const auto &round) { return round.definitionId != top; });
                }
                entry.usable=choice!=magazines.end() && choice->eligible;
                const auto &definition=content.item(asset->definitionId);
                entry.count=fmt::format("{} / {}",entry.remainingUses,
                    definition.magazineCapacity);
                entry.status=entry.usable?"READY TO RELOAD":"MAGAZINE INVENTORY CHANGED";
                result.push_back(std::move(entry));
                continue;
            }
            for(const auto &[otherId,other]:profile.assets.records())
                if(other.definitionId==asset->definitionId && inWornRig(profile,content,other,kind)) {
                    entry.quantity+=other.quantity;entry.remainingUses+=other.remainingCharges;
                }
            if(kind==UiQuickWheelKind::Medical) {
                const auto plan=queryMedicalUse(profile,content,id,MedicalAccess::CarriedOnly);
                entry.usable=inWornRig(profile,content,*asset,kind) && plan.canCommit;
                entry.status=entry.usable?"READY TO USE":plan.message;
                entry.count=fmt::format("x{} / {} USES",entry.quantity,entry.remainingUses);
            } else {
                const auto &definition=content.item(asset->definitionId);
                entry.usable=definition.grenadeUse ? grenadeInChestRig(profile,content,id) :
                    noiseDecoyInChestRig(profile,content,id);
                entry.status=entry.usable?"READY TO SELECT | AIM BEFORE THROWING":
                    definition.grenadeUse?"GRENADE INVENTORY CHANGED":"DECOY INVENTORY CHANGED";
                entry.count=fmt::format("x{}",entry.quantity);
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}
bool UiQuickWheelProjection::refresh(
    const ProfileState &profile, const ContentRegistry &content,
    const std::vector<AssetInstanceId> &ids, UiQuickWheelKind kind,
    std::optional<AssetInstanceId> weaponId) {
    // These are precisely the mutable inputs of the existing medical query.
    // Countdown milliseconds do not change applicability and need no reprojection.
    if (valid_ && profileId_ == profile.profileId && revision_ == profile.revision &&
        health_ == profile.currentHealth && bleeding_ == profile.medicalStatus.bleeding &&
        kind_ == kind && ids_ == ids && weaponId_ == weaponId) return false;
    entries = projectUiQuickWheel(profile, content, ids, kind, weaponId);
    valid_ = true; profileId_ = profile.profileId; revision_ = profile.revision;
    health_ = profile.currentHealth; bleeding_ = profile.medicalStatus.bleeding;
    kind_ = kind; ids_ = ids; weaponId_ = weaponId; ++generation;
    return true;
}

UiQuickWheelLayout layoutUiQuickWheel(
    const std::vector<UiQuickWheelEntry> &entries, std::size_t selected,
    UiQuickWheelKind kind, UiLanguage language, const UiQuickWheelMeasure &measure, std::size_t page) {
    UiQuickWheelLayout l;l.selected=selected;l.page=page;
    if (kind == UiQuickWheelKind::Magazine && selected < entries.size() && !entries[selected].chamberRound.empty()) {
        const auto &entry = entries[selected];
        auto count = entry.count;
        std::erase(count, ' ');
        l.count.size = 12;
        if (language == UiLanguage::SimplifiedChinese) {
            const auto chineseRound = [](std::string_view round) {
                if (round == "HP") return std::string_view{"扩"};
                if (round == "E") return std::string_view{"强"};
                if (round == "S") return std::string_view{"标"};
                return round;
            };
            l.count.text = fmt::format("{} 顶{} 膛{}", count,
                chineseRound(entry.magazineTopRound), chineseRound(entry.chamberRound));
        } else {
            l.count.text = fmt::format("{} TOP:{} CH:{}", count, entry.magazineTopRound, entry.chamberRound);
            if (measure(l.count.text, l.count.size) > 110)
                l.count.text = fmt::format("{} T:{} C:{}", count, entry.magazineTopRound, entry.chamberRound);
        }
        if (measure(l.count.text, l.count.size) > 110) l.count.size = 11;
    }
    constexpr float pi=3.14159265358979323846F;
    for(std::size_t i=0;i<8;++i) {
        const float angle=-pi/2+static_cast<float>(i)*pi/4;
        l.markers.push_back({640+std::cos(angle)*160-28,360+std::sin(angle)*160-28,56,56});
    }
    const auto pages=std::max<std::size_t>(1,(entries.size()+7)/8);
    if(pages>1)l.range={{600,450,80,18},fmt::format("{} / {}",page+1,pages),12};
    return l;
}
void UiQuickWheelEmphasis::select(AssetInstanceId next, UiQuickWheelKind nextKind, float now) noexcept {
    if (id != next || kind != nextKind) { id = next; kind = nextKind; started = now; }
}
float UiQuickWheelEmphasis::amount(float now) const noexcept {
    const float t=std::clamp((now-started)/.12F,0.0F,1.0F);
    return t*t*(3-2*t);
}
