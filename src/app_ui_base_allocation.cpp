#include "app.h"

#include <algorithm>
#include <fmt/format.h>

namespace {
using C=UiAllocationControl;
using T=UiAllocationTab;
using K=UiAllocationEventKind;
constexpr std::array categories{BaseSupplyCategory::Food,BaseSupplyCategory::Medical,BaseSupplyCategory::Recreation,BaseSupplyCategory::Security};
const char *categoryName(BaseSupplyCategory category) {
    switch(category) {
    case BaseSupplyCategory::Food:return "FOOD";
    case BaseSupplyCategory::Medical:return "MEDICAL";
    case BaseSupplyCategory::Recreation:return "RECREATION";
    case BaseSupplyCategory::Security:return "SECURITY";
    }
    return "UNKNOWN";
}
const char *tierName(BaseOperationalTier tier) {
    switch(tier) {
    case BaseOperationalTier::Critical:return "CRITICAL";
    case BaseOperationalTier::Strained:return "STRAINED";
    case BaseOperationalTier::Stable:return "STABLE";
    case BaseOperationalTier::Supported:return "SUPPORTED";
    }
    return "UNKNOWN";
}
}

void App::resetBaseAllocationUi() {
    allocationUi_={}; allocationProjection_={}; allocationProjectionRevision_.reset();
    allocationViewKey_.clear(); allocationFeedback_.clear(); pendingAllocationEvents_.clear();
}
void App::refreshBaseAllocationUi(bool force) {
    const auto &profile=gameSession_.profile();
    const auto language=uiTextRenderer_.language();
    if(allocationProjectionRevision_ && allocationProjection_.profileId!=profile.profileId)resetBaseAllocationUi();
    if(allocationProjectionRevision_ && allocationProjection_.language!=language) {
        allocationUi_.press.reset(); allocationUi_.detailsScroll=allocationUi_.feedbackScroll=0; allocationFeedback_.clear();
    }
    if(!force && allocationProjectionRevision_ && *allocationProjectionRevision_==profile.revision &&
        allocationProjection_.worldMinute==profile.worldClock.elapsedWorldMinutes && allocationProjection_.language==language &&
        allocationViewKey_==uiAllocationViewKey(allocationUi_))return;
    allocationProjection_=projectUiAllocation(profile,publishedContentRegistry(),language,allocationUi_);
    auto &p=allocationProjection_;
    if(p.selectionExpired || p.materialExpired) {
        allocationFeedback_=localizeUiText(language,"ALLOCATION ITEMS CHANGED - SELECT AGAIN");
        allocationUi_.feedbackScroll=0;
    }
    allocationUi_.wish=p.selectedWish;
    allocationUi_.contributions=p.chosen;
    allocationUi_.material=p.material;
    allocationUi_.supplySelection=p.supplySelection;
    if(!p.focus)allocationUi_.focusOnly=false;
    auto &page=allocationUi_.pages[static_cast<int>(allocationUi_.tab)];
    page=std::min(page,uiAllocationPageCount(p.rows.size(),allocationUi_.tab)-1);
    allocationViewKey_=uiAllocationViewKey(allocationUi_);
    allocationProjectionRevision_=profile.revision; ++allocationProjectionGeneration_;
}
void App::handleBaseAllocationEvent(const UiAllocationEvent &event) {
    if(gameFlow_.state()!=GameFlowState::Base || gameFlow_.activeBaseFacility()!=BaseFacilityKind::Allocation ||
        pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ || baseSiegeWarningVisible() ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) {
        allocationUi_.press.reset(); return;
    }
    refreshBaseAllocationUi(event.kind==K::Down || event.kind==K::Up);
    const auto tr=[&](std::string_view s){return localizeUiText(uiTextRenderer_.language(),s);};
    const auto feedback=[&](std::string value){ allocationFeedback_=std::move(value); allocationUi_.feedbackScroll=0; uiMessage_=allocationFeedback_; };
    auto &s=allocationUi_; const auto &p=allocationProjection_;
    if(event.kind==K::Wheel) {
        s.press.reset();
        if(uiContains(uiAllocationFeedbackRect(),event.position))s.feedbackScroll=std::clamp(s.feedbackScroll+event.delta*2,0,s.feedbackMaximum);
        else if(uiContains(uiAllocationDetailsRect(s.tab),event.position))s.detailsScroll=std::clamp(s.detailsScroll+event.delta*2,0,s.detailsMaximum);
        else if(s.tab!=T::Overview && uiContains(uiAllocationListRect(s.tab),event.position)) {
            auto &page=s.pages[static_cast<int>(s.tab)];
            const auto count=uiAllocationPageCount(p.rows.size(),s.tab);
            page=static_cast<std::size_t>(std::clamp(static_cast<int>(page)+event.delta,0,static_cast<int>(count)-1));
        }
        return;
    }
    const auto hit=hitUiAllocation(event.position,s,p);
    if(event.kind==K::Move) {
        if(s.press && hit!=s.press->hit)s.press.reset(); return;
    }
    if(event.kind==K::Down) {
        s.press.reset();
        if(hit)s.press=UiAllocationPress{*hit,uiTextRenderer_.language(),uiAllocationActionKey(*hit,s,p),uiAllocationSubjects(*hit,p)};
        return;
    }
    const auto press=std::move(s.press); s.press.reset();
    if(!press || !hit || *hit!=press->hit || press->language!=uiTextRenderer_.language())return;
    if(press->action!=uiAllocationActionKey(*hit,s,p) || press->subjects!=uiAllocationSubjects(*hit,p)) {
        feedback(tr("ALLOCATION PLAN CHANGED - CHECK AND CLICK AGAIN")); return;
    }
    const auto control=hit->control;
    if(control==C::Close) {
        gameFlow_.closeBaseFacility(); resetBaseAllocationUi(); uiMessage_.clear(); input_.suppressPrimaryPointerUntilRelease(); return;
    }
    if(control>=C::Overview && control<=C::Materials) {
        const auto next=static_cast<T>(control);
        if(s.tab!=next) {
            s.tab=next; s.contributions.clear(); s.material.reset(); s.supplySelection.reset();
            s.detailsScroll=0; allocationFeedback_.clear(); refreshBaseAllocationUi(true);
        }
        return;
    }
    if(control==C::Details) {s.details=!s.details; s.detailsScroll=0; return;}
    if(control>=C::Food && control<=C::Security) {
        const auto next=categories[static_cast<int>(control)-static_cast<int>(C::Food)];
        if(s.category!=next) {s.category=next; s.pages[static_cast<int>(s.tab)]=0; s.supplySelection.reset(); allocationFeedback_.clear(); refreshBaseAllocationUi(true);}
        return;
    }
    if(control==C::Previous || control==C::Next) {
        auto &page=s.pages[static_cast<int>(s.tab)]; const auto count=uiAllocationPageCount(p.rows.size(),s.tab);
        if(control==C::Previous && page)--page; else if(control==C::Next && page+1<count)++page;
        return;
    }
    if(control==C::Wish) {
        s.wish=hit->wish; s.contributions.clear(); s.focusOnly=false; s.pages[static_cast<int>(s.tab)]=0;
        s.detailsScroll=0; allocationFeedback_.clear(); refreshBaseAllocationUi(true); return;
    }
    if(control==C::Row) {
        const auto row=std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return r.definitionId==hit->definitionId && r.instanceId==hit->assetId;});
        if(row==p.rows.end())return;
        if(s.tab==T::Supply)s.supplySelection=row->definitionId;
        else if(s.tab==T::Materials && !row->assets.empty())s.material=row->assets.front();
        else if(s.tab==T::Wishes) {
            const bool all=std::all_of(row->assets.begin(),row->assets.end(),[&](const auto &a){return std::any_of(s.contributions.begin(),s.contributions.end(),[&](const auto &chosen){return a.id==chosen.id;});});
            std::erase_if(s.contributions,[&](const auto &a){return a.definitionId==row->definitionId;});
            if(!all)s.contributions.insert(s.contributions.end(),row->assets.begin(),row->assets.end());
        }
        s.detailsScroll=0; allocationFeedback_.clear(); refreshBaseAllocationUi(true); return;
    }
    if(!uiAllocationEnabled(control,s,p)) {
        feedback(tr(uiAllocationUnavailable(control,s,p))); gameAudio_.play(SoundEventId::UiDeny); return;
    }
    if(control==C::Filter) {
        s.focusOnly=!s.focusOnly;
        if(s.focusOnly)s.wish=p.focus;
        s.contributions.clear(); s.pages[static_cast<int>(s.tab)]=0; s.detailsScroll=0;
        allocationFeedback_.clear(); refreshBaseAllocationUi(true); return;
    }
    bool succeeded=false;
    if(control==C::Focus || control==C::ClearFocus) {
        const auto target=control==C::Focus?p.selectedWish:std::nullopt;
        const auto receipt=gameSession_.executeBaseWishFocus(target,nextProfileTransactionId("allocation-wish-focus"));
        succeeded=receipt.succeeded; feedback(tr(succeeded?"WISH FOCUS SAVED":receipt.message));
        if(succeeded)s.focusOnly=false;
    } else if(control==C::Main && s.tab==T::Supply && p.supplySelection) {
        const auto receipt=gameSession_.executeBaseSupplyAssignment(*p.supplySelection,p.targetAssignment,nextProfileTransactionId("allocation-supply-policy"));
        succeeded=receipt.succeeded;
        feedback(tr(succeeded?(p.targetAssignment?"AUTOMATIC BASE SUPPLY ENABLED":"AUTOMATIC BASE SUPPLY DISABLED"):receipt.message));
    } else if(control==C::Main && s.tab==T::Wishes && p.selectedWish) {
        std::vector<AssetInstanceId> ids; for(const auto &a:p.chosen)ids.push_back(a.id);
        const auto receipt=gameSession_.executeBasePrioritySubmission(p.selectedWish->definitionId,ids,nextProfileTransactionId("allocation-submit-wish"));
        succeeded=receipt.succeeded; feedback(tr(succeeded?"BASE WISH FULFILLED":receipt.message));
        if(succeeded)s.contributions.clear();
    } else if(control==C::Main && s.tab==T::Materials && p.material) {
        const auto receipt=gameSession_.executeConstructionMaterialContribution(p.material->id,nextProfileTransactionId("allocation-process-material"));
        succeeded=receipt.succeeded;
        feedback(succeeded?fmt::format("{} +{}",tr("BUILD MATERIAL GAIN"),receipt.materialUnits):tr(receipt.message));
        if(succeeded)s.material.reset();
    } else return;
    gameAudio_.play(succeeded?SoundEventId::UiConfirm:SoundEventId::UiDeny);
    refreshBaseAllocationUi(true);
}

