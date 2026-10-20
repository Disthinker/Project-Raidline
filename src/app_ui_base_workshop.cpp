#include "app.h"
#include <algorithm>
#include <fmt/format.h>

namespace { using Control=UiWorkshopControl; using Kind=UiWorkshopEventKind; }

void App::resetBaseWorkshopUi() {
    workshopUi_={}; workshopProjection_={}; workshopProjectionRevision_.reset();
    pendingWorkshopEvents_.clear(); workshopFeedback_.clear();
}
void App::refreshBaseWorkshopUi(bool force) {
    const auto &p=gameSession_.profile(); const auto language=uiTextRenderer_.language();
    if(workshopProjectionRevision_ && workshopProjection_.profileId!=p.profileId)resetBaseWorkshopUi();
    if(workshopProjectionRevision_ && workshopProjectionLanguage_!=language) {
        workshopUi_.press.reset(); workshopUi_.detailScroll=workshopUi_.feedbackScroll=0;
    }
    if(!force && workshopProjectionRevision_ && *workshopProjectionRevision_==p.revision &&
        workshopProjection_.worldMinute==p.worldClock.elapsedWorldMinutes && workshopProjectionLanguage_==language &&
        workshopProjection_.selection==workshopUi_.selection)return;
    workshopProjection_=projectUiWorkshop(p,publishedContentRegistry(),language,workshopUi_.selection);
    workshopUi_.selection=workshopProjection_.selection;
    workshopUi_.page=std::min(workshopUi_.page,uiWorkshopPageCount(workshopProjection_.recipes.size())-1);
    if(workshopProjection_.order) {
        const auto it=std::find_if(workshopProjection_.recipes.begin(),workshopProjection_.recipes.end(),[&](const auto &r){return workshopProjection_.selection==r.id;});
        if(it!=workshopProjection_.recipes.end())workshopUi_.page=static_cast<std::size_t>(it-workshopProjection_.recipes.begin())/6;
    }
    workshopProjectionRevision_=p.revision; workshopProjectionLanguage_=language; ++workshopProjectionGeneration_;
}
void App::handleBaseWorkshopEvent(const UiWorkshopEvent &e) {
    if(gameFlow_.state()!=GameFlowState::Base || gameFlow_.activeBaseFacility()!=BaseFacilityKind::Workshop ||
        pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        workshopUi_.press.reset(); return;
    }
    refreshBaseWorkshopUi(e.kind==Kind::Down || e.kind==Kind::Up);
    const auto changePage=[&](int delta) {
        workshopUi_.page=static_cast<std::size_t>(std::clamp(static_cast<int>(workshopUi_.page)+delta,0,static_cast<int>(uiWorkshopPageCount(workshopProjection_.recipes.size())-1)));
        workshopUi_.press.reset();
    };
    if(e.kind==Kind::Wheel) {
        workshopUi_.press.reset();
        if(uiContains(uiWorkshopFeedbackRect(),e.position)) workshopUi_.feedbackScroll=std::clamp(workshopUi_.feedbackScroll+e.delta*2,0,workshopUi_.feedbackMaximum);
        else if(uiContains(uiWorkshopDetailsRect(),e.position))workshopUi_.detailScroll=std::clamp(workshopUi_.detailScroll+e.delta*2,0,workshopUi_.detailMaximum);
        else if(e.position.x>=64 && e.position.x<=424 && e.position.y>=138 && e.position.y<=538 && !workshopProjection_.order)changePage(e.delta);
        return;
    }
    const auto hit=hitUiWorkshop(e.position,workshopUi_,workshopProjection_);
    if(e.kind==Kind::Move) {if(workshopUi_.press && hit!=workshopUi_.press->hit)workshopUi_.press.reset(); return;}
    const auto language=uiTextRenderer_.language();
    if(e.kind==Kind::Down) {
        workshopUi_.press.reset();
        if(hit)workshopUi_.press=UiWorkshopPress{*hit,language,uiWorkshopActionKey(*hit,workshopProjection_)};
        return;
    }
    const auto press=workshopUi_.press; workshopUi_.press.reset();
    if(!press || !hit || *hit!=press->hit || press->language!=language)return;
    const auto feedback=[&](std::string s){workshopFeedback_=std::move(s);uiMessage_=workshopFeedback_;workshopUi_.feedbackScroll=0;};
    if(press->action!=uiWorkshopActionKey(*hit,workshopProjection_)) {feedback("PRODUCTION PLAN CHANGED - CHECK AND CLICK AGAIN");return;}
    if(hit->recipe) {workshopUi_.selection=hit->recipe;workshopUi_.detailScroll=0;workshopFeedback_.clear();refreshBaseWorkshopUi(true);return;}
    const auto control=hit->control;
    if(control==Control::Close) {gameFlow_.closeBaseFacility();resetBaseWorkshopUi();uiMessage_.clear();input_.suppressPrimaryPointerUntilRelease();return;}
    if(control==Control::Facilities) {workshopUi_.facilities=!workshopUi_.facilities;workshopUi_.detailScroll=0;return;}
    if(control==Control::Previous || control==Control::Next) {changePage(control==Control::Previous?-1:1);return;}
    const auto &p=workshopProjection_;
    if(!uiWorkshopEnabled(control,p)) {
        feedback(!p.installed?"WORKSHOP IS PACKED | PRODUCTION AND UPGRADES ARE PAUSED":
            control==Control::Staffing?p.staffing.message:control==Control::Upgrade?p.upgrade.message:
            p.order?p.order->outputReady || control==Control::Cancel?p.returns.message:"PRODUCTION ACTIVE":p.selection?p.start.message:"NO PUBLISHED MANUFACTURING RECIPE");
        return;
    }
    bool succeeded=false;
    if(control==Control::Main && !p.order && p.selection) {
        const auto receipt=gameSession_.executeStartBaseManufacturing(*p.selection,nextProfileTransactionId("workshop-start"));
        succeeded=receipt.succeeded;feedback(succeeded?"MANUFACTURING STARTED":receipt.message);
    } else if(control==Control::Main) {
        const auto receipt=gameSession_.executeCollectBaseManufacturing(nextProfileTransactionId("workshop-collect"));
        succeeded=receipt.succeeded;feedback(succeeded?"MANUFACTURED ITEM COLLECTED":receipt.message);
    } else if(control==Control::Cancel) {
        const auto receipt=gameSession_.executeCancelBaseManufacturing(nextProfileTransactionId("workshop-cancel"));
        succeeded=receipt.succeeded;feedback(succeeded?"MANUFACTURING CANCELLED | INPUTS RETURNED":receipt.message);
    } else if(control==Control::Staffing) {
        const bool assigned=p.workforce.workshopWorker.has_value();
        const auto receipt=assigned?gameSession_.executeClearBaseWorker(BaseFacilityStaffingKind::Workshop,nextProfileTransactionId("workshop-clear-worker")):
            gameSession_.executeAssignBestBaseWorker(BaseFacilityStaffingKind::Workshop,nextProfileTransactionId("workshop-assign-worker"));
        succeeded=receipt.succeeded;feedback(succeeded?assigned?"WORKSHOP WORKER CLEARED":"WORKSHOP WORKER ASSIGNED":receipt.message);
    } else if(control==Control::Upgrade && p.upgradeId) {
        const bool cancel=p.cancelUpgrade;
        const auto receipt=cancel?gameSession_.executeCancelBaseConstruction(*p.upgradeId,nextProfileTransactionId("workshop-cancel-upgrade")):
            gameSession_.executeStartBaseConstruction(*p.upgradeId,nextProfileTransactionId("workshop-start-upgrade"));
        succeeded=receipt.succeeded;feedback(succeeded?cancel?"WORKSHOP UPGRADE CANCELLED":"WORKSHOP UPGRADE STARTED":receipt.message);
    }
    gameAudio_.play(succeeded?SoundEventId::UiConfirm:SoundEventId::UiDeny);refreshBaseWorkshopUi(true);
}
void App::renderBaseWorkshop() {
    refreshBaseWorkshopUi(); const auto &p=workshopProjection_;const auto language=uiTextRenderer_.language();
    const auto tr=[&](std::string_view s){return localizeUiText(language,s);};
    const auto fill=[&](SDL_FRect r,SDL_Color c){SDL_SetRenderDrawColor(renderer_,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer_,&r);};
    const auto text=[&](SDL_FRect r,std::string_view s,int size=14){SDL_SetRenderDrawColor(renderer_,222,231,226,255);uiTextRenderer_.renderBox(renderer_,r,s,{size,UiTextAlign::Left,0});};
    const auto button=[&](Control c,std::string_view label,bool selected=false){renderFoundationButton(5900+static_cast<int>(c),uiWorkshopControlRect(c),label,uiWorkshopEnabled(c,p),c==Control::Cancel || (c==Control::Upgrade && p.cancelUpgrade),selected,{},14);};
    const auto duration=[&](std::uint64_t m){return fmt::format("{}{} {}{}",m/60,language==UiLanguage::English?"h":"小时",m%60,language==UiLanguage::English?"m":"分");};
    const auto scroll=[&](SDL_FRect r,std::string_view value,int size,int &offset,int &maximum) {
        const auto lines=wrapUiText(tr(value),r.w-12,0,[&](auto s){return uiTextRenderer_.measure(s,size);});
        const float height=size*1.4F;const int visible=std::max(1,static_cast<int>(r.h/height));
        maximum=std::max(0,static_cast<int>(lines.size())-visible);offset=std::clamp(offset,0,maximum);
        for(int i=0;i<visible && i+offset<static_cast<int>(lines.size());++i)text({r.x,r.y+i*height,r.w-12,height+0.1F},lines[i+offset].text,size);
        if(maximum){fill({r.x+r.w-4,r.y,3,r.h},{51,69,69,255});fill({r.x+r.w-4,r.y+(r.h-12)*offset/maximum,3,12},{113,191,179,255});}
    };
    SDL_BlendMode previous{};SDL_GetRenderDrawBlendMode(renderer_,&previous);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    fill({40,60,1200,620},{21,29,31,252});fill({64,138,360,400},{27,38,40,255});fill({444,138,772,400},{27,38,40,255});
    text({64,80,900,34},"WORKSHOP & PRODUCTION",22);button(Control::Close,language==UiLanguage::English?"CLOSE":tr("CLOSE WORKSHOP"));
    for(std::size_t i=0;i<6;++i) {
        const auto index=workshopUi_.page*6+i;if(index>=p.recipes.size())break;const auto &row=p.recipes[index];
        const auto box=uiWorkshopRecipeRect(i);
        renderFoundationButton(6000+static_cast<int>(i),box,"",!p.order,false,p.selection==row.id,row.fullName,14);
        renderBaseSupplyImage(row.output,{box.x+8,box.y+5,52,38});
        text({box.x+72,box.y+5,244,38},fmt::format("{}  x{}",row.shortName,row.quantity),14);
    }
    if(uiWorkshopPageCount(p.recipes.size())>1){button(Control::Previous,"PREVIOUS");button(Control::Next,"NEXT");text({80,118,328,20},fmt::format("{} / {}",workshopUi_.page+1,uiWorkshopPageCount(p.recipes.size())),12);}
    const auto row=std::find_if(p.recipes.begin(),p.recipes.end(),[&](const auto &r){return p.selection==r.id;});
    if(row!=p.recipes.end()) {
        renderBaseSupplyImage(row->output,{464,154,172,66});
        text({656,158,536,56},fmt::format("{}  x{}",row->fullName,row->quantity),16);
    }
    const auto status=!p.installed?"WORKSHOP PAUSED IN RESERVE":p.order?p.order->outputReady?"PRODUCTION OUTPUT WAITING":"PRODUCTION ACTIVE":"PRODUCTION PREPARATION";
    text({464,232,728,28},status,16);
    auto time=p.order?p.order->outputReady?tr("OUTPUT READY | WORKER RELEASED"):fmt::format("{} {}",tr("WORLD TIME REMAINING"),duration(p.production.remainingMinutes)):
        fmt::format("{} {}",tr("PRODUCTION DURATION"),p.start.canCommit?duration(p.start.durationMinutes):tr("AVAILABLE WITH VALID PLAN"));
    if((!p.order || !p.order->outputReady) && row!=p.recipes.end())
        time+=fmt::format("  |  {} {}",tr(p.order?"WORKERS COMMITTED":"WORKERS REQUIRED"),p.order?p.production.committedWorkers:row->workers);
    text({464,270,728,28},time);
    if(workshopUi_.facilities) {
        button(Control::Staffing,p.workforce.workshopWorker?"CLEAR WORKSHOP WORKER":"ASSIGN BEST WORKER");
        button(Control::Upgrade,p.cancelUpgrade?"CANCEL WORKSHOP UPGRADE":p.upgradeComplete?"WORKSHOP LEVEL 2 COMPLETE":"UPGRADE WORKSHOP TO LEVEL 2");
        std::string info=fmt::format("{} {} · {} {}",tr("WORKSHOP LEVEL"),p.level,tr("ASSIGNED"),p.workforce.workshopWorker?tr(baseResidentProfessionName(*p.workforce.workshopWorker)):tr("EMPTY"));
        if(p.activeUpgrade)info+=fmt::format("\n{} {}",tr("WORKSHOP UPGRADE IN PROGRESS"),duration(p.activeUpgrade->completionWorldMinute>p.worldMinute?p.activeUpgrade->completionWorldMinute-p.worldMinute:0));
        if(!p.staffing.canCommit)info+="\n"+tr(p.staffing.message);
        if(p.upgradeId && !p.upgrade.canCommit && !p.upgradeComplete)info+="\n"+tr(p.upgrade.message);
        scroll({464,392,728,112},info,14,workshopUi_.detailScroll,workshopUi_.detailMaximum);
    } else {
        text({464,308,728,26},p.order?"RESERVED MATERIALS":"REQUIRED INPUTS");
        scroll(uiWorkshopDetailsRect(),p.order && p.order->outputReady?tr("MATERIALS CONSUMED - OUTPUT IS KEPT"):
            row!=p.recipes.end()?row->materials:tr("NO PUBLISHED MANUFACTURING RECIPE"),14,workshopUi_.detailScroll,workshopUi_.detailMaximum);
    }
    text({464,510,728,22},!p.installed?"INSTALL IT FREE ON THE REGIONAL PAGE TO RESUME EXACT PROGRESS":p.order?"AUTO STASH ON COMPLETION - COLLECT ONLY WHEN FULL":"RESERVE ON START - COMPLETE USING WORLD TIME",12);
    button(Control::Facilities,workshopUi_.facilities?"HIDE FACILITY AND STAFF":"FACILITY AND STAFF",workshopUi_.facilities);
    if(p.order && !p.order->outputReady)button(Control::Cancel,"CANCEL & RETURN INPUTS");
    button(Control::Main,p.order?p.order->outputReady?"COLLECT TO STASH":"PRODUCTION ACTIVE":"START PRODUCTION");
    std::string message=workshopFeedback_;
    if(message.empty())message=!p.installed?"WORKSHOP IS PACKED | PRODUCTION AND UPGRADES ARE PAUSED":
        p.order?!p.returns.canCommit?p.returns.message:"":p.selection?!p.start.canCommit?p.start.message:"":"NO PUBLISHED MANUFACTURING RECIPE";
    scroll(uiWorkshopFeedbackRect(),message,14,workshopUi_.feedbackScroll,workshopUi_.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}
