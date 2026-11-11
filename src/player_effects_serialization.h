#pragma once

#include "profile_state.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>

// Value-only schema60 codec. The Profile validator owns semantic legality.
namespace raidline::save60
{
using Json=nlohmann::json;
inline std::uint64_t integer(const Json &j,bool nonzero=false)
{
    if(!j.is_number_unsigned() || (nonzero && j.get<std::uint64_t>()==0))
        throw std::invalid_argument("schema60 requires an unsigned integer");
    return j.get<std::uint64_t>();
}
inline int signedInteger(const Json &j)
{
    if(!j.is_number_integer() || (j.is_number_unsigned() && j.get<std::uint64_t>()>static_cast<std::uint64_t>(std::numeric_limits<int>::max())))
        throw std::invalid_argument("schema60 requires an int32 integer");
    const auto value=j.get<std::int64_t>();
    if(value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max())throw std::invalid_argument("schema60 int32 overflow");
    return static_cast<int>(value);
}
inline unsigned unsignedInteger(const Json &j)
{
    const auto value=integer(j);if(value>std::numeric_limits<unsigned>::max())throw std::invalid_argument("schema60 uint32 overflow");
    return static_cast<unsigned>(value);
}
inline double number(const Json &j)
{
    if(!j.is_number())throw std::invalid_argument("schema60 requires a numeric value");
    const auto value=j.get<double>();if(!std::isfinite(value))throw std::invalid_argument("schema60 requires finite numbers");return value;
}
inline float real(const Json &j)
{
    const auto value=number(j);if(std::abs(value)>std::numeric_limits<float>::max())throw std::invalid_argument("schema60 float overflow");
    return static_cast<float>(value);
}
inline std::string text(const Json &j,bool nonempty=true)
{
    const auto value=j.get<std::string>();if(nonempty && value.empty())throw std::invalid_argument("schema60 requires a nonempty identity");return value;
}
inline Json point(Vec2 p){return {{"x",p.x},{"y",p.y}};}
inline Vec2 parsePoint(const Json &j){return {real(j.at("x")),real(j.at("y"))};}
inline Json scope(const WeaponActionScope &s)
{
    if(s.activity!=WeaponActionActivity::BaseDaily && s.activity!=WeaponActionActivity::BaseDefense)
        throw std::invalid_argument("isolated or Raid activity cannot be written as Base recovery");
    return {{"activity",s.activity==WeaponActionActivity::BaseDaily?"base_daily":"base_defense"},
        {"base_site_definition_id",s.baseSiteDefinitionId.value()},{"activity_id",s.activityId}};
}
inline WeaponActionScope parseScope(const Json &j)
{
    const auto activity=text(j.at("activity"));
    if(activity!="base_daily" && activity!="base_defense")throw std::invalid_argument("unknown Base activity");
    return {activity=="base_daily"?WeaponActionActivity::BaseDaily:WeaponActionActivity::BaseDefense,
        RegionalBaseSiteDefinitionId{text(j.at("base_site_definition_id"))},text(j.at("activity_id"))};
}
inline std::string role(RaidReloadMagazineRole r)
{
    switch(r){case RaidReloadMagazineRole::NewMagazine:return "new_magazine";case RaidReloadMagazineRole::OldMagazine:return "old_magazine";}
    throw std::invalid_argument("unknown reload role");
}
inline RaidReloadMagazineRole parseRole(const Json &j)
{
    const auto s=text(j);if(s=="new_magazine")return RaidReloadMagazineRole::NewMagazine;if(s=="old_magazine")return RaidReloadMagazineRole::OldMagazine;
    throw std::invalid_argument("unknown reload role");
}
inline Json stored(const StoredAssetLocation &s)
{
    Json container;
    if(s.container==ProfileContainerId::stash())container={{"type","stash"}};
    else if(s.container.kind==ProfileContainerKind::AssetCompartment)
        container={{"type","asset_compartment"},{"owner_asset_id",s.container.ownerAssetId},{"compartment",s.container.compartmentIndex}};
    else throw std::invalid_argument("Base reload has an unsupported source container");
    return {{"type","stored"},{"container",container},{"origin",{{"x",s.origin.x},{"y",s.origin.y}}}};
}
inline StoredAssetLocation parseStored(const Json &j)
{
    if(text(j.at("type"))!="stored")throw std::invalid_argument("reload placement is not stored");
    const auto &c=j.at("container");const auto type=text(c.at("type"));ProfileContainerId container;
    if(type=="stash")container=ProfileContainerId::stash();
    else if(type=="asset_compartment")container=ProfileContainerId::compartment(integer(c.at("owner_asset_id"),true),unsignedInteger(c.at("compartment")));
    else throw std::invalid_argument("unsupported reload placement container");
    return {container,{signedInteger(j.at("origin").at("x")),signedInteger(j.at("origin").at("y"))}};
}
inline std::string orientation(ItemOrientation o)
{
    if(o==ItemOrientation::Degrees0)return "0";if(o==ItemOrientation::Degrees90)return "90";
    throw std::invalid_argument("invalid recovery orientation");
}
inline ItemOrientation parseOrientation(const Json &j)
{
    const auto s=text(j);if(s=="0")return ItemOrientation::Degrees0;if(s=="90")return ItemOrientation::Degrees90;
    throw std::invalid_argument("invalid recovery orientation");
}
inline Json reload(const BaseReloadRecoveryState &r)
{
    std::string source;
    switch(r.source){case RaidReloadSource::AutomaticChestRig:source="automatic_chest_rig";break;case RaidReloadSource::SelectedChestRig:source="selected_chest_rig";break;case RaidReloadSource::ExplicitStored:source="explicit_stored";break;default:throw std::invalid_argument("invalid recovery source");}
    if(r.kind!=RaidReloadKind::Normal && r.kind!=RaidReloadKind::Emergency)throw std::invalid_argument("invalid recovery route");
    return {{"activity",scope(r.activity)},{"weapon_asset_id",r.weaponAssetId},{"new_magazine_asset_id",r.newMagazineAssetId},
        {"old_magazine_asset_id",r.oldMagazineAssetId?Json(*r.oldMagazineAssetId):Json(nullptr)},{"action_id",r.actionId},
        {"new_source",stored(r.newSource)},{"new_source_orientation",orientation(r.newSourceOrientation)},
        {"old_return",r.oldReturn?stored(*r.oldReturn):Json(nullptr)},{"old_return_orientation",orientation(r.oldReturnOrientation)},
        {"kind",r.kind==RaidReloadKind::Normal?"normal":"emergency"},{"source",source}};
}
inline BaseReloadRecoveryState parseReload(const Json &j)
{
    BaseReloadRecoveryState r;r.activity=parseScope(j.at("activity"));r.weaponAssetId=integer(j.at("weapon_asset_id"),true);
    r.newMagazineAssetId=integer(j.at("new_magazine_asset_id"),true);
    if(!j.at("old_magazine_asset_id").is_null())r.oldMagazineAssetId=integer(j.at("old_magazine_asset_id"),true);
    r.actionId=text(j.at("action_id"));r.newSource=parseStored(j.at("new_source"));r.newSourceOrientation=parseOrientation(j.at("new_source_orientation"));
    if(!j.at("old_return").is_null())r.oldReturn=parseStored(j.at("old_return"));
    r.oldReturnOrientation=parseOrientation(j.at("old_return_orientation"));
    const auto kind=text(j.at("kind"));if(kind=="normal")r.kind=RaidReloadKind::Normal;else if(kind=="emergency")r.kind=RaidReloadKind::Emergency;else throw std::invalid_argument("unknown recovery route");
    const auto source=text(j.at("source"));
    if(source=="automatic_chest_rig")r.source=RaidReloadSource::AutomaticChestRig;else if(source=="selected_chest_rig")r.source=RaidReloadSource::SelectedChestRig;else if(source=="explicit_stored")r.source=RaidReloadSource::ExplicitStored;else throw std::invalid_argument("unknown recovery source");
    return r;
}
inline Json corpse(const BaseCorpseRecord &c)
{
    return {{"corpse_id",c.corpseId},{"activity",scope(c.activity)},{"source_enemy_id",c.sourceEnemyId},
        {"cycle_index",c.cycleIndex},{"position",point(c.position)},{"size",point(c.size)}};
}
inline BaseCorpseRecord parseCorpse(const Json &j)
{
    return {text(j.at("corpse_id")),parseScope(j.at("activity")),integer(j.at("source_enemy_id"),true),integer(j.at("cycle_index")),parsePoint(j.at("position")),parsePoint(j.at("size"))};
}
inline Json grenadeUse(const GrenadeUseDefinition &u)
{
    return {{"range",u.range},{"flight_seconds",u.flightSeconds},{"fuse_seconds",u.fuseSeconds},{"radius",u.radius},
        {"core_radius",u.coreRadius},{"core_damage",u.coreDamage},{"edge_damage",u.edgeDamage},{"penetration",u.penetration},
        {"armor_damage",u.armorDamage},{"noise_radius",u.noiseRadius}};
}
inline GrenadeUseDefinition parseGrenadeUse(const Json &j)
{
    return {real(j.at("range")),real(j.at("flight_seconds")),real(j.at("fuse_seconds")),real(j.at("radius")),real(j.at("core_radius")),
        signedInteger(j.at("core_damage")),signedInteger(j.at("edge_damage")),signedInteger(j.at("penetration")),signedInteger(j.at("armor_damage")),real(j.at("noise_radius"))};
}
inline Json noiseUse(const NoiseDecoyUseDefinition &u)
{
    return {{"range",u.range},{"flight_seconds",u.flightSeconds},{"radius",u.radius},{"duration_seconds",u.durationSeconds},{"pulse_interval_seconds",u.pulseIntervalSeconds}};
}
inline NoiseDecoyUseDefinition parseNoiseUse(const Json &j)
{
    return {real(j.at("range")),real(j.at("flight_seconds")),real(j.at("radius")),real(j.at("duration_seconds")),real(j.at("pulse_interval_seconds"))};
}
inline Json landing(const GrenadeLanding &l)
{return {{"origin",point(l.origin)},{"point",point(l.point)},{"space_id",l.space.value()},{"use",grenadeUse(l.use)},{"effect_identity",l.effectIdentity}};}
inline Json landing(const NoiseDecoyLanding &l)
{return {{"origin",point(l.origin)},{"point",point(l.point)},{"space_id",l.space.value()},{"use",noiseUse(l.use)},{"effect_identity",l.effectIdentity}};}
inline GrenadeLanding parseGrenadeLanding(const Json &j)
{return {parsePoint(j.at("origin")),parsePoint(j.at("point")),RaidSpaceDefinitionId{text(j.at("space_id"))},parseGrenadeUse(j.at("use")),text(j.at("effect_identity"))};}
inline NoiseDecoyLanding parseNoiseLanding(const Json &j)
{return {parsePoint(j.at("origin")),parsePoint(j.at("point")),RaidSpaceDefinitionId{text(j.at("space_id"))},parseNoiseUse(j.at("use")),text(j.at("effect_identity"))};}
inline Json illuminationLanding(const ThrownIlluminationLanding &value)
{
    return {{"origin", point(value.origin)}, {"point", point(value.point)},
        {"space_id", value.space.value()}, {"effect_identity", value.effectIdentity},
        {"use", {{"maximum_range", value.use.maximumRange},
                 {"flight_seconds", value.use.flightSeconds},
                 {"radius", value.use.radius}, {"duration_seconds", value.use.durationSeconds}}}};
}

inline ThrownIlluminationLanding parseIlluminationLanding(const Json &value)
{
    const auto &use = value.at("use");
    return {parsePoint(value.at("origin")), parsePoint(value.at("point")),
        RaidSpaceDefinitionId{text(value.at("space_id"))},
        {real(use.at("maximum_range")), real(use.at("flight_seconds")),
         real(use.at("radius")), real(use.at("duration_seconds"))},
        text(value.at("effect_identity"))};
}

inline std::string medicalEffect(MedicalUseEffect e)
{
    switch(e){case MedicalUseEffect::RestoreHealth:return "restore_health";case MedicalUseEffect::StopLightBleeding:return "stop_light_bleeding";case MedicalUseEffect::StopAnyBleeding:return "stop_any_bleeding";case MedicalUseEffect::SuppressPain:return "suppress_pain";}
    throw std::invalid_argument("unknown medical recovery effect");
}
inline MedicalUseEffect parseMedicalEffect(const Json &j)
{
    const auto s=text(j);if(s=="restore_health")return MedicalUseEffect::RestoreHealth;if(s=="stop_light_bleeding")return MedicalUseEffect::StopLightBleeding;if(s=="stop_any_bleeding")return MedicalUseEffect::StopAnyBleeding;if(s=="suppress_pain")return MedicalUseEffect::SuppressPain;
    throw std::invalid_argument("unknown medical recovery effect");
}
inline std::string activeSlot(EquipmentSlotKind s)
{
    if(s==EquipmentSlotKind::PrimaryWeapon)return "primary_weapon";if(s==EquipmentSlotKind::SecondaryWeapon)return "secondary_weapon";if(s==EquipmentSlotKind::Sidearm)return "sidearm";
    throw std::invalid_argument("invalid active weapon recovery slot");
}
inline EquipmentSlotKind parseActiveSlot(const Json &j)
{
    const auto s=text(j);if(s=="primary_weapon")return EquipmentSlotKind::PrimaryWeapon;if(s=="secondary_weapon")return EquipmentSlotKind::SecondaryWeapon;if(s=="sidearm")return EquipmentSlotKind::Sidearm;
    throw std::invalid_argument("invalid active weapon recovery slot");
}
inline Json effects(const BasePlayerEffectsRecovery &r, bool illuminationContract)
{
    Json medical=nullptr;
    if(r.medical){const auto&m=*r.medical;medical={{"medical_asset_id",m.medicalAssetId},{"effect",medicalEffect(m.effect)},{"charge_consumed",m.chargeConsumed},
        {"healed_amount",m.healedAmount},{"maximum_healing",m.maximumHealing},{"elapsed_seconds",m.elapsedSeconds},{"duration_seconds",m.durationSeconds}};}
    const auto &g=r.grenade;Json grenade={{"next_explosion_id",g.nextExplosionId},{"flight",nullptr},{"fuse",nullptr},{"explosion",nullptr}};
    if(g.flight)grenade["flight"]={{"landing",landing(g.flight->landing)},{"elapsed",g.flight->elapsed}};
    if(g.fuse)grenade["fuse"]={{"landing",landing(g.fuse->landing)},{"elapsed",g.fuse->elapsed}};
    if(g.explosion)grenade["explosion"]={{"id",g.explosion->id},{"landing",landing(g.explosion->landing)}};
    const auto &n=r.noiseDecoy;Json decoy={{"next_source_id",n.nextSourceId},{"flight",nullptr},{"source",nullptr},{"investigations",Json::array()}};
    if(n.flight)decoy["flight"]={{"landing",landing(n.flight->landing)},{"elapsed",n.flight->elapsed}};
    if(n.source)decoy["source"]={{"id",n.source->id},{"landing",landing(n.source->landing)},{"elapsed",n.source->elapsed},{"pulses",n.source->pulses}};
    for(const auto &entry:n.investigations){const auto&i=entry.investigation;decoy["investigations"].push_back({{"target_id",entry.targetId},
        {"space_id",i.space.value()},{"source_id",i.source},{"point",point(i.point)},{"home",point(i.home)},{"returning",i.returning}});}
    Json payload = {{"scope",scope(r.scope)},{"world_size",point(r.worldSize)},{"player_position",point(r.playerPosition)},{"facing_direction",point(r.facingDirection)},
        {"control_remaining_seconds",r.controlRemainingSeconds},{"impact_slow_remaining_seconds",r.impactSlowRemainingSeconds},
        {"medical_tick_accumulator_seconds",r.medicalTickAccumulatorSeconds},{"daily_damage_protection_seconds",r.dailyDamageProtectionSeconds},
        {"medical",medical},{"grenade",grenade},{"noise_decoy",decoy},{"daily_command_sequence",r.dailyCommandSequence},
        {"daily_weapon_fault_sequence",r.dailyWeaponFaultSequence},{"daily_medical_random_sequence",r.dailyMedicalRandomSequence},
        {"daily_wound_random_sequence",r.dailyWoundRandomSequence},{"daily_active_weapon_slot",activeSlot(r.dailyActiveWeaponSlot)}};
    const auto &illumination = r.thrownIllumination;
    if (illumination.flight || illumination.source)
    {
        if (!illuminationContract)
            throw std::invalid_argument("Base illumination recovery requires schema 65 and content 81");
        Json flight = nullptr;
        Json source = nullptr;
        if (illumination.flight)
            flight = {{"landing", illuminationLanding(illumination.flight->landing)},
                {"elapsed", illumination.flight->elapsed}};
        if (illumination.source)
            source = {{"landing", illuminationLanding(illumination.source->landing)},
                {"elapsed", illumination.source->elapsed},
                {"night_vision_glare_triggered", illumination.source->nightVisionGlareTriggered}};
        payload["thrown_illumination"] = {{"flight", std::move(flight)}, {"source", std::move(source)}};
    }
    return payload;
}
inline BasePlayerEffectsRecovery parseEffects(const Json &j, bool illuminationContract)
{
    BasePlayerEffectsRecovery r;r.scope=parseScope(j.at("scope"));r.worldSize=parsePoint(j.at("world_size"));r.playerPosition=parsePoint(j.at("player_position"));r.facingDirection=parsePoint(j.at("facing_direction"));
    r.controlRemainingSeconds=real(j.at("control_remaining_seconds"));r.impactSlowRemainingSeconds=real(j.at("impact_slow_remaining_seconds"));
    r.medicalTickAccumulatorSeconds=real(j.at("medical_tick_accumulator_seconds"));r.dailyDamageProtectionSeconds=real(j.at("daily_damage_protection_seconds"));
    if(!j.at("medical").is_null()){const auto&m=j.at("medical");r.medical=BaseMedicalActionRecovery{integer(m.at("medical_asset_id"),true),parseMedicalEffect(m.at("effect")),m.at("charge_consumed").get<bool>(),
        signedInteger(m.at("healed_amount")),signedInteger(m.at("maximum_healing")),real(m.at("elapsed_seconds")),real(m.at("duration_seconds"))};}
    const auto &g=j.at("grenade");r.grenade.nextExplosionId=integer(g.at("next_explosion_id"),true);
    if(!g.at("flight").is_null()){const auto&v=g.at("flight");r.grenade.flight=GrenadeFlight{parseGrenadeLanding(v.at("landing")),number(v.at("elapsed"))};}
    if(!g.at("fuse").is_null()){const auto&v=g.at("fuse");r.grenade.fuse=GrenadeFuse{parseGrenadeLanding(v.at("landing")),number(v.at("elapsed"))};}
    if(!g.at("explosion").is_null()){const auto&v=g.at("explosion");r.grenade.explosion=GrenadeExplosion{integer(v.at("id"),true),parseGrenadeLanding(v.at("landing"))};}
    const auto &n=j.at("noise_decoy");r.noiseDecoy.nextSourceId=integer(n.at("next_source_id"),true);
    if(!n.at("flight").is_null()){const auto&v=n.at("flight");r.noiseDecoy.flight=NoiseDecoyFlight{parseNoiseLanding(v.at("landing")),number(v.at("elapsed"))};}
    if(!n.at("source").is_null()){const auto&v=n.at("source");r.noiseDecoy.source=NoiseDecoySource{integer(v.at("id"),true),parseNoiseLanding(v.at("landing")),number(v.at("elapsed")),unsignedInteger(v.at("pulses"))};}
    if(!n.at("investigations").is_array())throw std::invalid_argument("decoy investigations must be an array");
    for(const auto &i:n.at("investigations"))r.noiseDecoy.investigations.push_back({integer(i.at("target_id"),true),
        {RaidSpaceDefinitionId{text(i.at("space_id"))},integer(i.at("source_id"),true),parsePoint(i.at("point")),parsePoint(i.at("home")),i.at("returning").get<bool>()}});
    if (j.contains("thrown_illumination"))
    {
        if (!illuminationContract)
            throw std::invalid_argument("Base illumination recovery requires schema 65 and content 81");
        const auto &illumination = j.at("thrown_illumination");
        const auto &flight = illumination.at("flight");
        const auto &source = illumination.at("source");
        if (flight.is_null() == source.is_null())
            throw std::invalid_argument("Base illumination recovery requires exactly one phase");
        if (!flight.is_null())
            r.thrownIllumination.flight = ThrownIlluminationFlight{
                parseIlluminationLanding(flight.at("landing")), number(flight.at("elapsed"))};
        if (!source.is_null())
            r.thrownIllumination.source = ThrownIlluminationSource{
                parseIlluminationLanding(source.at("landing")), number(source.at("elapsed")),
                source.at("night_vision_glare_triggered").get<bool>()};
    }
    r.dailyCommandSequence=integer(j.at("daily_command_sequence"));r.dailyWeaponFaultSequence=integer(j.at("daily_weapon_fault_sequence"));
    r.dailyMedicalRandomSequence=integer(j.at("daily_medical_random_sequence"));r.dailyWoundRandomSequence=integer(j.at("daily_wound_random_sequence"));r.dailyActiveWeaponSlot=parseActiveSlot(j.at("daily_active_weapon_slot"));
    return r;
}
inline void writePlayerState(Json &payload,const ProfileState &profile,unsigned schema,
    bool illuminationContract)
{
    if(schema<60)
    {
        if(profile.baseReloadRecovery || profile.basePlayerEffects || !profile.baseCorpses.empty())throw std::invalid_argument("Base player recovery requires schema60");
        return;
    }
    payload["base_reload_recovery"]=profile.baseReloadRecovery?reload(*profile.baseReloadRecovery):Json(nullptr);
    payload["base_player_effects"]=profile.basePlayerEffects?effects(*profile.basePlayerEffects, illuminationContract):Json(nullptr);
    payload["base_corpses"]=Json::array();for(const auto &c:profile.baseCorpses)payload["base_corpses"].push_back(corpse(c));
}
inline void parsePlayerState(const Json &payload,ProfileState &profile,unsigned schema,
    bool illuminationContract)
{
    if(schema<60)
    {
        if(payload.contains("base_reload_recovery") || payload.contains("base_player_effects") || payload.contains("base_corpses"))throw std::invalid_argument("legacy save contains schema60 Base recovery");
        return;
    }
    if(!payload.at("base_reload_recovery").is_null())profile.baseReloadRecovery=parseReload(payload.at("base_reload_recovery"));
    if(!payload.at("base_player_effects").is_null())profile.basePlayerEffects=parseEffects(payload.at("base_player_effects"), illuminationContract);
    if(!payload.at("base_corpses").is_array())throw std::invalid_argument("Base corpses must be an array");
    for(const auto &c:payload.at("base_corpses"))profile.baseCorpses.push_back(parseCorpse(c));
}
}