void App::renderBaseAllocation() {
    refreshBaseAllocationUi();
    const auto &p=allocationProjection_; auto &s=allocationUi_;
    const auto language=uiTextRenderer_.language();
    const auto tr=[&](std::string_view value){return localizeUiText(language,value);};
    const auto fill=[&](SDL_FRect r,SDL_Color color){SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);SDL_RenderFillRect(renderer_,&r);};
    const auto text=[&](SDL_FRect r,std::string_view value,int size=14){SDL_SetRenderDrawColor(renderer_,222,231,226,255);uiTextRenderer_.renderBox(renderer_,r,value,{size,UiTextAlign::Left,0});};
    const auto button=[&](C c,std::string_view label,bool selected=false){renderFoundationButton(7100+static_cast<int>(c),uiAllocationControlRect(c),label,uiAllocationEnabled(c,s,p),false,selected,{},14);};
    const auto scroll=[&](SDL_FRect r,std::string_view value,int &offset,int &maximum){
        const auto lines=wrapUiText(tr(value),r.w-14,0,[&](auto v){return uiTextRenderer_.measure(v,14);});
        constexpr float lineHeight=21;
        const int visible=std::max(1,static_cast<int>(r.h/lineHeight)); maximum=std::max(0,static_cast<int>(lines.size())-visible); offset=std::clamp(offset,0,maximum);
        for(int i=0;i<visible && i+offset<static_cast<int>(lines.size());++i)text({r.x,r.y+i*lineHeight,r.w-14,lineHeight},lines[i+offset].text);
        if(maximum) {fill({r.x+r.w-4,r.y,3,r.h},{51,69,69,255});fill({r.x+r.w-4,r.y+(r.h-12)*offset/maximum,3,12},{113,191,179,255});}
    };
    const auto duration=[&](std::uint64_t minutes){
        if(minutes>=kWorldMinutesPerDay)return fmt::format("{}{} {}{}",minutes/kWorldMinutesPerDay,language==UiLanguage::English?"d":"天",minutes%kWorldMinutesPerDay/60,language==UiLanguage::English?"h":"小时");
        return fmt::format("{}{} {}{}",minutes/60,language==UiLanguage::English?"h":"小时",minutes%60,language==UiLanguage::English?"m":"分");
    };
    SDL_BlendMode previous{};SDL_GetRenderDrawBlendMode(renderer_,&previous);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    fill({40,60,1200,620},{21,29,31,252}); text({64,80,1000,34},"ALLOCATION & NEEDS",22);
    button(C::Close,language==UiLanguage::English?"CLOSE":tr("CLOSE ALLOCATION"));
    constexpr std::array labels{"ALLOCATION OVERVIEW","AUTO SUPPLY","BASE WISHES","BUILD MATERIALS"};
    for(int i=0;i<4;++i)button(static_cast<C>(i),labels[i],static_cast<int>(s.tab)==i);
    if(s.tab==T::Overview) {
        constexpr std::array names{"RATION STOCK","HYGIENE","OPERATIONS SUPPORT","SECURITY"};
        const std::array stocks{p.readiness.pool.food,p.readiness.pool.hygiene,p.readiness.pool.morale,p.readiness.pool.security};
        const std::array demands{p.demand.food,p.demand.hygiene,p.demand.morale,p.demand.security};
        const std::array authorized{p.readiness.authorizedContribution.food,p.readiness.authorizedContribution.hygiene,p.readiness.authorizedContribution.morale,p.readiness.authorizedContribution.security};
        const std::array gaps{p.readiness.projectedShortfall.food,p.readiness.projectedShortfall.hygiene,p.readiness.projectedShortfall.morale,p.readiness.projectedShortfall.security};
        const std::array last{p.lastShortfall.food,p.lastShortfall.hygiene,p.lastShortfall.morale,p.lastShortfall.security};
        for(int i=0;i<4;++i) {
            const float x=64+292.0F*i; fill({x,186,276,184},{27,38,40,255});
            text({x+14,196,248,22},names[i]); text({x+14,222,248,36},fmt::format("{} / {}",stocks[i],kMaximumBaseResource),24);
            text({x+14,262,248,18},fmt::format("{}  {}",tr("DAILY DEMAND"),demands[i]),12);
            text({x+14,282,248,18},fmt::format("{}  {}",tr("AUTHORIZED SUPPLY"),authorized[i]),12);
            text({x+14,302,248,18},fmt::format("{}  {}",tr("ESTIMATED GAP"),gaps[i]),12);
            text({x+14,322,248,18},fmt::format("{}  {}",tr("LAST DAILY GAP"),last[i]),12);
            text({x+14,346,248,18},tierName(p.tiers[i]),12);
        }
        fill({64,386,1152,150},{27,38,40,255});
        const auto morale=language==UiLanguage::English?std::string(baseMoraleTierName(p.morale.tier)):
            tr(p.morale.tier==BaseMoraleTier::Low?"ALLOCATION MORALE LOW":p.morale.tier==BaseMoraleTier::High?"ALLOCATION MORALE HIGH":"ALLOCATION MORALE STABLE");
        const auto trend=language==UiLanguage::English?std::string(baseMoraleTrendName(p.morale.trend)):
            tr(p.morale.trend==BaseMoraleTrend::Falling?"ALLOCATION TREND FALLING":p.morale.trend==BaseMoraleTrend::Rising?"ALLOCATION TREND RISING":"ALLOCATION TREND STEADY");
        text({84,398,780,24},fmt::format("{}  {} · {}",tr("RESIDENT MORALE"),morale,trend),16);
        text({856,398,340,24},fmt::format("{} {}",tr("NEXT DAILY SETTLEMENT"),duration(p.clock.minutesUntilNextDay)),12);
        std::string details=tr("ESTIMATE USES CURRENT STOCK AND AUTHORIZATION ONLY");
        if(s.details) {
            const auto &ledger=p.morale.lastLedger;
            details=fmt::format("{} {} · {} {} · {} {}\n{} {} / {} · {} +{} / -{}\n{} {} ({:+}) · {} {}\n{} {}%\n{}",
                tr("LAST DAILY SCORE"),ledger.netScore,tr("BED SHORTFALL"),ledger.bedShortfall,tr("LOW MORALE DAYS"),p.morale.consecutiveLowDays,
                tr("FULFILLED / MISSED WISHES"),ledger.fulfilledWishCount,ledger.missedWishCount,tr("EVENT EFFECTS"),ledger.positiveEventCount,ledger.negativeEventCount,
                tr("CURRENT COMMUNITY EVENT"),tr(p.eventName),p.eventEffect,tr("NEXT EVENT IN DAYS"),p.eventDays,
                tr("PRODUCTION DURATION"),p.productionPercent,tr("ESTIMATE USES CURRENT STOCK AND AUTHORIZATION ONLY"));
        }
        scroll(uiAllocationDetailsRect(s.tab),details,s.detailsScroll,s.detailsMaximum);
        button(C::Details,s.details?"HIDE DAILY DETAILS":"DAILY DETAILS",s.details);
    } else {
        if(s.tab==T::Supply)for(int i=0;i<4;++i)button(static_cast<C>(static_cast<int>(C::Food)+i),categoryName(categories[i]),s.category==categories[i]);
        if(s.tab==T::Wishes) {
            for(std::size_t i=0;i<p.wishes.size();++i) {
                const auto &wish=p.wishes[i]; const bool chosen=p.selectedWish && p.selectedWish->definitionId==wish.definitionId;
                renderFoundationButton(7200+static_cast<int>(i),uiAllocationWishRect(i),"",true,false,chosen,{},14);
                text({80,191+42.0F*i,386,26},tr(wish.displayName));
                text({470,191+42.0F*i,124,26},wish.fulfilled?"FULFILLED":p.focus && p.focus->definitionId==wish.definitionId?"WISH FOCUSED":"ACTIVE",12);
            }
        } else if(s.tab==T::Materials)text({80,192,516,24},"CHOOSE ONE MATERIAL STACK",16);
        fill(uiAllocationListRect(s.tab),{27,38,40,255});
        const auto first=s.pages[static_cast<int>(s.tab)]*uiAllocationPageSize(s.tab);
        for(std::size_t i=0;i<uiAllocationPageSize(s.tab) && first+i<p.rows.size();++i) {
            const auto &row=p.rows[first+i]; const auto box=uiAllocationRowRect(s.tab,i);
            const bool chosen=s.tab==T::Supply?p.supplySelection==row.definitionId:s.tab==T::Materials?p.material && p.material->id==row.instanceId:row.selectedQuantity>0;
            renderFoundationButton(7300+static_cast<int>(i),box,"",true,false,chosen,{},14);
            renderBaseSupplyImage(row.definitionId,{box.x+4,box.y+3,42,box.h-6});
            if(s.tab==T::Wishes) {
                text({box.x+58,box.y+5,300,24},fmt::format("{}  x{}",row.name,row.quantity),12);
                text({box.x+364,box.y+5,140,24},fmt::format("{} {}",tr("SELECTED UNITS"),row.selectedQuantity),12);
            } else {
                text({box.x+58,box.y+1,box.w-166,20},fmt::format("{}  x{}",row.name,row.quantity));
                if(chosen)text({box.x+382,box.y+1,122,20},"SELECTED",12);
                const auto subtitle=s.tab==T::Supply?fmt::format("{} +{} · {}",tr("EACH"),row.unit,row.assignment?tr(categoryName(*row.assignment)):tr("AUTO USE OFF")):row.source;
                text({box.x+58,box.y+22,box.w-70,18},subtitle,12);
            }
        }
        if(p.rows.empty())text({80,uiAllocationListRect(s.tab).y+12,516,76},s.tab==T::Supply?"NO SUPPLY TYPES IN THIS CATEGORY":s.tab==T::Wishes?"NO MATCHING WISH ITEMS":"NO ACCESSIBLE BUILD MATERIALS");
        button(C::Previous,"PREV.");button(C::Next,"NEXT");
        text({232,516,212,22},fmt::format("{} / {}",s.pages[static_cast<int>(s.tab)]+1,uiAllocationPageCount(p.rows.size(),s.tab)),12);
        fill({632,186,584,356},{27,38,40,255});
        std::string details;
        if(s.tab==T::Supply) {
            const auto row=std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return p.supplySelection && r.definitionId==*p.supplySelection;});
            if(row!=p.rows.end()) {
                renderBaseSupplyImage(row->definitionId,{652,202,128,72});text({796,206,396,30},row->name,18);
                text({796,242,396,24},fmt::format("{} {} · {} +{}",tr("AVAILABLE"),row->quantity,tr("EACH"),row->unit));
                details=fmt::format("{}: {}\n\n{}\n{}",tr("AUTO USE CATEGORY"),row->assignment?tr(categoryName(*row->assignment)):tr("AUTO USE OFF"),
                    tr("TYPE POLICY - ITEMS STAY PUT UNTIL DAILY USE"),tr("FUTURE ITEMS OF THIS TYPE FOLLOW THE SAME POLICY"));
            } else text({652,206,544,56},"SELECT A SUPPLY TYPE",16);
            button(C::Main,p.currentAssignment==s.category?"STOP AUTO USE":p.currentAssignment?"REASSIGN AUTO USE":"ALLOW AUTO USE");
        } else if(s.tab==T::Wishes) {
            const auto wish=std::find_if(p.wishes.begin(),p.wishes.end(),[&](const auto &w){return p.selectedWish && w.definitionId==p.selectedWish->definitionId;});
            if(wish!=p.wishes.end()) {
                text({652,202,544,32},tr(wish->displayName),18);
                text({652,242,544,26},wish->fulfilled?tr("WISH COMPLETE"):fmt::format("{} {} / {} · {} {}",tr("CONTRIBUTION"),p.selectedContribution,p.requiredContribution,tr("EXCESS"),
                    p.selectedContribution>p.requiredContribution?p.selectedContribution-p.requiredContribution:0));
                details=fmt::format("{} {}\n{} {} · {} {}\n{} {}\n",tr("SOURCE HINT"),tr(wish->sourceHint),tr("WISH CYCLE LEFT"),duration(p.remainingWishMinutes),tr("MISSED TOTAL"),p.missedWishes,tr("POPULATION THIS CYCLE"),p.frozenPopulation);
                if(!wish->fulfilled) {
                    details+="\n"+tr("THIS CONFIRMATION CONSUMES")+"\n";
                    for(const auto &a:p.chosen)details+=fmt::format("{}  x{} · {}\n",a.name,a.quantity,a.source);
                    if(p.chosen.empty())details+=tr("NO ITEMS SELECTED");
                }
            } else text({652,202,544,56},"NO ACTIVE BASE WISHES",16);
            text({652,502,544,32},"WHOLE STACKS - EXCESS IS NOT RETURNED",12);
            button(C::Focus,"FOCUS WISH",p.focus==p.selectedWish && p.focus.has_value());button(C::ClearFocus,"CLEAR FOCUS");button(C::Filter,"FOCUS STOCK",s.focusOnly);
            button(C::Main,"CONFIRM WISH ITEMS");
        } else {
            if(p.material) {
                renderBaseSupplyImage(p.material->definitionId,{652,202,128,72});text({796,206,396,30},p.material->name,18);
                text({796,242,396,24},fmt::format("{} · x{}",p.material->source,p.material->quantity));
                details=fmt::format("{}  +{}\n{}  {} / {}\n\n{}",tr("BUILD MATERIAL GAIN"),p.materialOutput,tr("PUBLIC MATERIAL CAPACITY"),p.materialStock,p.materialMaximum,tr("ONE INSTANCE - ITS WHOLE STACK IS CONSUMED"));
            } else text({652,206,544,56},"SELECT ONE MATERIAL STACK",16);
            button(C::Main,"CONFIRM PROCESSING");
        }
        scroll(uiAllocationDetailsRect(s.tab),details,s.detailsScroll,s.detailsMaximum);
    }
    std::string message=allocationFeedback_;
    if(message.empty() && ((s.tab==T::Supply && p.supplySelection) || (s.tab==T::Wishes && !p.chosen.empty()) ||
        (s.tab==T::Materials && p.material)))message=uiAllocationUnavailable(C::Main,s,p);
    scroll(uiAllocationFeedbackRect(),message,s.feedbackScroll,s.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}
