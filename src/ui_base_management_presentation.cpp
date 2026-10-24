#include "ui_base_management_presentation.h"
#include "base_population_domain.h"
#include "ui_text_renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>

std::string uiBaseManagementText(UiLanguage language, std::string_view en, std::string_view zh) {
    return std::string{language == UiLanguage::SimplifiedChinese ? zh : en};
}
std::string uiBaseFacilityTitle(BaseFacilityKind kind, UiLanguage language) {
    const auto text=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    switch(kind) {
    case BaseFacilityKind::Storage:return text("Storage","仓库");
    case BaseFacilityKind::Supply:return text("Supply & recovery","供应与回收");
    case BaseFacilityKind::Allocation:return text("Resources & needs","资源分配与需求");
    case BaseFacilityKind::Medical:return text("Medical","医疗站");
    case BaseFacilityKind::Dormitory:return text("Dormitory","宿舍");
    case BaseFacilityKind::KitchenWater:return text("Kitchen & water","厨房与净水");
    case BaseFacilityKind::Workshop:return text("Workshop","工坊");
    case BaseFacilityKind::RaidGate:return text("Deployment","对局出击");
    }
    return text("Unknown facility","未知设施");
}
std::string uiBaseFacilityShortName(BaseFacilityKind kind, UiLanguage language) {
    const auto text=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    switch(kind) {
    case BaseFacilityKind::Storage:return text("Storage","仓库");
    case BaseFacilityKind::Supply:return text("Supply","供应");
    case BaseFacilityKind::Allocation:return text("Needs","分配");
    case BaseFacilityKind::Medical:return text("Medical","医疗");
    case BaseFacilityKind::Dormitory:return text("Dorm","宿舍");
    case BaseFacilityKind::KitchenWater:return text("Kitchen","厨房");
    case BaseFacilityKind::Workshop:return text("Workshop","工坊");
    case BaseFacilityKind::RaidGate:return text("Deploy","出击");
    }
    return text("Facility","设施");
}

namespace {
enum class GlyphBadge { None, Working, Ready, Attention, Blocked };
struct GlyphAppearance { SDL_Color color; bool filled{}; GlyphBadge badge{}; };
GlyphAppearance serviceGlyph(const BaseFacilityWorldServiceProjection &service) {
    switch(service.status) {
    case BaseFacilityWorldServiceStatus::Ready:return {{126,224,174,255},false,GlyphBadge::None};
    case BaseFacilityWorldServiceStatus::Working:return {{232,180,84,255},true,GlyphBadge::Working};
    case BaseFacilityWorldServiceStatus::OutputReady:return {{108,210,240,255},true,GlyphBadge::Ready};
    case BaseFacilityWorldServiceStatus::NeedsStaff:return {{238,130,72,255},false,GlyphBadge::Attention};
    case BaseFacilityWorldServiceStatus::Blocked:return {{226,84,78,255},false,GlyphBadge::Blocked};
    }
    return {{174,188,180,255},false,GlyphBadge::None};
}
bool glyphBounds(SDL_FRect bounds) {
    return std::isfinite(bounds.x)&&std::isfinite(bounds.y)&&
        std::isfinite(bounds.w)&&std::isfinite(bounds.h)&&bounds.w>=12&&bounds.h>=12;
}
struct GlyphPen {
    SDL_Renderer *renderer;SDL_Color previous{};SDL_BlendMode blend{};
    explicit GlyphPen(SDL_Renderer *r,SDL_Color color):renderer(r) {
        SDL_GetRenderDrawColor(r,&previous.r,&previous.g,&previous.b,&previous.a);
        SDL_GetRenderDrawBlendMode(r,&blend);
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r,color.r,color.g,color.b,color.a);
    }
    ~GlyphPen() {
        SDL_SetRenderDrawColor(renderer,previous.r,previous.g,previous.b,previous.a);
        SDL_SetRenderDrawBlendMode(renderer,blend);
    }
};
// A few local line/rectangle coordinates, not a new icon asset or UI framework.
struct GlyphCanvas {
    SDL_Renderer *renderer;float x,y,scale;
    void line(float x1,float y1,float x2,float y2) const {
        SDL_RenderLine(renderer,x+x1*scale,y+y1*scale,x+x2*scale,y+y2*scale);
    }
    void box(float left,float top,float width,float height,bool filled=false) const {
        const SDL_FRect bounds{x+left*scale,y+top*scale,width*scale,height*scale};
        if(filled)SDL_RenderFillRect(renderer,&bounds);else SDL_RenderRect(renderer,&bounds);
    }
    void badge(GlyphBadge value,float left,float top) const {
        if(value==GlyphBadge::Working) {box(left,top+6,2,4,true);box(left+4,top+3,2,7,true);box(left+8,top,2,10,true);}
        else if(value==GlyphBadge::Ready) {line(left,top+5,left+3,top+9);line(left+3,top+9,left+10,top);}
        else if(value==GlyphBadge::Attention) {box(left+4,top,2,6,true);box(left+4,top+8,2,2,true);}
        else if(value==GlyphBadge::Blocked) {line(left,top,left+9,top+9);line(left+9,top,left,top+9);}
    }
};
}

