#include "app.h"
#include "alpha_content_ids.h"
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
        workshopUi_.salvageConfirmation.reset();
        workshopUi_.selectedMaterial.reset();
    }
    const auto &order = p.baseManufacturing.activeOrder;
    if (workshopUi_.selectedMaterial && (!order || order->jobId != workshopUi_.expediteJob ||
        p.revision != workshopUi_.expediteRevision || p.worldClock.elapsedWorldMinutes != workshopUi_.expeditePreviewMinute)) {
        workshopUi_.selectedMaterial.reset();
        workshopFeedback_ = "EXPEDITE PREVIEW CHANGED";
    }
    if(!force && workshopProjectionRevision_ && *workshopProjectionRevision_==p.revision &&
        workshopProjection_.worldMinute==p.worldClock.elapsedWorldMinutes && workshopProjectionLanguage_==language &&
        workshopProjection_.selection==workshopUi_.selection &&
        workshopProjection_.salvageOpen == workshopUi_.salvageOpen &&
        workshopProjection_.selectedGun == workshopUi_.selectedGun &&
        workshopProjection_.expediteOpen == workshopUi_.expediteOpen &&
        workshopProjection_.selectedMaterial == workshopUi_.selectedMaterial &&
        workshopProjection_.trainingSelected==workshopUi_.trainingSelected)return;
    workshopProjection_=projectUiWorkshop(p,publishedContentRegistry(),language,workshopUi_.selection,
        workshopUi_.trainingSelected, workshopUi_.salvageOpen, workshopUi_.selectedGun,
        workshopUi_.expediteOpen, workshopUi_.selectedMaterial);
    if (workshopUi_.expediteOpen) {
        workshopUi_.selectedMaterial = workshopProjection_.selectedMaterial;
        workshopUi_.expeditePage = std::min(workshopUi_.expeditePage, uiWorkshopPageCount(workshopProjection_.materials.size())-1);
    } else if (workshopUi_.salvageOpen) {
        workshopUi_.selectedGun = workshopProjection_.selectedGun;
        workshopUi_.salvagePage = std::min(workshopUi_.salvagePage, uiWorkshopPageCount(workshopProjection_.guns.size())-1);
        if (workshopUi_.salvageConfirmation &&
            *workshopUi_.salvageConfirmation != uiWorkshopActionKey({{}, Control::ConfirmSalvage}, workshopProjection_)) {
            workshopUi_.salvageConfirmation.reset();
            workshopFeedback_ = "SALVAGE PREVIEW CHANGED";
        }
    } else {
        workshopUi_.selection=workshopProjection_.selection;
        workshopUi_.trainingSelected=workshopProjection_.trainingSelected;
        workshopUi_.page=std::min(workshopUi_.page,uiWorkshopPageCount(workshopProjection_.recipes.size())-1);
        if(workshopProjection_.order) {
            const auto it=std::find_if(workshopProjection_.recipes.begin(),workshopProjection_.recipes.end(),[&](const auto &r){return workshopProjection_.selection==r.id;});
            if(it!=workshopProjection_.recipes.end())workshopUi_.page=static_cast<std::size_t>(it-workshopProjection_.recipes.begin())/6;
        }
    }
    workshopProjectionRevision_=p.revision; workshopProjectionLanguage_=language; ++workshopProjectionGeneration_;
}
void App::handleBaseWorkshopEvent(const UiWorkshopEvent &e) {
    if(profileUiBusy())return;
    if(gameFlow_.state()!=GameFlowState::Base || gameFlow_.activeBaseFacility()!=BaseFacilityKind::Workshop ||
        pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_) {
        workshopUi_.press.reset(); return;
    }
    refreshBaseWorkshopUi(e.kind==Kind::Down || e.kind==Kind::Up);
    const auto changePage=[&](int delta) {
        if (workshopUi_.expediteOpen) {
            auto &page = workshopUi_.expeditePage;
            const auto next = static_cast<std::size_t>(std::clamp(static_cast<int>(page)+delta,0,
                static_cast<int>(uiWorkshopPageCount(workshopProjection_.materials.size())-1)));
            if (next != page) {
                page = next;
                workshopUi_.selectedMaterial.reset();
                workshopFeedback_.clear();
            }
            workshopUi_.press.reset();
            return;
        }
        auto &page = workshopUi_.salvageOpen ? workshopUi_.salvagePage : workshopUi_.page;
        const auto count = workshopUi_.salvageOpen ? workshopProjection_.guns.size() : workshopProjection_.recipes.size();
        const auto next = static_cast<std::size_t>(std::clamp(static_cast<int>(page)+delta, 0,
            static_cast<int>(uiWorkshopPageCount(count)-1)));
        if (next != page && workshopUi_.salvageOpen) {
            workshopUi_.selectedGun.reset();
            workshopUi_.salvageConfirmation.reset();
            workshopFeedback_.clear();
        }
        page = next;
        workshopUi_.press.reset();
    };
    if(e.kind==Kind::Wheel) {
        workshopUi_.press.reset();
        if(uiContains(uiWorkshopFeedbackRect(),e.position)) workshopUi_.feedbackScroll=std::clamp(workshopUi_.feedbackScroll+e.delta*2,0,workshopUi_.feedbackMaximum);
        else if(uiContains(uiWorkshopDetailsRect(),e.position))workshopUi_.detailScroll=std::clamp(workshopUi_.detailScroll+e.delta*2,0,workshopUi_.detailMaximum);
        else if(e.position.x>=64 && e.position.x<=424 && e.position.y>=138 && e.position.y<=538 && (!workshopProjection_.order || workshopUi_.expediteOpen))changePage(e.delta);
        return;
    }
    const auto hit=hitUiWorkshop(e.position,workshopUi_,workshopProjection_);
    if(e.shortcut && (!hit || hit->control!=*e.shortcut))return;
    if(e.kind==Kind::Move) {if(workshopUi_.press && !workshopUi_.press->fromKeyboard && hit!=workshopUi_.press->hit)workshopUi_.press.reset(); return;}
    const auto language=uiTextRenderer_.language();
    if(e.kind==Kind::Down) {
        workshopUi_.press.reset();
        if(hit)workshopUi_.press=UiWorkshopPress{*hit,language,uiWorkshopActionKey(*hit,workshopProjection_),e.shortcut.has_value()};
        return;
    }
    const auto press=workshopUi_.press; workshopUi_.press.reset();
    if(!press || !hit || *hit!=press->hit || press->language!=language || press->fromKeyboard!=e.shortcut.has_value())return;
    const auto feedback=[&](std::string s){workshopFeedback_=std::move(s);uiMessage_=workshopFeedback_;workshopUi_.feedbackScroll=0;};
    if(press->action!=uiWorkshopActionKey(*hit,workshopProjection_)) {feedback("PRODUCTION PLAN CHANGED - CHECK AND CLICK AGAIN");return;}
    if (hit->recipe) {
        workshopUi_.selection = hit->recipe;
        workshopUi_.trainingSelected = false;
        workshopUi_.detailScroll = workshopUi_.feedbackScroll = 0;
        workshopFeedback_.clear();
        refreshBaseWorkshopUi(true);
        return;
    }
    const auto control=hit->control;
    if(control==Control::Close) {gameFlow_.closeBaseFacility();resetBaseWorkshopUi();uiMessage_.clear();input_.suppressPrimaryPointerUntilRelease();return;}
    if (control == Control::Expedite) {
        if (!uiWorkshopEnabled(control, workshopProjection_)) {
            if (workshopProjection_.order) feedback(queryExpediteBaseManufacturing(gameSession_.profile(),
                publishedContentRegistry(),{workshopProjection_.order->jobId,0}).message);
            return;
        }
        workshopUi_.expediteOpen = !workshopUi_.expediteOpen;
        workshopUi_.expeditePage = 0;
        workshopUi_.selectedMaterial.reset();
        workshopFeedback_.clear();
        refreshBaseWorkshopUi(true);
        return;
    }
    if (control == Control::Salvage) {
        workshopUi_.salvageOpen = !workshopUi_.salvageOpen;
        workshopUi_.salvagePage = 0;
        workshopUi_.selectedGun.reset();
        workshopUi_.salvageConfirmation.reset();
        workshopUi_.feedbackScroll = 0;
        workshopFeedback_.clear();
        refreshBaseWorkshopUi(true);
        return;
    }
    if (control == Control::Training) {
        if (uiWorkshopEnabled(control, workshopProjection_)) {
            workshopUi_.trainingSelected = !workshopUi_.trainingSelected;
            workshopUi_.facilities = false;
            workshopUi_.detailScroll = workshopUi_.feedbackScroll = 0;
            workshopFeedback_.clear();
            refreshBaseWorkshopUi(true);
        }
        return;
    }
    if(control==Control::Facilities) {workshopUi_.facilities=!workshopUi_.facilities;workshopUi_.detailScroll=0;return;}
    if(control==Control::Previous || control==Control::Next) {changePage(control==Control::Previous?-1:1);return;}
    const auto &p=workshopProjection_;
    if (p.expediteOpen) {
        if (hit->material && p.order) {
            workshopUi_.selectedMaterial = hit->material;
            workshopUi_.expediteRevision = p.revision;
            workshopUi_.expediteJob = p.order->jobId;
            workshopUi_.expeditePreviewMinute = p.worldMinute;
            workshopFeedback_.clear();
            refreshBaseWorkshopUi(true);
        } else if (control == Control::ConfirmExpedite && p.expedite.canCommit && p.selectedMaterial) {
            const auto command = ProfileUiExpediteManufacturingCommand{workshopUi_.expediteJob,
                *p.selectedMaterial,workshopUi_.expediteRevision};
            workshopUi_.selectedMaterial.reset();
            submitProfileUi<BaseManufacturingReceipt>(command, nextProfileTransactionId("workshop-expedite"),
                [this](const auto &receipt) {
                    if (receipt.succeeded) workshopUi_.expediteOpen = false;
                    workshopFeedback_ = receipt.succeeded ? "MANUFACTURING EXPEDITED" : receipt.message;
                    uiMessage_ = workshopFeedback_;
                    gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
                    refreshBaseWorkshopUi(true);
                });
        }
        return;
    }
    if (p.salvageOpen) {
        if (hit->gun) {
            workshopUi_.selectedGun = hit->gun;
            workshopUi_.salvageConfirmation.reset();
            workshopFeedback_.clear();
            refreshBaseWorkshopUi(true);
        } else if (control == Control::Cancel) {
            workshopUi_.salvageConfirmation.reset();
            workshopFeedback_.clear();
        } else if (p.selectedGun && p.salvage.canCommit && control == Control::PreviewSalvage) {
            workshopUi_.salvageConfirmation = uiWorkshopActionKey({{}, Control::ConfirmSalvage}, p);
            workshopFeedback_.clear();
        } else if (control == Control::ConfirmSalvage && workshopUi_.salvageConfirmation &&
                   *workshopUi_.salvageConfirmation == press->action && p.selectedGun && p.salvage.canCommit) {
            const auto gun = *p.selectedGun;
            workshopUi_.salvageConfirmation.reset();
            submitProfileUi<EconomyReceipt>(EconomyCommand{SalvageWeaponCommand{gun}},
                nextProfileTransactionId("workshop-salvage"), [this](const EconomyReceipt &receipt) {
                    workshopFeedback_ = receipt.succeeded ? "WEAPON SALVAGED · MATERIAL IN STASH" : receipt.message;
                    uiMessage_ = workshopFeedback_;
                    gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
                    refreshBaseWorkshopUi(true);
                });
        }
        return;
    }
    const bool research = p.selection == kArmorRepairResearchId;
    if(!uiWorkshopEnabled(control,p)) {
        if (p.trainingSelected && (control == Control::Main || control == Control::Cancel)) {
            feedback(control == Control::Cancel ? p.trainingCancel.message :
                p.training.orderPresent ? "TRAINING IN PROGRESS" : p.trainingStart.message);
            return;
        }
        feedback(!p.installed?"WORKSHOP IS PACKED | PRODUCTION AND UPGRADES ARE PAUSED":
            control==Control::Staffing?p.staffing.message:control==Control::Upgrade?p.upgrade.message:
            p.order?p.order->outputReady || control==Control::Cancel?p.returns.message:(research?"RESEARCH IN PROGRESS":"PRODUCTION ACTIVE"):p.selection?p.start.message:"NO PUBLISHED MANUFACTURING RECIPE");
        return;
    }
    const auto finish=[this,feedback](bool succeeded,std::string message){
        feedback(std::move(message));gameAudio_.play(succeeded?SoundEventId::UiConfirm:SoundEventId::UiDeny);refreshBaseWorkshopUi(true);
    };
    if (p.trainingSelected && control == Control::Main) {
        submitProfileUi<BaseManufacturingReceipt>(ProfileUiStartEngineeringTrainingCommand{},
            nextProfileTransactionId("workshop-training-start"),
            [finish](const auto &result) {
                finish(result.succeeded, result.succeeded ? "TRAINING STARTED" : result.message);
            });
    } else if (p.trainingSelected && control == Control::Cancel) {
        submitProfileUi<BaseManufacturingReceipt>(ProfileUiCancelEngineeringTrainingCommand{},
            nextProfileTransactionId("workshop-training-cancel"),
            [finish](const auto &result) {
                finish(result.succeeded, result.succeeded ? "TRAINING CANCELLED | MATERIALS RETURNED" : result.message);
            });
    } else if(control==Control::Main && p.selection && !p.order) {
        submitProfileUi<BaseManufacturingReceipt>(StartBaseManufacturingCommand{*p.selection},nextProfileTransactionId("workshop-start"),
            [finish,research](const auto &r){finish(r.succeeded,r.succeeded?(research?"RESEARCH STARTED":"MANUFACTURING STARTED"):r.message);});
    } else if(control==Control::Main) {
        submitProfileUi<BaseManufacturingReceipt>(ProfileUiCollectManufacturingCommand{},nextProfileTransactionId("workshop-collect"),
            [finish](const auto &r){finish(r.succeeded,r.succeeded?"MANUFACTURED ITEM COLLECTED":r.message);});
    } else if(control==Control::Cancel) {
        submitProfileUi<BaseManufacturingReceipt>(ProfileUiCancelManufacturingCommand{},nextProfileTransactionId("workshop-cancel"),
            [finish,research](const auto &r){finish(r.succeeded,r.succeeded?(research?"RESEARCH CANCELLED | BLUEPRINT RETURNED":"MANUFACTURING CANCELLED | INPUTS RETURNED"):r.message);});
    } else if(control==Control::Staffing) {
        const bool assigned=p.workforce.workshopWorker.has_value();
        submitProfileUi<BaseWorkforceReceipt>(assigned?ProfileUiCommand{ProfileUiClearWorkerCommand{BaseFacilityStaffingKind::Workshop}}:
            ProfileUiCommand{ProfileUiAssignWorkerCommand{BaseFacilityStaffingKind::Workshop}},nextProfileTransactionId("workshop-worker"),
            [finish,assigned](const auto &r){finish(r.succeeded,r.succeeded?(assigned?"WORKSHOP WORKER CLEARED":"WORKSHOP WORKER ASSIGNED"):r.message);});
    } else if(control==Control::Upgrade && p.upgradeId) {
        const bool cancel=p.cancelUpgrade;
        submitProfileUi<BaseConstructionReceipt>(cancel?ProfileUiCommand{CancelBaseConstructionCommand{*p.upgradeId}}:
            ProfileUiCommand{StartBaseConstructionCommand{*p.upgradeId}},nextProfileTransactionId("workshop-upgrade"),
            [finish,cancel](const auto &r){finish(r.succeeded,r.succeeded?(cancel?"WORKSHOP UPGRADE CANCELLED":"WORKSHOP UPGRADE STARTED"):r.message);});
    }

}
void App::renderBaseWorkshop() {
    refreshBaseWorkshopUi(); const auto &p=workshopProjection_;const auto language=uiTextRenderer_.language();
    const auto tr=[&](std::string_view s){return localizeUiText(language,s);};
    const auto fill=[&](SDL_FRect r,SDL_Color c){SDL_SetRenderDrawColor(renderer_,c.r,c.g,c.b,c.a);SDL_RenderFillRect(renderer_,&r);};
    const auto text=[&](SDL_FRect r,std::string_view s,int size=14){SDL_SetRenderDrawColor(renderer_,222,231,226,255);uiTextRenderer_.renderBox(renderer_,r,s,{size,UiTextAlign::Left,0});};
    const auto button=[&](Control c,std::string_view label,bool selected=false) {
        const bool warning = p.salvageOpen ? c == Control::ConfirmSalvage :
            c == Control::Cancel || (c == Control::Upgrade && p.cancelUpgrade);
        renderFoundationButton(5900+static_cast<int>(c),uiWorkshopControlRect(c),label,
                               uiWorkshopEnabled(c,p),warning,selected,{},14);
    };
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
    text({64,80,360,34},p.expediteOpen?"EXPEDITE ORDER":p.salvageOpen?"WEAPON SALVAGE":"WORKSHOP & PRODUCTION",22);
    button(Control::Close,language==UiLanguage::English?"CLOSE":tr("CLOSE WORKSHOP"));
    if (p.expediteOpen || (p.order && !p.trainingSelected && !p.salvageOpen && p.order->recipeDefinitionId!=kArmorRepairResearchId)) {
        const auto label = p.expediteOpen ? "BACK TO PRODUCTION" : p.order->expediteMinutesSaved ? "ORDER EXPEDITED" : "EXPEDITE ORDER";
        button(Control::Expedite,fmt::format("X  {}",tr(label)),p.expediteOpen);
    }
    if (p.expediteOpen) {
        const auto pages = uiWorkshopPageCount(p.materials.size());
        text({80,117,328,21},fmt::format("{}  ·  {} / {}",tr("STASH ELECTRONICS"),workshopUi_.expeditePage+1,pages),12);
        const auto &def = publishedContentRegistry().item(alpha_content::lootElectronics);
        const auto name = tr(def.displayName);
        for (std::size_t i=0; i<kUiWorkshopPageSize; ++i) {
            const auto n = workshopUi_.expeditePage*kUiWorkshopPageSize+i;
            if (n>=p.materials.size()) break;
            const auto &m = p.materials[n];
            const auto row = uiWorkshopRecipeRect(i);
            renderFoundationButton(6200+static_cast<int>(i),row,"",true,false,m.id==p.selectedMaterial,{},14);
            renderBaseSupplyImage(alpha_content::lootElectronics,{row.x+8,row.y+6,46,36});
            text({row.x+64,row.y+4,250,22},fmt::format("{}  {}  ×{}",i+1,name,m.quantity));
            text({row.x+64,row.y+27,250,18},fmt::format("#{}",m.id),12);
        }
        if (p.materials.empty()) text({80,159,328,62},"SELECT STASH ELECTRONICS");
        if (pages>1) {
            button(Control::Previous,"PREVIOUS PAGE");
            button(Control::Next,"NEXT");
        }
        const auto nameOfOrder = !p.order ? tr("MANUFACTURING ORDER CHANGED") :
            p.order->recipeDefinitionId == kArmorRepairResearchId ? tr("ARMOR REPAIR RESEARCH") :
            tr(publishedContentRegistry().baseManufacturingRecipe(p.order->recipeDefinitionId).displayName);
        text({464,160,728,36},nameOfOrder,20);
        text({464,226,728,32},fmt::format("{}  {} ×1{}",tr("EXPEDITE COST"),name,
            p.selectedMaterial?fmt::format("  #{}",*p.selectedMaterial):""),16);
        if (p.order && p.expedite.newRemainingMinutes) {
            const auto clock = [&](std::uint64_t minute) {
                const auto time = projectWorldClock({minute});
                return fmt::format("{} {:02}:{:02}",fmt::format(fmt::runtime(tr("DAY {}")),time.day),time.hour,time.minute);
            };
            text({464,294,728,32},fmt::format("{}  {} → {}",tr("WORK REMAINING"),duration(p.expedite.remainingMinutes),
                duration(p.expedite.newRemainingMinutes)),16);
            text({464,345,728,32},fmt::format("{}  {} → {}",tr("ESTIMATED COMPLETION"),
                clock(p.expedite.estimatedCompletionBefore),clock(p.expedite.estimatedCompletionAfter)),16);
        }
        text({464,433,728,58},p.expedite.canCommit?"EXPEDITE CONSUMES THE SELECTED UNIT":p.expedite.message,14);
        button(Control::ConfirmExpedite,fmt::format("Enter  {}",tr("CONFIRM EXPEDITE")));
        scroll(uiWorkshopFeedbackRect(),workshopFeedback_,14,workshopUi_.feedbackScroll,workshopUi_.feedbackMaximum);
        SDL_SetRenderDrawBlendMode(renderer_,previous);
        return;
    }
    button(Control::Salvage, p.salvageOpen ? "BACK TO PRODUCTION" : "WEAPON SALVAGE", p.salvageOpen);
    if (p.salvageOpen) {
        text({80,118,328,20}, fmt::format("{}  ·  {} / {}", tr("STASH WEAPONS"), workshopUi_.salvagePage+1,
            uiWorkshopPageCount(p.guns.size())), 12);
        for (std::size_t i = 0; i < kUiWorkshopPageSize; ++i) {
            const auto index = workshopUi_.salvagePage * kUiWorkshopPageSize + i;
            if (index >= p.guns.size()) break;
            const auto &gun = p.guns[index];
            const auto box = uiWorkshopRecipeRect(i);
            renderFoundationButton(6100+static_cast<int>(i), box, "", true, false, gun.id == p.selectedGun, gun.name, 14);
            renderBaseSupplyImage(gun.definition, {box.x+8, box.y+5, 52, 38});
            text({box.x+72, box.y+5, 250, 20}, fmt::format("{}  #{}", gun.name, gun.id), 14);
            text({box.x+72, box.y+25, 250, 20}, fmt::format("{}  {:.0f} / {:.0f}", tr("DURABILITY"),
                 gun.durability/100.0F, gun.maximumDurability/100.0F), 12);
        }
        if (p.guns.empty()) text({80,158,328,65}, "NO WEAPONS IN STASH");
        if (uiWorkshopPageCount(p.guns.size()) > 1) {
            button(Control::Previous, "PREVIOUS PAGE");
            button(Control::Next, "NEXT");
        }
        const auto selected = std::find_if(p.guns.begin(), p.guns.end(), [&](const auto &gun) { return gun.id == p.selectedGun; });
        if (selected != p.guns.end()) {
            renderBaseSupplyImage(selected->definition, {464,154,172,66});
            text({656,158,536,56}, fmt::format("{}  #{}", selected->name, selected->id), 18);
            text({464,240,728,36}, "SALVAGE OUTPUT", 16);
            renderBaseSupplyImage(kWeaponSalvageMaterialId, {464,288,100,64});
            text({584,300,600,40}, fmt::format("{}  ×{}", tr(publishedContentRegistry().item(kWeaponSalvageMaterialId).displayName), kWeaponSalvageMaterialQuantity));
            text({464,400,728,64}, workshopUi_.salvageConfirmation ? "CONFIRM DESTROYING THIS WEAPON" :
                 p.salvage.canCommit ? "SALVAGE REMOVES THE SELECTED WEAPON" : p.salvage.message);
            if (workshopUi_.salvageConfirmation) {
                button(Control::Cancel, "KEEP WEAPON");
                button(Control::ConfirmSalvage, "CONFIRM SALVAGE");
            } else button(Control::PreviewSalvage, "PREVIEW SALVAGE");
        } else text({464,158,728,64}, "SELECT A STASH WEAPON");
        scroll(uiWorkshopFeedbackRect(), workshopFeedback_, 14, workshopUi_.feedbackScroll, workshopUi_.feedbackMaximum);
        SDL_SetRenderDrawBlendMode(renderer_,previous);
        return;
    }
    button(Control::Training, "ENGINEERING TRAINING", p.trainingSelected);
    for(std::size_t i=0;i<6;++i) {
        const auto index=workshopUi_.page*6+i;if(index>=p.recipes.size())break;const auto &row=p.recipes[index];
        const auto box=uiWorkshopRecipeRect(i);
        renderFoundationButton(6000+static_cast<int>(i),box,"",!p.order && !p.training.orderPresent,false,
            !p.trainingSelected && p.selection==row.id,row.fullName,14);
        renderBaseSupplyImage(row.output,{box.x+8,box.y+5,52,38});
        text({box.x+72,box.y+5,244,38},fmt::format("{}  x{}",row.shortName,row.quantity),14);
    }
    if(uiWorkshopPageCount(p.recipes.size())>1){button(Control::Previous,"PREVIOUS PAGE");button(Control::Next,"NEXT");text({80,118,328,20},fmt::format("{} / {}",workshopUi_.page+1,uiWorkshopPageCount(p.recipes.size())),12);}
    const auto row=std::find_if(p.recipes.begin(),p.recipes.end(),[&](const auto &r){return p.selection==r.id;});
    if (p.trainingSelected) {
        text({464,158,728,56}, "GENERAL RESIDENT → ENGINEERING", 20);
    } else if(row!=p.recipes.end()) {
        renderBaseSupplyImage(row->output,{464,154,172,66});
        text({656,158,536,56},fmt::format("{}  x{}",row->fullName,row->quantity),16);
    }
    const bool research = p.selection == kArmorRepairResearchId;
    const bool armorRecipe = p.selection == kArmorRepairRecipeId;
    const auto activeLabel = research ? "RESEARCH IN PROGRESS" : "PRODUCTION ACTIVE";
    const char *status = "PRODUCTION PREPARATION";
    if (p.trainingSelected)
        status = p.training.orderPresent
            ? (p.training.paused ? "TRAINING PAUSED" : "TRAINING IN PROGRESS")
            : "TRAINING PREPARATION";
    else if (!p.installed)
        status = "WORKSHOP PAUSED IN RESERVE";
    else if (p.order)
        status = p.order->outputReady ? "PRODUCTION OUTPUT WAITING" : activeLabel;
    else if (research)
        status = "BLUEPRINT RESEARCH PREPARATION";
    else if (armorRecipe)
        status = "ARMOR REPAIR RECIPE UNLOCKED";
    text({464,232,728,28},status,16);
    const auto &power = gameSession_.profile().emergencyPower;
    const bool powerRunning = p.order && power && power->target == BaseEmergencyPowerTarget::Workshop &&
        power->activeJobId == p.order->jobId;
    auto time=p.order?p.order->outputReady?tr("OUTPUT READY | WORKER RELEASED"):fmt::format("{} {}",tr(powerRunning ? "WORK REMAINING" : "WORLD TIME REMAINING"),duration(p.production.remainingMinutes)):
        fmt::format("{} {}",tr(research?"RESEARCH DURATION":"PRODUCTION DURATION"),p.start.canCommit?duration(p.start.durationMinutes):tr("AVAILABLE WITH VALID PLAN"));
    if((!p.order || !p.order->outputReady) && row!=p.recipes.end())
        time+=fmt::format("  |  {} {}",tr(p.order?"WORKERS COMMITTED":"WORKERS REQUIRED"),p.order?p.production.committedWorkers:row->workers);
    if (p.trainingSelected)
        time = fmt::format("{} {}", tr(p.training.orderPresent ? "WORLD TIME REMAINING" : "TRAINING DURATION"),
            duration(p.training.orderPresent ? p.training.remainingMinutes : kBaseEngineeringTrainingMinutes));
    text({464,270,728,28},time);
    if(workshopUi_.facilities) {
        button(Control::Staffing,p.workforce.workshopWorker?"CLEAR WORKSHOP WORKER":"ASSIGN BEST WORKER");
        button(Control::Upgrade,p.cancelUpgrade?"CANCEL WORKSHOP UPGRADE":p.upgradeComplete?"WORKSHOP LEVEL 2 COMPLETE":"UPGRADE WORKSHOP TO LEVEL 2");
        std::string info=fmt::format("{} {} · {} {}",tr("WORKSHOP LEVEL"),p.level,tr("ASSIGNED"),p.workforce.workshopWorker?tr(baseResidentProfessionName(*p.workforce.workshopWorker)):tr("EMPTY"));
        if(p.activeUpgrade)info+=fmt::format("\n{} {}",tr("WORKSHOP UPGRADE IN PROGRESS"),duration(p.activeUpgrade->completionWorldMinute>p.worldMinute?p.activeUpgrade->completionWorldMinute-p.worldMinute:0));
        if(!p.staffing.canCommit)info+="\n"+tr(p.staffing.message);
        if(p.upgradeId && !p.upgrade.canCommit && !p.upgradeComplete)info+="\n"+tr(p.upgrade.message);
        scroll({464,392,728,112},info,14,workshopUi_.detailScroll,workshopUi_.detailMaximum);
    } else if (p.trainingSelected) {
        text({464,308,728,26}, p.training.orderPresent ? "RESERVED MATERIALS" : "REQUIRED INPUTS");
        scroll(uiWorkshopDetailsRect(), p.trainingMaterials + "\n" +
            tr("TRAINING REQUIRES A HEALTHY ENGINEER AND AN AVAILABLE HEALTHY GENERAL RESIDENT"),
            14, workshopUi_.detailScroll, workshopUi_.detailMaximum);
    } else {
        text({464,308,728,26},research?(p.order?"RESERVED BLUEPRINT":"REQUIRED BLUEPRINT"):
            p.order?"RESERVED MATERIALS":"REQUIRED INPUTS");
        scroll(uiWorkshopDetailsRect(),p.order && p.order->outputReady?tr("MATERIALS CONSUMED - OUTPUT IS KEPT"):
            row!=p.recipes.end()?row->materials:tr("NO PUBLISHED MANUFACTURING RECIPE"),14,workshopUi_.detailScroll,workshopUi_.detailMaximum);
    }
    text({464,510,728,22}, p.trainingSelected
        ? (p.training.paused ? p.training.pauseReason : "TRAINING USES THE WORKSHOP PRODUCTION SLOT")
        : powerRunning?"ORDER POWERED · DOUBLE PROGRESS":
        p.order && !p.order->outputReady && !research && power && power->target == BaseEmergencyPowerTarget::Workshop && !power->activeJobId ?
            "ORDER POWER PAUSED":
        !p.installed?"INSTALL IT FREE ON THE REGIONAL PAGE TO RESUME EXACT PROGRESS":research?(p.order?"COMPLETION UNLOCKS THE ARMOR REPAIR RECIPE":"LEVEL 2 WORKSHOP · ASSIGNED HEALTHY ENGINEER"):
        p.order?"AUTO STASH ON COMPLETION - COLLECT ONLY WHEN FULL":"RESERVE ON START - COMPLETE USING WORLD TIME",12);
    button(Control::Facilities,workshopUi_.facilities?"HIDE FACILITY AND STAFF":"FACILITY AND STAFF",workshopUi_.facilities);
    if (p.trainingSelected) {
        if (p.training.orderPresent)
            button(Control::Cancel, "CANCEL & RETURN INPUTS");
        button(Control::Main, p.training.orderPresent ? status : "START TRAINING");
    } else {
        if(p.order && !p.order->outputReady)button(Control::Cancel,research?"CANCEL & RETURN BLUEPRINT":"CANCEL & RETURN INPUTS");
        const auto startLabel = research ? "START RESEARCH" : "START PRODUCTION";
        button(Control::Main, p.order
            ? (p.order->outputReady ? "COLLECT TO STASH" : activeLabel)
            : startLabel);
    }
    std::string message=workshopFeedback_;
    if (message.empty() && p.trainingSelected) {
        if (!p.training.orderPresent && !p.trainingStart.canCommit) message = p.trainingStart.message;
        else if (p.training.paused) message = p.training.pauseReason;
        else if (p.training.orderPresent && !p.trainingCancel.canCommit) message = p.trainingCancel.message;
    } else if(message.empty())message=!p.installed?"WORKSHOP IS PACKED | PRODUCTION AND UPGRADES ARE PAUSED":
        p.order?!p.returns.canCommit?p.returns.message:"":p.selection?!p.start.canCommit?p.start.message:"":"NO PUBLISHED MANUFACTURING RECIPE";
    scroll(uiWorkshopFeedbackRect(),message,14,workshopUi_.feedbackScroll,workshopUi_.feedbackMaximum);
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}
