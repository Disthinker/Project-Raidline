#include "game_session.h"
#include "base_world.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

std::optional<MeleeUseDefinition> GameSession::currentMeleeDefinition() const
{
    const auto id=equippedAsset(profile_,EquipmentSlotKind::QuickMelee);
    const auto *asset=id ? profile_.assets.find(*id):nullptr;
    return asset ? publishedContentRegistry().item(asset->definitionId).meleeUse:std::nullopt;
}
bool GameSession::currentMeleeUsable() const
{
    const auto id=equippedAsset(profile_,EquipmentSlotKind::QuickMelee);
    const auto *asset=id ? profile_.assets.find(*id):nullptr;
    return asset && asset->currentDurability>0 && currentMeleeDefinition().has_value();
}

bool GameSession::prepareMeleeInput(GameplayInput &input,float seconds,const WorldMeleeRuntime &melee)
{
    if(!input.meleePressed && !input.meleeJustPressed && !input.meleeHeavyHeld && !input.meleeHeavyJustPressed)
        meleeSuppressedUntilRelease_=false;
    if (input.inventoryOpen || input.freezeObservationDirection || input.throwableModal ||
        input.weaponSlotJustPressed || input.interactJustPressed || input.reloadJustPressed ||
        input.reloadGestureJustPressed || input.healJustPressed)
    {
        input.meleeHeavyHeld = input.meleeHeavyJustPressed = false;
    }
    if(input.inventoryOpen || input.freezeObservationDirection)
    {
        if(input.meleePressed || input.meleeJustPressed)meleeSuppressedUntilRelease_=true;
        input.meleeJustPressed=false;
    }
    if(meleeSuppressedUntilRelease_)input.meleeJustPressed=input.meleeHeavyJustPressed=false;
    const bool requested=input.meleeJustPressed || (input.meleeHeavyJustPressed && input.meleeHeavyHeld);
    const bool winding = melee.chargingHeavy() && input.meleeHeavyHeld;
    const bool recovering=melee.recoverySeconds()>(std::isfinite(seconds) ? std::max(0.0F,seconds):0.0F)+0.000001F;
    if(requested && !recovering && raidActionState_.active())
    {
        const bool cancelOnly=std::visit([](const auto &a){using A=std::decay_t<decltype(a)>;
            return std::is_same_v<A,MedicalRaidAction> || std::is_same_v<A,HealRaidAction> ||
                std::is_same_v<A,WeaponMaintenanceRaidAction> || std::is_same_v<A,ArmorMaintenanceRaidAction>;},*raidActionState_.active());
        if (!cancelActiveRaidAction())
        {
            state_ = GameSessionState::SettlementBlocked;
            input.meleeJustPressed = input.meleeHeavyJustPressed = input.meleeHeavyHeld = false;
            meleeSuppressedUntilRelease_ = true;
            return true;
        }
        if(cancelOnly){input.meleeJustPressed=input.meleeHeavyJustPressed=input.meleeHeavyHeld=false;meleeSuppressedUntilRelease_=true;}
    }
    if(requested || recovering || winding)
    {
        cancelWeaponControlIntent();
        fireSuppressedUntilRelease_=fireSuppressedUntilRelease_ || input.firePressed || input.fireJustPressed;
        sprintSuppressedUntilRelease_=sprintSuppressedUntilRelease_ || input.sprint;
        input.firePressed=input.fireJustPressed=input.sprint=false;
        input.reloadJustPressed=input.healJustPressed=input.interactPressed=input.interactJustPressed=false;
        input.weaponSlotJustPressed.reset();
        sprintFireIntentPending_=false;sprintFireReadyRemaining_=0;weaponClearGesture_.reset();
        if(requested && !currentMeleeDefinition())persistenceMessage_="EQUIP A UTILITY KNIFE";
        else if (input.meleeHeavyJustPressed && !currentMeleeUsable())persistenceMessage_="UTILITY KNIFE BROKEN | BASE MAINTENANCE";
    }
    if(recovering)input.meleeJustPressed=input.meleeHeavyJustPressed=false;
    if(requested)meleeSuppressedUntilRelease_=true;
    return requested || recovering || winding;
}

namespace
{
template<class World>
bool commitMeleeWear(ProfileState &profile,World &world,bool allow,std::string &message,bool &changed)
{
    const auto result=world.melee().result();
    if(!result.attempted || result.committed)return true;
    if(!allow){world.cancelPreparedMelee();return true;}
    const auto equipped=equippedAsset(profile,EquipmentSlotKind::QuickMelee);
    const auto *asset=equipped ? profile.assets.find(*equipped):nullptr;
    if(!asset || *equipped!=result.equipmentAssetId){world.cancelPreparedMelee();message="MELEE EQUIPMENT CHANGED";return false;}
    const auto &definition=publishedContentRegistry().item(asset->definitionId);
    if(!definition.meleeUse || (result.contact && (asset->currentDurability==0 ||
        profile.revision==std::numeric_limits<ProfileRevision>::max() || !validateProfileState(profile,publishedContentRegistry()).valid)))
    {world.cancelPreparedMelee();message="MELEE EQUIPMENT INVALID";return false;}
    const auto wear=definition.meleeUse->wearPerHitCenti;
    const auto id=*equipped;
    // Every rejectable participant is checked before HP changes. The world
    // cannot mutate Registry; this tiny prevalidated wear commit cannot fail.
    if(!world.commitPreparedMelee()){world.cancelPreparedMelee();return true;}
    if(result.contact)
    {
        auto *current=profile.assets.findMutable(id);
        current->currentDurability-=std::min(current->currentDurability,wear);
        ++profile.revision;changed=true;
    }
    if(result.broken)message="UTILITY KNIFE BROKEN | BASE MAINTENANCE";
    return true;
}
}
bool GameSession::commitPlayerMelee(GameplayWorld &world,bool allow)
{
    bool changed{};
    const bool ok=commitMeleeWear(profile_,world,allow,persistenceMessage_,changed);
    return ok;
}
bool GameSession::commitPlayerMelee(BaseWorld &world,bool allow,ProfileState *privateCandidate)
{
    bool changed{};
    const bool ok=commitMeleeWear(privateCandidate?*privateCandidate:profile_,world,allow,persistenceMessage_,changed);
    worldClockDirty_=worldClockDirty_ || changed;return ok;
}