void renderUiBaseEntranceGlyph(SDL_Renderer *renderer,SDL_FRect bounds,
    const BaseFacilityWorldServiceProjection &service) {
    if(!renderer||!glyphBounds(bounds))return;
    const auto appearance=serviceGlyph(service);GlyphPen pen(renderer,appearance.color);
    const float scale=std::min(bounds.w/24.0F,bounds.h/28.0F);
    const GlyphCanvas glyph{renderer,bounds.x+(bounds.w-24*scale)*.5F,
        bounds.y+(bounds.h-28*scale)*.5F,scale};
    glyph.line(2,27,2,4);glyph.line(2,4,17,4);glyph.line(17,4,17,27);
    glyph.box(5,8,9,18,appearance.filled);glyph.box(11,16,2,2,true);
    glyph.badge(appearance.badge,13,0);
}

void renderUiBaseWorkSocketGlyph(SDL_Renderer *renderer,
    const BaseFacilityWorkSocketProjection &socket,
    const BaseFacilityWorldServiceProjection &service,
    const std::optional<BaseFacilityWorkerWorldProjection> &worker,
    const std::optional<BaseResidentWorldProjection> &residents,
    const std::optional<BaseResourceFlowWorldProjection> &resourceFlow) {
    const SDL_FRect bounds{socket.bounds.position.x,socket.bounds.position.y,
        socket.bounds.size.x,socket.bounds.size.y};
    if(!renderer||!glyphBounds(bounds))return;
    const bool workerMatches=worker&&worker->facility==service.facility&&worker->workSocket==socket.kind;
    const bool residentsMatch=residents&&residents->facility==service.facility&&residents->workSocket==socket.kind;
    auto appearance=serviceGlyph(service);
    const bool serviceBlocked=service.status==BaseFacilityWorldServiceStatus::NeedsStaff||
        service.status==BaseFacilityWorldServiceStatus::Blocked;
    if(!serviceBlocked) {
        if(workerMatches) {
            if(worker->status==BaseFacilityWorkerWorldStatus::Missing)
                appearance={{230,102,78,255},false,GlyphBadge::Attention};
            else if(worker->status==BaseFacilityWorkerWorldStatus::Paused)
                appearance={{214,132,76,255},false,GlyphBadge::Attention};
            else if(worker->status==BaseFacilityWorkerWorldStatus::Working&&appearance.badge==GlyphBadge::None)
                appearance={{240,190,84,255},true,GlyphBadge::Working};
        }
        if(residentsMatch) {
            if(residents->status==BaseResidentWorldStatus::Injured)
                appearance={{232,176,82,255},residents->residents>0,GlyphBadge::Attention};
            else if(residents->status==BaseResidentWorldStatus::Overcrowded)
                appearance={{226,98,76,255},residents->residents>0,GlyphBadge::Attention};
            else if(appearance.badge==GlyphBadge::None)
                appearance={residents->residents?SDL_Color{122,202,166,255}:SDL_Color{126,138,134,255},residents->residents>0,GlyphBadge::None};
        }
        if(resourceFlow&&resourceFlow->facility==service.facility&&resourceFlow->workSocket==socket.kind) {
            if(resourceFlow->status==BaseResourceFlowWorldStatus::Shortage)
                appearance={{226,112,76,255},false,GlyphBadge::Attention};
            else if(appearance.badge==GlyphBadge::None)
                appearance={resourceFlow->status==BaseResourceFlowWorldStatus::Empty?SDL_Color{126,138,134,255}:
                    resourceFlow->status==BaseResourceFlowWorldStatus::Prepared?SDL_Color{122,202,166,255}:SDL_Color{126,174,218,255},
                    resourceFlow->status!=BaseResourceFlowWorldStatus::Empty,GlyphBadge::None};
        }
    }
    GlyphPen pen(renderer,appearance.color);
    const float scale=std::min({1.0F,bounds.w/56.0F,bounds.h/36.0F});
    const GlyphCanvas glyph{renderer,bounds.x+(bounds.w-56*scale)*.5F,
        bounds.y+(bounds.h-36*scale)*.5F,scale};
    switch(socket.kind) {
    case BaseFacilityWorkSocketKind::StorageHandling:
        glyph.box(4,18,17,14,appearance.filled);glyph.box(24,18,17,14,appearance.filled);
        glyph.box(14,3,17,13,appearance.filled);glyph.line(22,4,22,12);break;
    case BaseFacilityWorkSocketKind::MedicalBed:
        glyph.box(5,10,37,17);glyph.box(8,13,9,8,appearance.filled);
        glyph.box(20,13,19,11,appearance.filled);glyph.line(5,27,5,33);glyph.line(42,27,42,33);
        glyph.line(29,3,29,9);glyph.line(26,6,32,6);break;
    case BaseFacilityWorkSocketKind::DormitoryBunk:
        glyph.line(5,3,5,33);glyph.line(42,3,42,33);
        glyph.box(6,9,35,8,appearance.filled);glyph.box(6,24,35,7,appearance.filled);
        glyph.box(9,5,8,4);glyph.box(9,20,8,4);break;
    case BaseFacilityWorkSocketKind::KitchenProcessing:
        glyph.box(10,13,27,18,appearance.filled);glyph.line(8,11,39,11);
        glyph.box(19,6,10,4);glyph.line(6,17,6,25);glyph.line(6,17,10,17);
        glyph.line(41,17,41,25);glyph.line(37,17,41,17);break;
    case BaseFacilityWorkSocketKind::WorkshopBench:
        glyph.box(4,12,39,7,appearance.filled);glyph.line(8,19,8,33);glyph.line(39,19,39,33);
        glyph.line(8,28,39,28);glyph.box(16,5,15,4,appearance.filled);break;
    }
    glyph.badge(appearance.badge,45,1);
    if(workerMatches||residentsMatch) {
        // One presence marker, never a head count or an inferred staffing rule.
        const bool present=(workerMatches&&worker->status!=BaseFacilityWorkerWorldStatus::Missing)||
            (residentsMatch&&residents->residents>0);
        glyph.box(49,16,4,4,present);glyph.box(48,22,6,9,present);
        glyph.line(49,31,49,33);glyph.line(53,31,53,33);
    }
}

