#include "raid_noise_decoy_runtime.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x-b.x, a.y-b.y); }
}
std::optional<NoiseDecoyLanding> queryNoiseDecoyLanding(Vec2 origin, Vec2 aim,
    const RaidSpaceDefinitionId &space, const NoiseDecoyUseDefinition &use, const RaidSpaceBlockerIndex &index)
{
    if (!finite(origin) || !finite(aim) || !space.valid() || !std::isfinite(use.range) || use.range<=0) return std::nullopt;
    const float length=distance(origin,aim);
    if (!std::isfinite(length) || length<1.0F) return std::nullopt;
    const Vec2 direction{(aim.x-origin.x)/length,(aim.y-origin.y)/length};
    float limit=std::min(length,use.range);
    const auto size=index.worldSize();
    if (origin.x<4 || origin.y<4 || origin.x>size.x-4 || origin.y>size.y-4) return std::nullopt;
    if(direction.x>0)limit=std::min(limit,(size.x-4-origin.x)/direction.x);
    if(direction.x<0)limit=std::min(limit,(4-origin.x)/direction.x);
    if(direction.y>0)limit=std::min(limit,(size.y-4-origin.y)/direction.y);
    if(direction.y<0)limit=std::min(limit,(4-origin.y)/direction.y);
    auto point=[&](float d){return Vec2{origin.x+direction.x*d,origin.y+direction.y*d};};
    // LOS is monotone along this ray. Query the established indexed geometry,
    // then back off the first contact rather than inventing another wall set.
    if (!index.hasLineOfSight(origin,point(limit))) {
        float low=0, high=limit;
        for(int i=0;i<24;++i){const float middle=(low+high)*0.5F;
            if(index.hasLineOfSight(origin,point(middle)))low=middle;else high=middle;}
        limit=std::max(0.0F,low-4.0F);
    }
    if(limit<4 || !index.hasLineOfSight(origin,point(limit)))return std::nullopt;
    return NoiseDecoyLanding{origin,point(limit),space,use};
}

bool RaidNoiseDecoyRuntime::start(const NoiseDecoyLanding &landing)
{
    const auto &u=landing.use;
    if(flight_ || nextSource_==std::numeric_limits<std::uint64_t>::max() || !landing.space.valid() ||
        !finite(landing.origin) || !finite(landing.point) || !std::isfinite(u.flightSeconds) || u.flightSeconds<=0 ||
        !std::isfinite(u.durationSeconds) || u.durationSeconds<=0 || !std::isfinite(u.pulseIntervalSeconds) ||
        u.pulseIntervalSeconds<0.1F || u.durationSeconds/u.pulseIntervalSeconds>32 ||
        !std::isfinite(u.radius) || u.radius<=0 || u.radius>1000) return false;
    flight_=NoiseDecoyFlight{landing,0};return true;
}
void RaidNoiseDecoyRuntime::endSource()
{
    if(source_)for(auto &[id,goal]:investigations_)if(goal.source==source_->id)goal.returning=true;
    source_.reset();
}
void RaidNoiseDecoyRuntime::advanceSource(const RaidSpaceDefinitionId &space,double seconds)
{
    if(!source_ || source_->landing.space!=space)return;
    source_->elapsed+=seconds;
    const auto &u=source_->landing.use;
    while(source_->pulses*u.pulseIntervalSeconds<u.durationSeconds-0.00001 &&
          source_->pulses*u.pulseIntervalSeconds<=source_->elapsed+0.00001){++source_->pulses;++pulsesLastUpdate_;}
    if(source_->elapsed+0.00001>=u.durationSeconds)endSource();
}
void RaidNoiseDecoyRuntime::advance(const RaidSpaceDefinitionId &space,float seconds)
{
    pulsesLastUpdate_=0;candidatesLastUpdate_=0;
    if(!std::isfinite(seconds) || seconds<=0)return;
    double remainder=seconds;
    if(flight_ && flight_->landing.space==space) {
        const double untilLanding=std::max(0.0,flight_->landing.use.flightSeconds-flight_->elapsed);
        if(remainder+0.000001>=untilLanding) {
            advanceSource(space,untilLanding);remainder=std::max(0.0,remainder-untilLanding);
            endSource();source_=NoiseDecoySource{nextSource_++,flight_->landing,0,1};flight_.reset();++pulsesLastUpdate_;
        } else {flight_->elapsed+=remainder;advanceSource(space,remainder);return;}
    }
    advanceSource(space,remainder);
}
void RaidNoiseDecoyRuntime::hearPulses(std::span<const NoiseDecoyListener> listeners,const RaidSpaceBlockerIndex &index)
{
    if(!pulsesLastUpdate_ || !source_)return;
    const auto p=source_->landing.point;const float r=source_->landing.use.radius;
    const int firstX=static_cast<int>(std::floor((p.x-r)/128)), lastX=static_cast<int>(std::floor((p.x+r)/128));
    const int firstY=static_cast<int>(std::floor((p.y-r)/128)), lastY=static_cast<int>(std::floor((p.y+r)/128));
    const int columns=lastX-firstX+1, rows=lastY-firstY+1;
    if(columns<=0 || rows<=0 || columns*rows>400)return;
    // Counting-sort local cells into one flat buffer. Same broad phase as the
    // previous map, without per-cell tree nodes and tiny vector allocations.
    std::array<std::size_t,400> offsets{};
    struct Local {std::size_t cell;const NoiseDecoyListener *listener;};
    std::vector<Local> local;local.reserve(listeners.size());
    for(const auto &listener:listeners)if(listener.eligible && listener.id && finite(listener.center)){
        const int x=static_cast<int>(std::floor(listener.center.x/128)),y=static_cast<int>(std::floor(listener.center.y/128));
        if(x<firstX || x>lastX || y<firstY || y>lastY)continue;
        const auto cell=static_cast<std::size_t>((y-firstY)*columns+x-firstX);
        ++offsets[cell];local.push_back({cell,&listener});
    }
    std::size_t next{};
    for(auto &offset:offsets){const auto count=offset;offset=next;next+=count;}
    std::vector<const NoiseDecoyListener*> binned(local.size());
    for(const auto &entry:local)binned[offsets[entry.cell]++]=entry.listener;
    for(const auto *listener:binned){
        ++candidatesLastUpdate_;
        if(distance(p,listener->center)<=r+0.0001F && index.hasLineOfSight(p,listener->center))
            investigations_.insert_or_assign(listener->id,NoiseInvestigation{source_->landing.space,source_->id,p,listener->home,false});
    }
}
std::optional<Vec2> RaidNoiseDecoyRuntime::investigationGoal(CombatTargetId id,const RaidSpaceDefinitionId &space,Vec2 center)
{
    const auto it=investigations_.find(id);
    if(it==investigations_.end() || it->second.space!=space)return std::nullopt;
    const auto goal=it->second.returning ? it->second.home:it->second.point;
    if(it->second.returning && distance(goal,center)<=20){investigations_.erase(it);return std::nullopt;}
    // Hold inside the arrival band; navigation must not orbit the source.
    return distance(goal,center)<=20 ? center:goal;
}
void RaidNoiseDecoyRuntime::prune(const RaidSpaceDefinitionId &space,std::span<const CombatTargetId> alive)
{
    std::erase_if(investigations_,[&](const auto &entry){return entry.second.space==space &&
        std::find(alive.begin(),alive.end(),entry.first)==alive.end();});
}
void RaidNoiseDecoyRuntime::clear() noexcept
{flight_.reset();source_.reset();investigations_.clear();pulsesLastUpdate_=0;candidatesLastUpdate_=0;}
