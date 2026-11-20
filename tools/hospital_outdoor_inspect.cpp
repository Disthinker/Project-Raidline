#include "raid_hospital_outdoor.h"
#include "raid_lifecycle.h"
#include "save_repository.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using Stage = RaidDeploymentProgressStage;

const char *stageName(Stage stage)
{
    switch (stage)
    {
    case Stage::Validating: return "验证出击";
    case Stage::PreparingSnapshot: return "准备快照";
    case Stage::GeneratingLayout: return "生成室外布局";
    case Stage::FreezingLoot: return "冻结战利品";
    case Stage::ValidatingSnapshot: return "校验快照";
    case Stage::BuildingWorld: return "创建世界";
    case Stage::SavingProfile: return "保存进度";
    case Stage::Complete: return "完成";
    }
    return "未知";
}

double elapsedMs(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct Timings
{
    Stage stage{Stage::Validating};
    double layoutMs{};
    double deployMs{};
};

ProfileState deploy(const ContentRegistry &content, std::uint64_t seed, Timings &times)
{
    ProfileState profile = makeNewAlphaProfile("hospital-inspection", content);
    Clock::time_point layoutStart;
    const auto start = Clock::now();
    const auto result = executeDeploy(profile, content,
        {"hospital-inspection", "hospital-return", seed, kHospitalFloorplanMapId},
        {profile.revision, "inspect-deploy"}, [&](const RaidDeploymentProgress &event)
        {
            times.stage = event.stage;
            if (event.stage == Stage::GeneratingLayout)
                layoutStart = Clock::now();
            if (event.stage == Stage::FreezingLoot && layoutStart != Clock::time_point{})
                times.layoutMs = elapsedMs(layoutStart, Clock::now());
        });
    times.deployMs = elapsedMs(start, Clock::now());
    if (!result.succeeded || !profile.pendingRaid)
        throw std::runtime_error(result.message.empty() ? "出击未产生快照" : result.message);
    return profile;
}

Json point(Vec2 value) { return {{"x", value.x}, {"y", value.y}}; }

Json rectangle(ContentRect value)
{
    return {{"position", point(value.position)}, {"size", point(value.size)}};
}

Json route(const std::vector<Vec2> &points, const std::vector<ContentRect> &blockers)
{
    Json encoded = Json::array();
    double distance{}, nearCoverDistance{};
    for (std::size_t index{}; index < points.size(); ++index)
    {
        encoded.push_back(point(points[index]));
        if (index == 0) continue;
        const auto a = points[index - 1], b = points[index];
        const auto segment = std::hypot(b.x - a.x, b.y - a.y);
        const Vec2 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
        const bool nearby = std::any_of(blockers.begin(), blockers.end(), [&](ContentRect r) {
            const float dx = mid.x - std::clamp(mid.x, r.position.x, r.position.x + r.size.x);
            const float dy = mid.y - std::clamp(mid.y, r.position.y, r.position.y + r.size.y);
            return dx * dx + dy * dy <= 160.0F * 160.0F;
        });
        distance += segment;
        if (nearby) nearCoverDistance += segment;
    }
    return {{"distance", distance}, {"near_cover", nearCoverDistance}, {"points", std::move(encoded)}};
}

void inspectFrozenLayout(Json &item, const MapDefinition &map, const RaidGeneratedMapLayout &layout,
                         Json &envelope)
{
    const auto &definition = map.proceduralOutdoor;
    item["version"] = layout.layoutVersion;
    item["limit"] = definition.maximumAttempts;
    item["attempt"] = layout.generationAttempt;
    item["fallback"] = layout.usedFallback;
    item["hash"] = std::to_string(layout.layoutHash);
    item["bounds"] = rectangle(map.walkableBounds);
    item["grid"] = {definition.columns, definition.rows};
    item["district_grid"] = {definition.districtColumns, definition.districtRows};
    item["layout"] = std::move(envelope.at("payload").at("pending_raid").at("spatial_layout"));
    item["parcels"] = Json::array();
    if (layout.layoutVersion == 6U && usesHospitalOutdoorPlan(map))
    {
        const auto routes = inspectHospitalOutdoorRoutes(map, layout);
        item["routes"] = {{"main", route(routes.mainRoute, layout.ballisticBlockers)},
                          {"bypass", route(routes.bypassRoute, layout.ballisticBlockers)},
                          {"reachable", routes.anchorsReachable}};
        for (const auto &parcel : inspectHospitalParcels(map, layout))
        {
            constexpr std::array names{"沿街店面", "小院", "勤务棚"};
            item["parcels"].push_back({{"family", names.at(static_cast<std::size_t>(parcel.family))},
                {"bounds", rectangle(parcel.bounds)}, {"gate", point(parcel.gate)},
                {"turns", static_cast<unsigned>(parcel.quarterTurns)}});
        }
        if (!routes.passable())
            throw std::runtime_error("主路、绕行或关键交互点未通过通行校验");
    }
    item["stage"] = layout.usedFallback ? "尝试耗尽，采用备用布局" : "完成";
}

std::string readInput(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取输入文件");
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return bytes.str();
}

Json savedSample(const ContentRegistry &content, const std::filesystem::path &path)
{
    const std::string bytes = readInput(path);
    const auto loaded = deserializeProfileEnvelope(bytes, content);
    if (!loaded.profile)
        throw std::runtime_error("专档读取失败 · " + loaded.message);
    if (!loaded.profile->pendingRaid || loaded.profile->pendingRaid->mapDefinitionId != kHospitalFloorplanMapId)
        throw std::runtime_error("专档中没有冻结的医院对局");
    const auto &raid = *loaded.profile->pendingRaid;
    Json envelope = Json::parse(bytes);
    Json item{{"seed", std::to_string(raid.seed)}, {"frozen", true},
              {"profile", loaded.profile->profileId}, {"raid", raid.raidId},
              {"content", envelope.at("content_version")}};
    inspectFrozenLayout(item, content.map(raid.mapDefinitionId), raid.spatialLayout, envelope);
    std::cout << "frozen=" << loaded.profile->profileId << " seed=" << raid.seed
              << " hash=" << raid.spatialLayout.layoutHash << '\n';
    return item;
}

Json sample(const ContentRegistry &content, std::uint64_t seed)
{
    const auto &map = content.map(kHospitalFloorplanMapId);
    const auto &definition = map.proceduralOutdoor;
    Json item{{"seed", std::to_string(seed)}, {"version", definition.layoutVersion},
              {"limit", definition.maximumAttempts}, {"content", content.contentVersion()}};
    Timings first, repeated;
    Timings *active = &first;
    try
    {
        const auto profile = deploy(content, seed, first);
        const auto &layout = profile.pendingRaid->spatialLayout;
        active = &repeated;
        const auto second = deploy(content, seed, repeated);
        if (layout != second.pendingRaid->spatialLayout)
            throw std::runtime_error("同种子两次出击产生不同的冻结布局");
        item["timings_ms"] = {{"first_deploy", first.deployMs}, {"repeat_deploy", repeated.deployMs},
                              {"first_layout", first.layoutMs}, {"repeat_layout", repeated.layoutMs}};
        auto envelope = Json::parse(serializeProfileEnvelope(profile, content.contentVersion()));
        inspectFrozenLayout(item, map, layout, envelope);
        std::cout << "seed=" << seed << " v=" << layout.layoutVersion
            << " attempt=" << layout.generationAttempt << "/" << definition.maximumAttempts
            << " fallback=" << layout.usedFallback << " hash=" << layout.layoutHash
            << " layout_ms(first/repeat)=" << first.layoutMs << "/" << repeated.layoutMs
            << " deploy_ms(first/repeat)=" << first.deployMs << "/" << repeated.deployMs;
        if (item.contains("routes"))
            std::cout << " route_units(main/bypass)=" << item["routes"]["main"]["distance"]
                      << "/" << item["routes"]["bypass"]["distance"]
                      << " parcels=" << item["parcels"].size();
        std::cout << '\n';
    }
    catch (const std::exception &error)
    {
        item["stage"] = item.contains("layout") ? "复核冻结布局" : stageName(active->stage);
        item["error"] = error.what();
        std::cerr << "seed=" << seed << " stage=" << item.at("stage")
                  << " error=" << error.what() << '\n';
    }
    return item;
}

constexpr std::string_view header = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>医院片区 · 种子检查</title>
<style>*{box-sizing:border-box}body{margin:auto;padding:1.2rem;max-width:1500px;background:#141c22;color:#e6ecee;font:15px/1.5 system-ui,sans-serif}h1{font-size:1.55rem;margin:.2em 0}label,select{margin:0 1em 0 0}#layers{display:flex;flex-wrap:wrap;gap:.35em;margin:.7em 0}#map{width:100%;display:block;background:#263139;border:1px solid #65737b}#info{margin:.6em 0;color:#d3e0e7}svg text{fill:white;paint-order:stroke;stroke:#1c2328;stroke-width:20px}summary{cursor:pointer;margin:.5em 0}#details{white-space:pre-line}</style>
<h1>医院片区 · 种子检查</h1><label>出击种子 <select id="seed"></select></label><span id="version"></span>
<div id="info"></div><div id="layers"></div><svg id="map" role="img" aria-label="医院室外地图"></svg>
<details><summary>地标、通行与计时明细</summary><div id="details"></div></details>
<script id="cases" type="application/json">)HTML";

constexpr std::string_view viewer = R"HTML(</script>
<script>
'use strict';
const samples=JSON.parse(document.querySelector('#cases').textContent);
const $=id=>document.getElementById(id), ns='http://www.w3.org/2000/svg';
const names={districts:'分区',terrain:'地表',roads:'道路',plots:'地块／大门',props:'物体占地',blockers:'碰撞／视线遮挡',anchors:'交互／资源',routes:'主路／绕行'};
const svg=$('map'), select=$('seed'), enabled={};
function shape(parent,name,attrs,title){
 const el=document.createElementNS(ns,name);
 for(const [k,v] of Object.entries(attrs))el.setAttribute(k,v);
 if(title){const t=document.createElementNS(ns,'title');t.textContent=title;el.append(t);}
 parent.append(el);return el;
}
function rect(group,b,attrs,title){return shape(group,'rect',{x:b.position.x,y:b.position.y,width:b.size.x,height:b.size.y,...attrs},title);}
function dot(group,p,r,color,title){return shape(group,'circle',{cx:p.x,cy:p.y,r,fill:color,stroke:'#18232c','stroke-width':8},title);}
function label(group,p,name,size){const t=shape(group,'text',{x:p.x,y:p.y,'font-size':size});t.textContent=name;}
const hospitalNames={
 'landmark.hospital.main_building':'区医院','landmark.hospital.service_clinic':'服务诊所',
 'landmark.hospital.transport_depot':'救护车保障站'
};
function landmarkName(m){return hospitalNames[m.definition_id] || m.display_name;}
function damage(item){return item.error ? '失败 · '+item.stage+' · '+item.error : item.stage;}
function show(){
 const s=samples[select.selectedIndex], l=s.layout;
 $('version').textContent='布局 v'+s.version+'　'+s.content;
 svg.replaceChildren();$('details').textContent='';
 if(!l){$('info').textContent=damage(s);return;}
 const routes=s.routes, length=key=>Math.round(routes[key].distance), t=s.timings_ms;
 $('info').textContent='尝试 '+s.attempt+'/'+s.limit+'　'+(s.frozen?'专档冻结布局 · ':'')+damage(s)+'　哈希 '+s.hash+
  (routes?'　主路 '+length('main')+'／绕行 '+length('bypass')+' 世界单位':'');
 $('details').textContent=l.landmarks.map(m=>landmarkName(m)+'（'+m.road_sockets.length+' 个道路接口）').join('、')+
  '\n地块 '+s.parcels.length+'　遮挡体 '+l.ballistic_blockers.length+'　交互点 '+l.anchor_placements.length+
  (routes?'\n玩家净宽通行 · '+(routes.reachable?'关键交互点可达':'存在不可达点')+
   '\n邻近遮挡的路程（每段中点到最近遮挡体 ≤ 160 世界单位）· 主路 '+Math.round(routes.main.near_cover)+'／绕行 '+Math.round(routes.bypass.near_cover):
   '\n旧版未定义主路与绕行对照。')+
  (s.frozen?'\n读取专档 '+s.profile+' · 对局 '+s.raid:'\n同种子连续两次出击，单位 ms。生成 '+t.first_layout.toFixed(2)+'／'+t.repeat_layout.toFixed(2)+
  '，完整领域出击 '+t.first_deploy.toFixed(2)+'／'+t.repeat_deploy.toFixed(2)+'。计时不含画面与磁盘写入。');
 const b=s.bounds,x=b.position.x,y=b.position.y,w=b.size.x,h=b.size.y;
 svg.setAttribute('viewBox',[x,y,w,h].join(' '));
 const groups={};for(const k in names){groups[k]=shape(svg,'g',{'data-name':k,display:enabled[k]?'inline':'none'});}
 const cols=['#5c7a79','#557a55','#8a7659','#706d93','#67819a','#68765b','#89706e','#7a6990'];
 function tiles(group,spans,grid,color,title){
  const dx=w/grid[0],dy=h/grid[1];
  let d='';for(const a of spans){const run=a.length*dx;d+='M'+(x+a.first_column*dx)+' '+(y+a.row*dy)+'h'+run+'v'+dy+'h-'+run+'Z';}
  shape(group,'path',{d,fill:color},title);
 }
 for(const d of l.districts)tiles(groups.districts,d.cells,s.district_grid,cols[(d.instance_id-1)%cols.length],d.display_name);
 const ground=['#61743e','#b8b9bb','#89725c','#51585e','#517f9d'];
 for(const [kind,color] of ground.entries())tiles(groups.terrain,l.terrain_spans.filter(a=>a.kind===kind),s.grid,color);
 groups.terrain.setAttribute('opacity','.67');
 for(const [kind,color] of ['#b5aa88','#d8c7a0','#fff1ce'].entries()){
  const cells=l.road_cells.filter(a=>a.kind===kind).map(a=>({row:a.row,first_column:a.column,length:1}));
  tiles(groups.roads,cells,s.grid,color,['支路','次干道','主干道'][kind]);
 }
 for(const m of l.landmarks){
  rect(groups.plots,m.bounds,{fill:'none',stroke:'#f6d174','stroke-width':14},landmarkName(m));
  for(const p of m.road_sockets)dot(groups.plots,p,30,'#ffe38d',landmarkName(m)+' 道路接口');
  label(groups.plots,m.bounds.position,landmarkName(m),94);
 }
 for(const p of s.parcels){
  rect(groups.plots,p.bounds,{fill:'none',stroke:'#8bf0ca','stroke-width':12},p.family+' · '+(90*p.turns)+'°');
  dot(groups.plots,p.gate,30,'#8bf0ca',p.family+' 大门');
 }
 for(const p of l.props)rect(groups.props,p.bounds,{fill:p.collidable?'#253743':'#a6b29d'},'物体 '+p.instance_id);
 for(const b of l.ballistic_blockers)rect(groups.blockers,b,{fill:'#bc554c','fill-opacity':.36,stroke:'#ff9a83','stroke-width':7},'碰撞及视线遮挡');
 for(const a of l.anchor_placements){
  const c={x:a.bounds.position.x+a.bounds.size.x/2,y:a.bounds.position.y+a.bounds.size.y/2};
  dot(groups.anchors,c,30,'#a8deff',a.id);
 }
 for(const r of l.resource_points)rect(groups.anchors,r.bounds,{fill:'none',stroke:'#cfb0ff','stroke-width':15},r.display_name);
 if(routes)for(const [key,color,title] of [['bypass','#ffad68','绕行'],['main','#47e0ff','主路']]){
  shape(groups.routes,'polyline',{points:routes[key].points.map(p=>p.x+','+p.y).join(' '),
   fill:'none',stroke:color,'stroke-width':22,'stroke-linejoin':'round'},title);
 }
 if(routes&&routes.main.points.length){
  for(const [p,name] of [[routes.main.points[0],'出生点'],[routes.main.points.at(-1),'医院入口']]){
   dot(groups.routes,p,42,'#ffffff',name);
   label(groups.routes,{x:p.x+54,y:p.y+105},name,98);
  }
 }
}
for(const [k,name] of Object.entries(names)){
 const label=document.createElement('label'), check=document.createElement('input');check.type='checkbox';
 enabled[k]=check.checked=k!=='terrain';check.onchange=()=>{enabled[k]=check.checked;const g=svg.querySelector('[data-name="'+k+'"]');if(g)g.setAttribute('display',check.checked?'inline':'none');};
 label.append(check,name);$('layers').append(label);
}
for(const [index,s] of samples.entries()){const option=document.createElement('option');option.textContent='样本 '+(index+1)+' · '+s.seed;select.append(option);}
select.onchange=show;show();
</script></html>)HTML";
}