std::string uiBaseFacilityStatus(BaseFacilityOperationalStatus status,UiLanguage language) {
    switch(status) {
    case BaseFacilityOperationalStatus::Operational:return uiBaseManagementText(language,"Installed","已安装");
    case BaseFacilityOperationalStatus::Reserve:return uiBaseManagementText(language,"In reserve","储备中");
    case BaseFacilityOperationalStatus::Unavailable:return uiBaseManagementText(language,"Unavailable","未开放");
    }
    return uiBaseManagementText(language,"Unknown","未知");
}
std::string uiBaseQuickActionTitle(BaseFacilityQuickActionKind kind,UiLanguage language) {
    const auto t=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    switch(kind) {
    case BaseFacilityQuickActionKind::OpenFunction:return t("Open","打开功能");
    case BaseFacilityQuickActionKind::AssignBestWorker:return t("Assign worker","安排工作人员");
    case BaseFacilityQuickActionKind::ClearWorker:return t("Release worker","撤下工作人员");
    case BaseFacilityQuickActionKind::StartUpgrade:return t("Start upgrade","开始升级");
    case BaseFacilityQuickActionKind::CancelUpgrade:return t("Cancel upgrade","取消升级");
    case BaseFacilityQuickActionKind::CollectManufacturing:return t("Collect output","领取产物");
    case BaseFacilityQuickActionKind::CancelManufacturing:return t("Cancel production","取消生产");
    case BaseFacilityQuickActionKind::StartResidentTreatment:return t("Treat residents","治疗居民");
    case BaseFacilityQuickActionKind::AutoFillWorkers:return t("Fill vacant jobs","自动补齐岗位");
    }
    return t("Unavailable","不可用");
}
std::string uiBaseOperationTitle(BaseOperationOverviewKind kind,UiLanguage language) {
    const auto t=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    switch(kind) {
    case BaseOperationOverviewKind::OutputReady:return t("Output ready","产物就绪");
    case BaseOperationOverviewKind::Construction:return t("Upgrade","设施升级");
    case BaseOperationOverviewKind::ResidentTreatment:return t("Resident treatment","居民治疗");
    case BaseOperationOverviewKind::Manufacturing:return t("Production","生产中");
    case BaseOperationOverviewKind::StaffingGap:return t("Needs worker","缺少人员");
    case BaseOperationOverviewKind::ResourceShortage:return t("Resource shortage","资源短缺");
    case BaseOperationOverviewKind::ResidentPressure:return t("Resident pressure","居民压力");
    case BaseOperationOverviewKind::BaseWish:return t("Base wish","基地愿望");
    }
    return t("Unknown operation","未知事项");
}
std::string uiBaseMoraleTrendTitle(BaseMoraleTrend trend,UiLanguage language) {
    switch(trend) {
    case BaseMoraleTrend::Falling:return uiBaseManagementText(language,"Falling","下降");
    case BaseMoraleTrend::Steady:return uiBaseManagementText(language,"Steady","平稳");
    case BaseMoraleTrend::Rising:return uiBaseManagementText(language,"Rising","上升");
    }
    return uiBaseManagementText(language,"Unknown","未知");
}
std::string uiBaseRemainingTime(std::uint64_t minutes,UiLanguage language) {
    if(language==UiLanguage::SimplifiedChinese) {
        if(minutes>=kWorldMinutesPerDay)return fmt::format("{}天{}小时{}分",minutes/kWorldMinutesPerDay,minutes%kWorldMinutesPerDay/60,minutes%60);
        if(minutes>=60)return fmt::format("{}小时{}分",minutes/60,minutes%60);
        return fmt::format("{}分",minutes);
    }
    if(minutes>=kWorldMinutesPerDay)return fmt::format("{}d {}h {}m",minutes/kWorldMinutesPerDay,minutes%kWorldMinutesPerDay/60,minutes%60);
    if(minutes>=60)return fmt::format("{}h {}m",minutes/60,minutes%60);
    return fmt::format("{}m",minutes);
}
SDL_FRect uiBaseManagementExplanationRect() noexcept {return {606,324,604,226};}
SDL_FRect uiBaseManagementFeedbackRect() noexcept {return {88,678,1098,20};}
SDL_FRect uiBaseManagementInspectorRect() noexcept {return {70,276,520,274};}
SDL_FRect uiBaseManagementDetailsButtonRect() noexcept {return {1050,648,136,28};}
void renderUiBaseManagementFeedback(SDL_Renderer *renderer,UiTextRenderer &text,
    SDL_FRect area,std::string_view value,float &offset) {
    const auto lines=wrapUiText(localizeUiText(text.language(),value),area.w-18,0,
        [&](auto line){return text.measure(line,14);});
    constexpr float height=21;
    const int visible=std::max(1,static_cast<int>(area.h/height));
    const int maximum=std::max(0,static_cast<int>(lines.size())-visible);
    offset=std::clamp(offset,0.0F,static_cast<float>(maximum));
    for(int row=0;row<visible && row+static_cast<int>(offset)<static_cast<int>(lines.size());++row)
        uiFinalText(renderer,text,{area.x,area.y+row*height,area.w-18,height},
            lines[row+static_cast<int>(offset)].text,14);
    if(maximum) {
        const SDL_FRect track{area.x+area.w-4,area.y,3,area.h};
        const SDL_FRect thumb{track.x,track.y+(track.h-18)*offset/maximum,3,18};
        SDL_SetRenderDrawColor(renderer,51,69,69,255);SDL_RenderFillRect(renderer,&track);
        SDL_SetRenderDrawColor(renderer,113,191,179,255);SDL_RenderFillRect(renderer,&thumb);
    }
}

