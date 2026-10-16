#include "app.h"
#include <fmt/format.h>
#include <cmath>

void App::renderProfileMeleeDetails(const AssetRecord &asset,const ItemDefinition &definition)
{
    const auto &use=*definition.meleeUse;
    const SDL_FRect panel{40,70,1200,600},back{860,92,340,32};
    SDL_SetRenderDrawColor(renderer_,24,29,30,250);SDL_RenderFillRect(renderer_,&panel);
    SDL_SetRenderDrawColor(renderer_,43,60,65,255);SDL_RenderFillRect(renderer_,&back);
    const auto text=[&](float x,float y,const std::string &label){SDL_SetRenderDrawColor(renderer_,232,225,201,255);uiTextRenderer_.render(renderer_,x,y,label.c_str());};
    text(70,100,"MELEE DETAILS");text(866,100,"BACK TO INVENTORY");text(70,155,definition.displayName);
    text(70,210,fmt::format("MELEE DURABILITY {:.1f}/{:.1f}",asset.currentDurability/100.0F,asset.currentMaximumDurability/100.0F));
    text(70,250,fmt::format("MELEE DAMAGE {}",use.damage));
    text(70,290,fmt::format("MELEE RANGE {:.0f} | ANGLE {:.0f}",use.range,use.angleDegrees));
    text(70,330,fmt::format("攻击恢复 {:.2f} 秒",use.recoverySeconds));
    text(70,370,fmt::format("刀具重量 {} 克 | 尺寸 {} × {} 格",definition.unitWeightGrams,definition.inventoryWidthCells,definition.inventoryHeightCells));
    text(650,210,"V QUICK MELEE | KEEP CURRENT FIREARM");
    text(650,250,"ONE TARGET | WALLS AND ARMOR APPLY");
    text(650,290,"WEAR ONLY ON CONTACT | NO STUN LOCK");
    text(650,330,"BASE FULL MAINTENANCE | NO RAID REPAIR");
    text(70,450,asset.currentDurability ? "READY FOR EMERGENCY SELF DEFENSE":"UTILITY KNIFE BROKEN | BASE MAINTENANCE");
    text(70,550,"EQUIP IN QUICK MELEE SLOT");
    if(!uiMessage_.empty())text(70,610,uiMessage_);
}

void App::renderMeleeSwing(const WorldMeleeRuntime &melee)
{
    if(melee.swingSeconds()<=0)return;
    const auto origin=melee.swingOrigin(),direction=melee.swingDirection();
    const float heading=std::atan2(direction.y,direction.x);
    SDL_BlendMode previous;SDL_GetRenderDrawBlendMode(renderer_,&previous);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_,223,216,188,static_cast<Uint8>(180*melee.swingSeconds()/0.15F));
    Vec2 last{};
    for(int n=0;n<=12;++n){const float angle=heading-0.785398163F+n*1.570796327F/12;
        const Vec2 p{origin.x+std::cos(angle)*72,origin.y+std::sin(angle)*72};
        if(n)SDL_RenderLine(renderer_,last.x,last.y,p.x,p.y);last=p;}
    const float sweep=heading-0.785398163F+(1-melee.swingSeconds()/0.15F)*1.570796327F;
    SDL_RenderLine(renderer_,origin.x,origin.y,origin.x+std::cos(sweep)*72,origin.y+std::sin(sweep)*72);
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}

void App::renderMeleeStatus()
{
    if(inventoryOverlayState_.isOpen())return;
    const auto id=equippedAsset(gameSession_.profile(),EquipmentSlotKind::QuickMelee);
    const auto *asset=id ? gameSession_.profile().assets.find(*id):nullptr;
    if(asset){SDL_SetRenderDrawColor(renderer_,230,224,194,255);uiTextRenderer_.render(renderer_,16,668,
        asset->currentDurability ? fmt::format("V QUICK MELEE | MELEE DURABILITY {:.1f}",asset->currentDurability/100.0F).c_str()
                                 : "UTILITY KNIFE BROKEN | BASE MAINTENANCE");}
    else if(gameSession_.persistenceMessage()=="EQUIP A UTILITY KNIFE")
    {SDL_SetRenderDrawColor(renderer_,230,224,194,255);uiTextRenderer_.render(renderer_,16,668,"EQUIP A UTILITY KNIFE");}
}