int main(int argc, char **argv)
{
    try
    {
        int arg = 1;
        std::optional<ContentRegistry> external;
        if (arg + 1 < argc && std::string_view(argv[arg]) == "--content")
        {
            std::ifstream input(argv[arg + 1], std::ios::binary);
            if (!input) throw std::runtime_error("无法读取指定内容文件");
            std::ostringstream bytes;
            bytes << input.rdbuf();
            external = ContentRegistry::fromJson(bytes.str());
            arg += 2;
        }
        const bool frozen = arg < argc && std::string_view(argv[arg]) == "--frozen";
        if (frozen) ++arg;
        if (arg >= argc || (frozen && arg + 1 >= argc))
        {
            std::cerr << "HospitalOutdoorInspect [--content core.json] report.html [seed ...]\n"
                         "HospitalOutdoorInspect [--content core.json] --frozen report.html envelope1.json [...]\n";
            return 2;
        }
        const auto &content = external ? *external : publishedContentRegistry();
        const char *output = argv[arg++];
        Json cases = Json::array();
        if (frozen)
        {
            for (; arg < argc; ++arg)
            {
                const std::filesystem::path source{argv[arg]};
                if (std::filesystem::exists(output) && std::filesystem::equivalent(output, source))
                    throw std::invalid_argument("报告输出与输入专档指向同一文件");
                cases.push_back(savedSample(content, source));
            }
        }
        std::vector<std::uint64_t> seeds;
        for (; arg < argc; ++arg)
        {
            const std::string_view token{argv[arg]};
            std::uint64_t seed{};
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), seed);
            if (error != std::errc{} || end != token.data() + token.size() || seed == 0)
                throw std::invalid_argument("种子应为大于零的无符号整数");
            seeds.push_back(seed);
        }
        if (!frozen && seeds.empty()) seeds = {1, 42, 910223};
        bool ok = true;
        for (auto seed : seeds)
        {
            cases.push_back(sample(content, seed));
            ok = ok && !cases.back().contains("error");
        }
        std::string data = cases.dump();
        for (std::size_t pos{}; (pos = data.find('<', pos)) != std::string::npos; pos += 6)
            data.replace(pos, 1, "\\u003c");
        std::ofstream out(output, std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << header << data << viewer;
        out.close();
        return ok ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