std::vector<UiBaseConstructionCard> projectUiBaseConstructionCards(
    const ProfileState &p,const ContentRegistry &c,bool purchase,UiLanguage language) {
    std::vector<UiBaseConstructionCard> rows;
    const auto t=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    if(purchase) {
        for(const auto &project:projectBaseConstructionCatalog(p,c)) {
            UiBaseConstructionCard row;
            row.kind=UiBaseConstructionCardKind::Project; row.identity=project.definitionId.value();
            row.title=localizeUiText(language,project.displayName);
            row.facts=fmt::format(fmt::runtime(t("Lv {} → {} · Material {}","等级 {} → {} · 材料 {}")),
                project.currentLevel,project.targetLevel,project.materialCost);
            row.timing=fmt::format(fmt::runtime(t("Workers {} · {} min","人员 {} · {} 分钟")),project.workerCount,project.durationMinutes);
            row.enabled=project.canCommit;
            row.active=project.action==BaseConstructionCatalogAction::Cancel;
            row.complete=project.action==BaseConstructionCatalogAction::Complete;
            switch(project.action) {
            case BaseConstructionCatalogAction::Start:row.action=t("Start project","开始建设");break;
            case BaseConstructionCatalogAction::Cancel:row.action=fmt::format(fmt::runtime(t("Cancel · {} min left","取消 · 余 {} 分钟")),project.remainingMinutes);break;
            case BaseConstructionCatalogAction::Complete:row.action=t("Complete","已完成");break;
            case BaseConstructionCatalogAction::Blocked:row.action=t("Unavailable","条件未满足");break;
            }
            row.explanation=localizeUiText(language,project.message);
            if(row.active) row.explanation+=t("\nCancel returns the original materials and releases workers.","\n取消将按原规则返还材料并释放工作人员。");
            if(row.complete) row.explanation=t("This project is complete. Open the facility to use it.","该建设项目已完成，可前往设施使用原有功能。");
            rows.push_back(std::move(row));
        }
    } else {
        constexpr std::array kinds{BaseFacilityKind::Storage,BaseFacilityKind::Medical,BaseFacilityKind::Dormitory,
            BaseFacilityKind::KitchenWater,BaseFacilityKind::Workshop};
        constexpr std::array ids{"base_facility.warehouse","base_facility.medical","base_facility.dormitory",
            "base_facility.kitchen_water","base_facility.workshop"};
        for(std::size_t i=0;i<kinds.size();++i) {
            const BaseFacilityDefinitionId id{ids[i]};
            if(!baseFacilityOwned(p,id) || baseFacilityInstalled(p,id))continue;
            const auto plan=queryInstallBaseFacility(p,c,{id});
            rows.push_back({UiBaseConstructionCardKind::ReserveFacility,std::string{id.value()},uiBaseFacilityTitle(kinds[i],language),
                t("In reserve","储备中"),t("Choose a valid location","选择合法位置"),
                plan.canCommit?t("Install","安装"):t("Unavailable","条件未满足"),
                localizeUiText(language,plan.message),plan.canCommit,false,false,{}});
        }
    }
    const auto stash=assetsInContainer(p,ProfileContainerId::stash());
    for(const auto &definition:c.items()) {
        if(!definition.basePlacement)continue;
        std::size_t count{}; std::optional<AssetInstanceId> first;
        for(const auto *asset:stash)if(asset->definitionId==definition.definitionId){if(!first)first=asset->instanceId;++count;}
        if(purchase?definition.marketBuyPrice==0:count==0)continue;
        UiBaseConstructionCard row;
        row.kind=UiBaseConstructionCardKind::Placeable;row.identity=definition.definitionId.value();
        row.title=localizeUiText(language,definition.displayName);row.firstOwnedAsset=first;
        // The existing purchase service has no pure query. Display only the
        // known price prerequisite; the original command still decides space,
        // ownership and persistence, without copying/mutating a Profile here.
        row.enabled=!purchase || p.currency>=definition.marketBuyPrice;
        row.facts=purchase?fmt::format(fmt::runtime(t("Price {}","价格 {}")),definition.marketBuyPrice):fmt::format(fmt::runtime(t("In storage ×{}","仓库中 ×{}")),count);
        row.timing=t("Owned item · choose location","自有物品 · 选择位置");
        row.action=purchase?(row.enabled?t("Buy & place","购买并放置"):t("Insufficient funds","货币不足")):t("Place","放置");
        row.explanation=purchase?t("Purchase first enters storage, then opens placement. Cancelling placement keeps the purchased item. The original command checks storage and saves the transaction.",
            "购买先存入仓库，再进入放置。取消放置仍保留已购物品；仓库空间及保存由原命令检查。"):
            t("Place the first available stored instance. Rotation, obstruction checks and ownership keep their original rules.","放置仓库中首个可用实例；旋转、阻挡检查及所有权规则保持。");
        rows.push_back(std::move(row));
    }
    return rows;
}
UiBaseKitchenFacts projectUiBaseKitchenFacts(const ProfileState &p,const ContentRegistry &c) {
    UiBaseKitchenFacts result;
    result.installed=baseFacilityInstalled(p,BaseFacilityDefinitionId{"base_facility.kitchen_water"});
    result.level=p.baseConstruction.kitchenWaterLevel;result.pool=p.baseResources.pool;
    result.demand=populationAdjustedDailyDemand(p.basePopulation);result.lastShortfall=p.baseResources.lastShortfall;
    result.operations=projectBaseOperations(p.baseResources,c.baseOperations(),result.demand);
    result.clock=projectWorldClock(p.worldClock);result.dailyCycles=p.baseResources.resolvedDemandCycleCount;
    return result;
}
