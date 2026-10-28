#include "raid_space_query.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <array>
#include <queue>
#include <vector>

namespace
{
    constexpr float kGeometryEpsilon{0.001F};
    constexpr float kDistanceEpsilon{0.0001F};
    constexpr std::size_t kNoNode =
        std::numeric_limits<std::size_t>::max();
    constexpr std::size_t kDenseGridBlockerThreshold{48U};
    constexpr float kDenseNavigationGridCellSize{48.0F};
    constexpr int kDenseGridNeighborOffsets[4][2]{
        {0, -1}, {-1, 0}, {1, 0}, {0, 1}};

    bool finite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool finitePositive(Vec2 value) noexcept
    {
        return finite(value) && value.x > 0.0F && value.y > 0.0F;
    }

    // Finite binary32 values have one representation per numeric value,
    // except signed zero. Preserve the original comparison sort whenever
    // equal values could have different bits, or denormal comparison could
    // depend on the caller's floating-point mode. The template keeps the
    // unsupported-representation fallback well formed on other targets.
    template <typename Float>
    void sortConeAngles(std::vector<Float> &angles)
    {
        if constexpr (sizeof(Float) != sizeof(std::uint32_t) ||
                      !std::numeric_limits<Float>::is_iec559 ||
                      std::numeric_limits<Float>::radix != 2 ||
                      std::numeric_limits<Float>::digits != 24 ||
                      std::numeric_limits<Float>::max_exponent != 128)
        {
            std::sort(angles.begin(), angles.end());
        }
        else
        {
            auto *const values = angles.data();
            const auto count = angles.size();
            if (count < 256)
            {
                std::sort(values, values + count);
                return;
            }
            std::vector<std::uint32_t> keys(count), scratch(count);
            auto *source = keys.data();
            auto *destination = scratch.data();
            std::array<std::size_t, 4 * 256> histogram{};
            auto *const bins = histogram.data();
            unsigned zeroSigns{};
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto bits = std::bit_cast<std::uint32_t>(values[i]);
                const auto magnitude = bits & 0x7fffffffU;
                const auto exponent = magnitude & 0x7f800000U;
                if (magnitude == 0)
                    zeroSigns |= (bits & 0x80000000U) ? 2U : 1U;
                if (exponent == 0x7f800000U ||
                    (exponent == 0 && magnitude != 0) || zeroSigns == 3)
                {
                    std::sort(values, values + count);
                    return;
                }
                const auto key = bits & 0x80000000U ? ~bits : bits ^ 0x80000000U;
                source[i] = key;
                ++bins[key & 255U];
                ++bins[256 + ((key >> 8) & 255U)];
                ++bins[512 + ((key >> 16) & 255U)];
                ++bins[768 + (key >> 24)];
            }
            // Every buffer already has live elements. Four stable byte
            // passes produce the same bit sequence as float < for eligible
            // values, including a group containing only one sign of zero.
            for (unsigned pass = 0; pass < 4; ++pass)
            {
                auto *const offsets = bins + pass * 256;
                std::size_t sum{};
                for (std::size_t bin = 0; bin < 256; ++bin)
                {
                    const auto size = offsets[bin];
                    offsets[bin] = sum;
                    sum += size;
                }
                const auto shift = pass * 8;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto key = source[i];
                    destination[offsets[(key >> shift) & 255U]++] = key;
                }
                std::swap(source, destination);
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto key = source[i];
                const auto bits = key & 0x80000000U ? key ^ 0x80000000U : ~key;
                values[i] = std::bit_cast<Float>(bits);
            }
        }
    }

    // The original X-then-Y slabs, with each ray's absolute direction reused.
    // The entry distance is independent of the initial leave bound; a hit
    // still requires the original leave > entry + epsilon predicate.
    bool castConeRay(const Rect &rect, const Vec2 &origin, const Vec2 &direction,
                     float absoluteX, float absoluteY, float maximum,
                     float &hitDistance) noexcept
    {
        float enter = 0, leave = maximum;
        if (absoluteX < 1e-7F)
        {
            if (!(origin.x >= rect.position.x && origin.x <= rect.position.x + rect.size.x))
                return false;
        }
        else
        {
            float a = (rect.position.x - origin.x) / direction.x,
                  b = (rect.position.x + rect.size.x - origin.x) / direction.x;
            if (a > b) { const float temporary = a; a = b; b = temporary; }
            if (enter < a) enter = a;
            if (b < leave) leave = b;
            if (!(enter <= leave)) return false;
        }
        if (absoluteY < 1e-7F)
        {
            if (!(origin.y >= rect.position.y && origin.y <= rect.position.y + rect.size.y))
                return false;
        }
        else
        {
            float a = (rect.position.y - origin.y) / direction.y,
                  b = (rect.position.y + rect.size.y - origin.y) / direction.y;
            if (a > b) { const float temporary = a; a = b; b = temporary; }
            if (enter < a) enter = a;
            if (b < leave) leave = b;
            if (!(enter <= leave)) return false;
        }
        if (!(enter >= 0 && enter <= maximum && leave > enter + 0.00001F))
            return false;
        hitDistance = enter;
        return true;
    }

    float distance(Vec2 first, Vec2 second) noexcept
    {
        const float deltaX = second.x - first.x;
        const float deltaY = second.y - first.y;
        const float squared = deltaX * deltaX + deltaY * deltaY;
        return std::isfinite(squared)
            ? std::sqrt(squared)
            : std::numeric_limits<float>::infinity();
    }

    bool pointInsideOpenRect(Vec2 point, const Rect &rect) noexcept
    {
        return point.x > rect.position.x + kGeometryEpsilon &&
               point.x < rect.position.x + rect.size.x - kGeometryEpsilon &&
               point.y > rect.position.y + kGeometryEpsilon &&
               point.y < rect.position.y + rect.size.y - kGeometryEpsilon;
    }

    bool segmentIntersectsRectInterior(
        Vec2 start,
        Vec2 end,
        const Rect &rect) noexcept
    {
        return raidSightSegmentIntersectsRectInterior(start,end,rect);
    }

    Rect expandedForActor(
        const Rect &rect,
        Vec2 actorSize,
        float clearance) noexcept
    {
        const Vec2 expansion{
            actorSize.x * 0.5F + clearance,
            actorSize.y * 0.5F + clearance};
        return Rect{
            Vec2{
                rect.position.x - expansion.x,
                rect.position.y - expansion.y},
            Vec2{
                rect.size.x + expansion.x * 2.0F,
                rect.size.y + expansion.y * 2.0F}};
    }

    bool pointInsideWorldForActor(
        Vec2 point,
        Vec2 actorSize,
        Vec2 worldSize) noexcept
    {
        const Vec2 half{actorSize.x * 0.5F, actorSize.y * 0.5F};
        return point.x >= half.x && point.y >= half.y &&
               point.x <= worldSize.x - half.x &&
               point.y <= worldSize.y - half.y;
    }

}

RaidVisibilityProjection
buildRaidSpaceVisibility(const RaidSpaceBlockerIndex &index,
                         const RaidSpaceDefinitionId &space, Vec2 observer,
                         std::size_t *blockerTests)
{
    if (blockerTests)
        *blockerTests = 0;
    RaidVisibilityProjection result;
    result.spaceId = space;
    result.observer = observer;
    result.worldSize = index.worldSize();
    if (!finite(observer) || observer.x < 0 || observer.y < 0 ||
        observer.x > result.worldSize.x || observer.y > result.worldSize.y)
        return result;
    for (std::size_t i = 0; i < index.blockerCount(); ++i)
        if (pointInsideOpenRect(observer, index.blockerBounds(i)))
            return result;
    std::vector<float> angles;
    const auto corner = [&](Vec2 p)
    {
        const float angle = std::atan2(p.y - observer.y, p.x - observer.x);
        for (float offset : {-0.00001F, 0.0F, 0.00001F})
            angles.push_back(
                std::atan2(std::sin(angle + offset), std::cos(angle + offset)));
    };
    corner({0, 0});
    corner({result.worldSize.x, 0});
    corner(result.worldSize);
    corner({0, result.worldSize.y});
    for (std::size_t i = 0; i < index.blockerCount(); ++i)
    {
        const auto r = index.blockerBounds(i);
        corner(r.position);
        corner({r.position.x + r.size.x, r.position.y});
        corner({r.position.x + r.size.x, r.position.y + r.size.y});
        corner({r.position.x, r.position.y + r.size.y});
    }
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(), [](float a, float b)
                             { return std::abs(a - b) < 0.0000001F; }),
                 angles.end());
    std::vector<std::size_t> candidates;
    for (float angle : angles)
    {
        const Vec2 direction{std::cos(angle), std::sin(angle)};
        float distance = std::numeric_limits<float>::infinity();
        if (direction.x > 0.0000001F)
            distance = std::min(distance, (result.worldSize.x - observer.x) /
                                              direction.x);
        else if (direction.x < -0.0000001F)
            distance = std::min(distance, -observer.x / direction.x);
        if (direction.y > 0.0000001F)
            distance = std::min(distance, (result.worldSize.y - observer.y) /
                                              direction.y);
        else if (direction.y < -0.0000001F)
            distance = std::min(distance, -observer.y / direction.y);
        Vec2 end{observer.x + direction.x * distance,
                 observer.y + direction.y * distance};
        index.queryCandidateIndices(
            {{std::min(observer.x, end.x), std::min(observer.y, end.y)},
             {std::abs(end.x - observer.x), std::abs(end.y - observer.y)}},
            candidates);
        for (auto candidate : candidates)
        {
            if (blockerTests)
                ++*blockerTests;
            const auto r = index.blockerBounds(candidate);
            float enter = 0, leave = distance;
            const auto axis =
                [&](float origin, float movement, float low, float high)
            {
                if (std::abs(movement) < 0.0000001F)
                    return origin >= low && origin <= high;
                float a = (low - origin) / movement,
                      b = (high - origin) / movement;
                if (a > b)
                    std::swap(a, b);
                enter = std::max(enter, a);
                leave = std::min(leave, b);
                return enter <= leave;
            };
            if (axis(observer.x, direction.x, r.position.x,
                     r.position.x + r.size.x) &&
                axis(observer.y, direction.y, r.position.y,
                     r.position.y + r.size.y) &&
                enter > 0 && enter <= distance && leave >= enter)
                distance = enter;
        }
        result.boundary.push_back(
            {std::clamp(observer.x + direction.x * distance, 0.0F,
                        result.worldSize.x),
             std::clamp(observer.y + direction.y * distance, 0.0F,
                        result.worldSize.y)});
    }
    result.geometryRevision = 1;
    result.valid = result.boundary.size() >= 3;
    return result;
}

RaidVisibilityProjection
buildRaidSpaceConeVisibility(const RaidSpaceBlockerIndex &index,
                             const RaidObservationKey &key,
                             RaidObservationWork *work)
{
    if (work)
        *work = {};
    RaidVisibilityProjection result;
    result.spaceId = key.spaceId;
    result.observer = key.observer;
    result.worldSize = index.worldSize();
    result.geometryRevision = key.geometryRevision;
    result.direction = key.direction;
    result.angleDegrees = key.angleDegrees;
    result.maximumDistance = key.maximumDistance;
    result.environment = key.environment;
    const auto o = key.observer;
    result.lightAssetId = key.lightAssetId; result.lightBatteryId = key.lightBatteryId;
    result.lightAngleDegrees = key.lightAngleDegrees; result.lightDistance = key.lightDistance;
    const float radius = std::max(key.maximumDistance, key.lightDistance);
    if (!finite(o) || !finite(key.direction) || !std::isfinite(radius) ||
        radius <= 0 || !std::isfinite(key.maximumDistance) || key.maximumDistance <= 0 || !std::isfinite(key.angleDegrees) ||
        key.angleDegrees <= 0 || key.angleDegrees > 180 ||
        std::abs(std::hypot(key.direction.x, key.direction.y) - 1) > 0.001F ||
        o.x < 0 || o.y < 0 || o.x > result.worldSize.x ||
        o.y > result.worldSize.y || key.geometryRevision == 0)
        return result;
    if (!std::isfinite(key.lightDistance) || !std::isfinite(key.lightAngleDegrees) ||
        (key.lightAssetId ? (!key.lightBatteryId || key.lightAngleDegrees <= 0 || key.lightAngleDegrees > 180 || key.lightDistance <= 0 || key.lightDistance > 1500) :
                            (key.lightBatteryId || key.lightAngleDegrees != 0 || key.lightDistance != 0))) return result;
    std::vector<std::size_t> candidates;
    index.queryCandidateIndices(
        {{o.x - radius, o.y - radius}, {radius * 2, radius * 2}}, candidates);
    const auto *const blockerData = index.allBlockerBounds().data();
    std::erase_if(candidates,
                  [&](auto i)
                  {
                      const auto r = blockerData[i];
                      const float x = std::clamp(o.x, r.position.x,
                                                 r.position.x + r.size.x);
                      const float y = std::clamp(o.y, r.position.y,
                                                 r.position.y + r.size.y);
                      return std::hypot(o.x - x, o.y - y) > radius;
                  });
    if (work)
        work->localBlockers = candidates.size();
    for (auto i : candidates)
        if (pointInsideOpenRect(o, blockerData[i]))
            return result;
    constexpr float pi = 3.14159265358979323846F;
    const float axisAngle = std::atan2(key.direction.y, key.direction.x);
    const float ambientHalf = key.angleDegrees * pi / 360;
    const float lightHalf = key.lightAngleDegrees * pi / 360;
    const float half = std::max(ambientHalf, lightHalf);
    const auto radiusAt = [&](float angle) {
        const float ambient = std::abs(angle) <= ambientHalf + 0.0000001F ? key.maximumDistance : 0.0F;
        const float lamp = key.lightAssetId && std::abs(angle) <= lightHalf + 0.0000001F ? key.lightDistance : 0.0F;
        return ambient < lamp ? lamp : ambient;
    };
    // At most one world unit of inward arc error, never revealing beyond range.
    const int arcSegments =
        std::max(2, static_cast<int>(std::ceil(half * 2 / 0.035F)));
    // One arc, four optional light seams, and three rays per world/blocker
    // corner. Establish valid size before borrowing writable storage.
    std::vector<float> angles(static_cast<std::size_t>(arcSegments) + 1 +
        (key.lightAssetId ? 4 : 0) + 12 * (candidates.size() + 1));
    auto *angleWrite = angles.data();
    for (int n = 0; n <= arcSegments; ++n)
        *angleWrite++ = -half + half * 2 * n / arcSegments;
    if (key.lightAssetId)
        for (float edge : {-ambientHalf, ambientHalf, -lightHalf, lightHalf}) *angleWrite++ = edge;
    const auto corner = [&](Vec2 p)
    {
        const float angle = std::atan2(p.y - o.y, p.x - o.x) - axisAngle;
        const float local = std::atan2(std::sin(angle), std::cos(angle));
        for (float offset : {-0.00001F, 0.0F, 0.00001F})
            if (local + offset >= -half && local + offset <= half)
                *angleWrite++ = local + offset;
    };
    corner({0, 0});
    corner({result.worldSize.x, 0});
    corner(result.worldSize);
    corner({0, result.worldSize.y});
    for (auto i : candidates)
    {
        const auto r = blockerData[i];
        corner(r.position);
        corner({r.position.x + r.size.x, r.position.y});
        corner({r.position.x + r.size.x, r.position.y + r.size.y});
        corner({r.position.x, r.position.y + r.size.y});
    }
    angles.resize(static_cast<std::size_t>(angleWrite - angles.data()));
    if (key.lightAssetId)
        for (auto &angle : angles)
            for (float seam : {ambientHalf,lightHalf})
                if (std::abs(std::abs(angle)-seam)<=0.000002F) angle=std::copysign(seam,angle);
    sortConeAngles(angles);
    auto *const angleData = angles.data();
    const auto *const uniqueEnd = std::unique(angleData, angleData + angles.size(),
        [](float a, float b) { return std::abs(a - b) < 1e-7F; });
    angles.resize(static_cast<std::size_t>(uniqueEnd - angleData));
    // Only a powered light union can emit two endpoints for one angle.
    // Copy one initialized value to establish live points before bulk writes.
    result.boundary.assign((key.lightAssetId ? 2 : 1) * angles.size() + 1, Vec2{});
    auto *boundaryWrite = result.boundary.data();
    // Keep the original corner rays and original-index slab ordering. A ray
    // can only hit local blockers overlapping its closed segment bounds.
    // Borrow the immutable bounds once and use a closed broad-phase test
    // before the original narrow phase, without per-ray allocation or sorting.
    struct ConeBlocker
    {
        Rect bounds;
        Vec2 relativeCenter, halfExtent;
        float linePadding;
    };
    std::vector<ConeBlocker> localBounds;
    localBounds.reserve(candidates.size());
    for (auto i : candidates)
    {
        const auto r = blockerData[i];
        const Vec2 halfExtent{r.size.x * 0.5F, r.size.y * 0.5F};
        const Vec2 relativeCenter{r.position.x + halfExtent.x - o.x,
                                  r.position.y + halfExtent.y - o.y};
        // Conservative roundoff allowance for a line touching a corner. This
        // only admits extra narrow-phase candidates, never grows visibility.
        const float padding = 8 * std::numeric_limits<float>::epsilon() *
            (std::abs(o.x) + std::abs(o.y) + std::abs(relativeCenter.x) +
             std::abs(relativeCenter.y) + halfExtent.x + halfExtent.y + 1);
        localBounds.push_back({r, relativeCenter, halfExtent, padding});
    }
    // Conservative angular buckets avoid scanning every local rectangle for
    // every ray. Each bounding circle covers its whole rectangle; padded bin
    // ranges may admit extras. Insertion keeps the original blocker order.
    constexpr std::size_t bucketCount = 512;
    struct BucketRange { std::size_t candidate, first, last; };
    std::vector<BucketRange> bucketRanges;
    bucketRanges.reserve(localBounds.size() * 2);
    std::array<std::size_t, bucketCount> bucketSizes{};
    auto *const bucketSizeData = bucketSizes.data();
    const auto addRange = [&](std::size_t candidate, std::size_t first, std::size_t last)
    {
        bucketRanges.push_back({candidate, first, last});
        for (std::size_t bin = first; bin <= last; ++bin) ++bucketSizeData[bin];
    };
    const auto bucketFor = [&](float angle)
    {
        if (half <= 0) return std::size_t{0};
        const float raw = (angle + half) * static_cast<float>(bucketCount) / (half * 2);
        const float last = static_cast<float>(bucketCount - 1);
        return static_cast<std::size_t>(raw < 0.0F ? 0.0F : last < raw ? last : raw);
    };
    const auto *const localData = localBounds.data();
    for (std::size_t i = 0; i < localBounds.size(); ++i)
    {
        const auto &b = localData[i];
        const float centerDistance = std::hypot(b.relativeCenter.x, b.relativeCenter.y);
        const float circleRadius = std::hypot(b.halfExtent.x, b.halfExtent.y) + b.linePadding;
        if (half <= 0 || !std::isfinite(circleRadius) || circleRadius >= centerDistance)
        {
            addRange(i, 0, bucketCount - 1);
            continue;
        }
        const float delta = std::atan2(b.relativeCenter.y, b.relativeCenter.x) - axisAngle;
        const float centerAngle = std::atan2(std::sin(delta), std::cos(delta));
        const float angularRadius = std::asin(std::clamp(circleRadius / centerDistance, 0.0F, 1.0F));
        for (float wrap : {-2 * pi, 0.0F, 2 * pi})
        {
            const float low = centerAngle + wrap - angularRadius;
            const float high = centerAngle + wrap + angularRadius;
            if (low > half || high < -half) continue;
            const auto first = bucketFor(std::max(low, -half));
            const auto last = bucketFor(std::min(high, half));
            const auto begin = first > 0 ? first - 1 : 0;
            const auto end = std::min(last + 1, bucketCount - 1);
            addRange(i, begin, end);
        }
    }
    std::array<std::size_t, bucketCount + 1> bucketOffsets{};
    auto *const bucketOffsetData = bucketOffsets.data();
    for (std::size_t bin = 0; bin < bucketCount; ++bin)
        bucketOffsetData[bin + 1] = bucketOffsetData[bin] + bucketSizeData[bin];
    std::vector<std::size_t> angularCandidates(bucketOffsets.back());
    auto writeOffsets = bucketOffsets;
    auto *const writeOffsetData = writeOffsets.data();
    auto *const angularData = angularCandidates.data();
    for (const auto range : bucketRanges)
        for (std::size_t bin = range.first; bin <= range.last; ++bin)
            angularData[writeOffsetData[bin]++] = range.candidate;
    // A preview only bounds the broad phase, never the ordered ray result.
    // The original epsilon can transmit a different accept/reject decision
    // through a chain of near-equal hits. Budget one worst-case addition gap
    // per candidate, using the whole radius as a uniform float-ULP bound.
    // The extra ULP and outward rounding retain uncertain boundary cases.
    const float hitEpsilon = 0.00001F;
    const float additionCap = radius + hitEpsilon;
    const float nextAddition = std::nextafter(additionCap, std::numeric_limits<float>::infinity());
    const bool previewBudgetValid = std::isfinite(additionCap) && std::isfinite(nextAddition);
    const double candidateSlack = previewBudgetValid ? static_cast<double>(hitEpsilon) +
        2 * (static_cast<double>(nextAddition) - additionCap) : 0.0;
    const auto *const lastAngle = angles.data() + angles.size();
    for (auto angleCursor = angles.data(); angleCursor != lastAngle; ++angleCursor)
    {
        const float angle = *angleCursor;
        const Vec2 d{std::cos(axisAngle + angle), std::sin(axisAngle + angle)};
        std::array<float,2> limits{radiusAt(angle),0};
        std::size_t limitCount=1;
        if (key.lightAssetId && (std::abs(std::abs(angle)-ambientHalf)<1e-7F || std::abs(std::abs(angle)-lightHalf)<1e-7F) && std::abs(angle)<half-1e-7F)
        {
            limits={radiusAt(angle-0.000001F),radiusAt(angle+0.000001F)};
            if (limits[0]!=limits[1]) limitCount=2;
        }
        for (std::size_t limitIndex=0; limitIndex<limitCount; ++limitIndex)
        {
        if (work) ++work->rays;
        float length = limits[limitIndex];
        if (d.x > 1e-7F)
        {
            const float cap = (result.worldSize.x - o.x) / d.x;
            if (cap < length) length = cap;
        }
        else if (d.x < -1e-7F)
        {
            const float cap = -o.x / d.x;
            if (cap < length) length = cap;
        }
        if (d.y > 1e-7F)
        {
            const float cap = (result.worldSize.y - o.y) / d.y;
            if (cap < length) length = cap;
        }
        else if (d.y < -1e-7F)
        {
            const float cap = -o.y / d.y;
            if (cap < length) length = cap;
        }
        const Vec2 end{o.x + d.x * length, o.y + d.y * length};
        float left = end.x < o.x ? end.x : o.x, right = o.x < end.x ? end.x : o.x;
        float top = end.y < o.y ? end.y : o.y, bottom = o.y < end.y ? end.y : o.y;
        const float absoluteX = std::abs(d.x), absoluteY = std::abs(d.y);
        const auto bin = bucketFor(angle);
        const auto candidateCount = bucketOffsetData[bin + 1] - bucketOffsetData[bin];
        const auto *const firstCandidate = candidateCount ? angularData + bucketOffsetData[bin] : nullptr;
        auto previewCursor = candidateCount ? firstCandidate + candidateCount : nullptr;
        bool previewFound = false;
        // A reverse coarse scan finds one useful actual slab to preview.
        // Later entries proven outside the original segment need no second
        // scan. All surviving decisions are consumed in original order.
        while (previewCursor != firstCandidate)
        {
            --previewCursor;
            const auto &candidate = localData[*previewCursor];
            const auto &r = candidate.bounds;
            const float pad = candidate.linePadding;
            if (r.position.x + r.size.x < left - pad || r.position.x > right + pad ||
                r.position.y + r.size.y < top - pad || r.position.y > bottom + pad) continue;
            const float cross = d.x * candidate.relativeCenter.y - d.y * candidate.relativeCenter.x;
            const float extent = absoluteX * candidate.halfExtent.y + absoluteY * candidate.halfExtent.x;
            if (std::abs(cross) > extent + pad) continue;
            previewFound = true;
            break;
        }
        const auto *const lastCandidate = previewFound ? previewCursor + 1 : firstCandidate;
        float previewEntry{}, previewThreshold{}, coarseLength = length;
        bool previewHit = false;
        if (previewFound)
        {
            if (work) ++work->blockerTests;
            previewHit = castConeRay(localData[*previewCursor].bounds, o, d,
                                     absoluteX, absoluteY, length, previewEntry);
            if (previewHit)
            {
                previewThreshold = previewEntry + hitEpsilon;
                // A successful preview has finite 0 <= entry < threshold
                // (or threshold==entry after rounding) < leave <= length.
                // The positive bounded budget cannot produce NaN; an upper
                // bound outside this segment simply leaves the old scan.
                if (previewBudgetValid)
                {
                    const double limit = static_cast<double>(previewThreshold) +
                        static_cast<double>(candidateCount) * candidateSlack;
                    if (limit < static_cast<double>(length))
                    {
                        const float cap = std::nextafter(static_cast<float>(limit),
                                                        std::numeric_limits<float>::infinity());
                        if (cap < length)
                        {
                            coarseLength = cap;
                            const Vec2 closestEnd{o.x + d.x * coarseLength, o.y + d.y * coarseLength};
                            left = closestEnd.x < o.x ? closestEnd.x : o.x;
                            right = o.x < closestEnd.x ? closestEnd.x : o.x;
                            top = closestEnd.y < o.y ? closestEnd.y : o.y;
                            bottom = o.y < closestEnd.y ? closestEnd.y : o.y;
                        }
                    }
                }
            }
        }
        for (auto cursor = firstCandidate; cursor != lastCandidate; ++cursor)
        {
            if (cursor == previewCursor)
            {
                // Preview leave was min(L0, both axis exits) and exceeded
                // T=entry+epsilon. With L<=L0 the original acceptance is
                // exactly L>T. A miss at L0 cannot become a hit at smaller L.
                // This is current-ray reuse only; count the actual slab once.
                if (previewHit && length > previewThreshold) length = previewEntry;
                continue;
            }
            const auto &candidate = localData[*cursor];
            const auto &r = candidate.bounds;
            const float pad = candidate.linePadding;
            if (r.position.x + r.size.x < left - pad || r.position.x > right + pad ||
                r.position.y + r.size.y < top - pad || r.position.y > bottom + pad) continue;
            const float cross = d.x * candidate.relativeCenter.y -
                                d.y * candidate.relativeCenter.x;
            const float extent = absoluteX * candidate.halfExtent.y +
                                 absoluteY * candidate.halfExtent.x;
            if (std::abs(cross) > extent + pad) continue;
            if (work)
                ++work->blockerTests;
            float enter{};
            if (castConeRay(r, o, d, absoluteX, absoluteY, length, enter))
            {
                length = enter;
                // A nearer hit shortens the remaining closed segment. Keep
                // the original per-blocker padding and candidate order; this
                // only skips later slabs beyond the current closest hit.
                if (length < coarseLength) coarseLength = length;
                const Vec2 closestEnd{o.x + d.x * coarseLength, o.y + d.y * coarseLength};
                left = closestEnd.x < o.x ? closestEnd.x : o.x;
                right = o.x < closestEnd.x ? closestEnd.x : o.x;
                top = closestEnd.y < o.y ? closestEnd.y : o.y;
                bottom = o.y < closestEnd.y ? closestEnd.y : o.y;
            }
        }
        const float x = o.x + d.x * length, y = o.y + d.y * length;
        *boundaryWrite++ =
            {x < 0.0F ? 0.0F : result.worldSize.x < x ? result.worldSize.x : x,
             y < 0.0F ? 0.0F : result.worldSize.y < y ? result.worldSize.y : y};
        }
    }
    *boundaryWrite++ = o; // Close both sector edges at the original apex.
    result.boundary.resize(static_cast<std::size_t>(boundaryWrite - result.boundary.data()));
    result.valid = result.boundary.size() >= 3;
    return result;
}

bool raidSpaceHasLineOfSight(
    Vec2 start,
    Vec2 end,
    std::span<const BallisticBlocker> blockers) noexcept
{
    if (!finite(start) || !finite(end))
    {
        return false;
    }

    return std::none_of(
        blockers.begin(),
        blockers.end(),
            [&](const BallisticBlocker &blocker)
            {
                return raidSpaceSegmentIntersectsBlockerInterior(
                    start, end, blocker.bounds);
            });
}

bool raidSpaceSegmentIntersectsBlockerInterior(
    Vec2 start,
    Vec2 end,
    Rect blocker) noexcept
{
    return segmentIntersectsRectInterior(start, end, blocker);
}

std::optional<RaidSpaceNavigationField>
RaidSpaceNavigationField::build(
    Vec2 actorSize,
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers,
    float clearance)
{
    if (!finitePositive(actorSize) || !finitePositive(worldSize) ||
        !std::isfinite(clearance) || clearance < 0.0F)
    {
        return std::nullopt;
    }

    RaidSpaceNavigationField field;
    field.actorSize_ = actorSize;
    field.worldSize_ = worldSize;
    field.clearance_ = clearance;
    field.expandedBlockers_.reserve(blockers.size());
    for (const BallisticBlocker &blocker : blockers)
    {
        if (!finite(blocker.bounds.position) ||
            !finitePositive(blocker.bounds.size))
        {
            return std::nullopt;
        }
        field.expandedBlockers_.push_back(
            expandedForActor(blocker.bounds, actorSize, clearance));
    }
    std::vector<BallisticBlocker> expandedBlockers;
    expandedBlockers.reserve(field.expandedBlockers_.size());
    for (std::size_t index{}; index < field.expandedBlockers_.size(); ++index)
    {
        expandedBlockers.push_back(BallisticBlocker{
            static_cast<std::uint32_t>(index + 1U),
            field.expandedBlockers_[index]});
    }
    field.expandedBlockerIndex_ = RaidSpaceBlockerIndex::build(
        worldSize,
        expandedBlockers);
    if (!field.expandedBlockerIndex_.has_value())
    {
        return std::nullopt;
    }

    if (field.expandedBlockers_.size() >= kDenseGridBlockerThreshold)
    {
        const Vec2 halfActor{actorSize.x * 0.5F, actorSize.y * 0.5F};
        const Vec2 navigableSize{
            worldSize.x - actorSize.x,
            worldSize.y - actorSize.y};
        if (navigableSize.x < 0.0F || navigableSize.y < 0.0F)
        {
            return std::nullopt;
        }
        field.usesDenseGrid_ = true;
        field.denseGridCellSize_ = kDenseNavigationGridCellSize;
        field.denseGridOrigin_ = halfActor;
        field.denseGridColumns_ = static_cast<std::size_t>(
            std::floor(navigableSize.x / kDenseNavigationGridCellSize)) + 1U;
        field.denseGridRows_ = static_cast<std::size_t>(
            std::floor(navigableSize.y / kDenseNavigationGridCellSize)) + 1U;
        const std::size_t gridSize =
            field.denseGridColumns_ * field.denseGridRows_;
        field.denseGridWalkable_.assign(gridSize, 1U);
        std::vector<std::size_t> blockerCandidates;
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{};
                 column < field.denseGridColumns_;
                 ++column)
            {
                const Vec2 point{
                    field.denseGridOrigin_.x +
                        static_cast<float>(column) *
                            field.denseGridCellSize_,
                    field.denseGridOrigin_.y +
                        static_cast<float>(row) *
                            field.denseGridCellSize_};
                field.expandedBlockerIndex_->queryCandidateIndices(
                    Rect{point, {0.0F, 0.0F}}, blockerCandidates);
                const bool blocked = std::any_of(
                    blockerCandidates.begin(),
                    blockerCandidates.end(),
                    [&](std::size_t index)
                    {
                        return pointInsideOpenRect(
                            point, field.expandedBlockers_[index]);
                    });
                field.denseGridWalkable_[
                    row * field.denseGridColumns_ + column] =
                    blocked ? 0U : 1U;
            }
        }
        field.denseGridConnections_.assign(gridSize, 0U);
        const auto cellPosition = [&](std::size_t row, std::size_t column)
        {
            return Vec2{
                field.denseGridOrigin_.x +
                    static_cast<float>(column) * field.denseGridCellSize_,
                field.denseGridOrigin_.y +
                    static_cast<float>(row) * field.denseGridCellSize_};
        };
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{};
                 column < field.denseGridColumns_;
                 ++column)
            {
                const std::size_t current =
                    row * field.denseGridColumns_ + column;
                if (field.denseGridWalkable_[current] == 0U)
                {
                    continue;
                }
                for (std::size_t direction{};
                     direction < std::size(kDenseGridNeighborOffsets);
                     ++direction)
                {
                    const int nextColumn = static_cast<int>(column) +
                        kDenseGridNeighborOffsets[direction][0];
                    const int nextRow = static_cast<int>(row) +
                        kDenseGridNeighborOffsets[direction][1];
                    if (nextColumn < 0 || nextRow < 0 ||
                        nextColumn >=
                            static_cast<int>(field.denseGridColumns_) ||
                        nextRow >= static_cast<int>(field.denseGridRows_))
                    {
                        continue;
                    }
                    const std::size_t next =
                        static_cast<std::size_t>(nextRow) *
                            field.denseGridColumns_ +
                        static_cast<std::size_t>(nextColumn);
                    if (field.denseGridWalkable_[next] != 0U &&
                        field.expandedBlockerIndex_->hasLineOfSight(
                            cellPosition(row, column),
                            cellPosition(
                                static_cast<std::size_t>(nextRow),
                                static_cast<std::size_t>(nextColumn))))
                    {
                        field.denseGridConnections_[current] |=
                            static_cast<std::uint8_t>(1U << direction);
                    }
                }
            }
        }
        field.portalChunkSizeCells_ = 16U;
        field.portalChunkColumns_ =
            (field.denseGridColumns_ + field.portalChunkSizeCells_ - 1U) /
            field.portalChunkSizeCells_;
        field.portalChunkRows_ =
            (field.denseGridRows_ + field.portalChunkSizeCells_ - 1U) /
            field.portalChunkSizeCells_;
        field.portalChunkConnections_.assign(
            field.portalChunkColumns_ * field.portalChunkRows_, 0U);
        for (std::size_t row{}; row < field.denseGridRows_; ++row)
        {
            for (std::size_t column{}; column < field.denseGridColumns_;
                 ++column)
            {
                const std::size_t current =
                    row * field.denseGridColumns_ + column;
                const std::size_t chunkRow =
                    row / field.portalChunkSizeCells_;
                const std::size_t chunkColumn =
                    column / field.portalChunkSizeCells_;
                const std::size_t chunk =
                    chunkRow * field.portalChunkColumns_ + chunkColumn;
                for (std::size_t direction{};
                     direction < std::size(kDenseGridNeighborOffsets);
                     ++direction)
                {
                    if ((field.denseGridConnections_[current] &
                         static_cast<std::uint8_t>(1U << direction)) == 0U)
                        continue;
                    const int nextColumn = static_cast<int>(column) +
                        kDenseGridNeighborOffsets[direction][0];
                    const int nextRow = static_cast<int>(row) +
                        kDenseGridNeighborOffsets[direction][1];
                    if (nextColumn < 0 || nextRow < 0)
                        continue;
                    const std::size_t nextChunkColumn =
                        static_cast<std::size_t>(nextColumn) /
                        field.portalChunkSizeCells_;
                    const std::size_t nextChunkRow =
                        static_cast<std::size_t>(nextRow) /
                        field.portalChunkSizeCells_;
                    if (nextChunkColumn != chunkColumn ||
                        nextChunkRow != chunkRow)
                        field.portalChunkConnections_[chunk] |=
                            static_cast<std::uint8_t>(1U << direction);
                }
            }
        }
        return field;
    }

    field.cornerNodes_.reserve(field.expandedBlockers_.size() * 4U);
    for (const Rect &blocker : field.expandedBlockers_)
    {
        const float left = blocker.position.x;
        const float right = blocker.position.x + blocker.size.x;
        const float top = blocker.position.y;
        const float bottom = blocker.position.y + blocker.size.y;
        const Vec2 corners[]{
            Vec2{left, top},
            Vec2{right, top},
            Vec2{right, bottom},
            Vec2{left, bottom}};
        for (const Vec2 corner : corners)
        {
            if (!pointInsideWorldForActor(corner, actorSize, worldSize) ||
                std::any_of(
                    field.expandedBlockers_.begin(),
                    field.expandedBlockers_.end(),
                    [&](const Rect &other)
                    {
                        return pointInsideOpenRect(corner, other);
                    }))
            {
                continue;
            }
            field.cornerNodes_.push_back(corner);
        }
    }

    const std::size_t cornerCount = field.cornerNodes_.size();
    const float infinity = std::numeric_limits<float>::infinity();
    field.cornerEdges_.assign(cornerCount * cornerCount, infinity);
    for (std::size_t first{}; first < cornerCount; ++first)
    {
        field.cornerEdges_[first * cornerCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < cornerCount;
             ++second)
        {
            if (!field.expandedBlockerIndex_->hasLineOfSight(
                    field.cornerNodes_[first],
                    field.cornerNodes_[second]))
            {
                continue;
            }
            const float edgeDistance = distance(
                field.cornerNodes_[first],
                field.cornerNodes_[second]);
            if (!std::isfinite(edgeDistance) ||
                edgeDistance <= kDistanceEpsilon)
            {
                continue;
            }
            field.cornerEdges_[first * cornerCount + second] = edgeDistance;
            field.cornerEdges_[second * cornerCount + first] = edgeDistance;
        }
    }
    return field;
}

std::optional<Vec2> RaidSpaceNavigationField::nextWaypoint(
    Vec2 start,
    Vec2 goal,
    float goalTolerance) const
{
    if (!expandedBlockerIndex_.has_value() ||
        !finite(start) || !finite(goal) ||
        !std::isfinite(goalTolerance) || goalTolerance < 0.0F ||
        !pointInsideWorldForActor(start, actorSize_, worldSize_) ||
        !pointInsideWorldForActor(goal, actorSize_, worldSize_) ||
        std::any_of(
            expandedBlockers_.begin(),
            expandedBlockers_.end(),
            [&](const Rect &blocker)
            {
                return pointInsideOpenRect(start, blocker);
            }))
    {
        return std::nullopt;
    }

    if (distance(start, goal) <= goalTolerance + kDistanceEpsilon)
    {
        return start;
    }

    const bool exactGoalAvailable = std::none_of(
        expandedBlockers_.begin(),
        expandedBlockers_.end(),
        [&](const Rect &blocker)
        {
            return pointInsideOpenRect(goal, blocker);
        });
    if (exactGoalAvailable &&
        expandedBlockerIndex_->hasLineOfSight(start, goal))
    {
        return goal;
    }
    if (usesDenseGrid_)
    {
        return nextDenseGridWaypoint(start, goal, goalTolerance);
    }

    std::vector<Vec2> goalNodes;
    const auto appendGoalNode = [&](Vec2 candidate)
    {
        if (!pointInsideWorldForActor(candidate, actorSize_, worldSize_) ||
            distance(candidate, goal) >
                goalTolerance + kGeometryEpsilon ||
            std::any_of(
                expandedBlockers_.begin(),
                expandedBlockers_.end(),
                [&](const Rect &blocker)
                {
                    return pointInsideOpenRect(candidate, blocker);
                }) ||
            std::any_of(
                goalNodes.begin(),
                goalNodes.end(),
                [&](Vec2 existing)
                {
                    return distance(existing, candidate) <=
                        kDistanceEpsilon;
                }))
        {
            return;
        }
        goalNodes.push_back(candidate);
    };

    appendGoalNode(goal);
    if (goalTolerance > kDistanceEpsilon)
    {
        for (const Rect &blocker : expandedBlockers_)
        {
            if (!pointInsideOpenRect(goal, blocker))
            {
                continue;
            }

            const float left = blocker.position.x;
            const float right = blocker.position.x + blocker.size.x;
            const float top = blocker.position.y;
            const float bottom = blocker.position.y + blocker.size.y;
            const float projectedX = std::clamp(goal.x, left, right);
            const float projectedY = std::clamp(goal.y, top, bottom);
            appendGoalNode(Vec2{left, projectedY});
            appendGoalNode(Vec2{right, projectedY});
            appendGoalNode(Vec2{projectedX, top});
            appendGoalNode(Vec2{projectedX, bottom});
        }
    }

    if (goalNodes.empty())
    {
        return std::nullopt;
    }

    if (goalNodes.front().x == goal.x &&
        goalNodes.front().y == goal.y &&
        expandedBlockerIndex_->hasLineOfSight(start, goal))
    {
        return goal;
    }

    std::vector<Vec2> nodes{start};
    nodes.insert(nodes.end(), goalNodes.begin(), goalNodes.end());
    const std::size_t firstCornerNode = nodes.size();
    nodes.insert(nodes.end(), cornerNodes_.begin(), cornerNodes_.end());

    const std::size_t nodeCount = nodes.size();
    const std::size_t cornerCount = cornerNodes_.size();
    const float infinity = std::numeric_limits<float>::infinity();
    // Static corner-to-corner visibility already lives in cornerEdges_. Only
    // the start/goal rows are dynamic. Keeping those rows separately avoids
    // copying the full O(corners^2) matrix for every enemy refresh.
    std::vector<float> dynamicEdges(
        firstCornerNode * nodeCount,
        infinity);
    for (std::size_t first{}; first < firstCornerNode; ++first)
    {
        dynamicEdges[first * nodeCount + first] = 0.0F;
        for (std::size_t second = first + 1U;
             second < nodeCount;
             ++second)
        {
            if (!expandedBlockerIndex_->hasLineOfSight(
                    nodes[first], nodes[second]))
            {
                continue;
            }
            const float edgeDistance = distance(nodes[first], nodes[second]);
            if (!std::isfinite(edgeDistance) ||
                edgeDistance <= kDistanceEpsilon)
            {
                continue;
            }
            dynamicEdges[first * nodeCount + second] = edgeDistance;
        }
    }
    const auto edgeWeight = [&](std::size_t first, std::size_t second)
    {
        if (first < firstCornerNode)
        {
            return first <= second
                ? dynamicEdges[first * nodeCount + second]
                : dynamicEdges[second * nodeCount + first];
        }
        if (second < firstCornerNode)
        {
            return dynamicEdges[second * nodeCount + first];
        }
        return cornerEdges_[
            (first - firstCornerNode) * cornerCount +
            (second - firstCornerNode)];
    };

    std::vector<float> distances(nodeCount, infinity);
    std::vector<std::size_t> previous(nodeCount, kNoNode);
    std::vector<bool> visited(nodeCount, false);
    distances[0] = 0.0F;

    for (std::size_t iteration{}; iteration < nodeCount; ++iteration)
    {
        std::size_t current = kNoNode;
        for (std::size_t index{}; index < nodeCount; ++index)
        {
            if (visited[index])
            {
                continue;
            }
            if (current == kNoNode ||
                distances[index] < distances[current] - kDistanceEpsilon ||
                (std::abs(distances[index] - distances[current]) <=
                     kDistanceEpsilon &&
                 index < current))
            {
                current = index;
            }
        }
        if (current == kNoNode || !std::isfinite(distances[current]))
        {
            break;
        }
        visited[current] = true;

        for (std::size_t neighbor{}; neighbor < nodeCount; ++neighbor)
        {
            const float edge = edgeWeight(current, neighbor);
            if (visited[neighbor] || !std::isfinite(edge))
            {
                continue;
            }
            const float candidate = distances[current] + edge;
            if (candidate < distances[neighbor] - kDistanceEpsilon ||
                (std::abs(candidate - distances[neighbor]) <=
                     kDistanceEpsilon &&
                 current < previous[neighbor]))
            {
                distances[neighbor] = candidate;
                previous[neighbor] = current;
            }
        }
    }

    std::size_t selectedGoal = kNoNode;
    for (std::size_t goalIndex = 1U;
         goalIndex < firstCornerNode;
         ++goalIndex)
    {
        if (!std::isfinite(distances[goalIndex]) ||
            previous[goalIndex] == kNoNode)
        {
            continue;
        }
        if (selectedGoal == kNoNode ||
            distances[goalIndex] <
                distances[selectedGoal] - kDistanceEpsilon ||
            (std::abs(
                 distances[goalIndex] - distances[selectedGoal]) <=
                 kDistanceEpsilon &&
             goalIndex < selectedGoal))
        {
            selectedGoal = goalIndex;
        }
    }

    if (selectedGoal == kNoNode)
    {
        return std::nullopt;
    }

    std::size_t next = selectedGoal;
    std::size_t guard{};
    while (previous[next] != 0U)
    {
        next = previous[next];
        if (next == kNoNode || ++guard > nodeCount)
        {
            return std::nullopt;
        }
    }
    return nodes[next];
}

Vec2 RaidSpaceNavigationField::actorSize() const noexcept
{
    return actorSize_;
}

Vec2 RaidSpaceNavigationField::worldSize() const noexcept
{
    return worldSize_;
}

std::optional<Vec2> RaidSpaceNavigationField::nextDenseGridWaypoint(
    Vec2 start,
    Vec2 goal,
    float goalTolerance) const
{
    if (!usesDenseGrid_ || denseGridColumns_ == 0U ||
        denseGridRows_ == 0U || denseGridWalkable_.empty() ||
        denseGridConnections_.size() != denseGridWalkable_.size())
    {
        return std::nullopt;
    }

    const auto cellPosition = [&](std::size_t index)
    {
        const std::size_t row = index / denseGridColumns_;
        const std::size_t column = index % denseGridColumns_;
        return Vec2{
            denseGridOrigin_.x +
                static_cast<float>(column) * denseGridCellSize_,
            denseGridOrigin_.y +
                static_cast<float>(row) * denseGridCellSize_};
    };
    const auto nearestWalkable = [&](Vec2 point, float maximumDistance)
        -> std::optional<std::size_t>
    {
        std::optional<std::size_t> selected;
        float selectedDistance = std::numeric_limits<float>::infinity();
        const int centerColumn = std::clamp(
            static_cast<int>(std::lround(
                (point.x - denseGridOrigin_.x) / denseGridCellSize_)),
            0, static_cast<int>(denseGridColumns_ - 1U));
        const int centerRow = std::clamp(
            static_cast<int>(std::lround(
                (point.y - denseGridOrigin_.y) / denseGridCellSize_)),
            0, static_cast<int>(denseGridRows_ - 1U));
        const int radius = static_cast<int>(
            std::ceil(maximumDistance / denseGridCellSize_)) + 1;
        const int firstColumn = std::max(0, centerColumn - radius);
        const int lastColumn = std::min(
            static_cast<int>(denseGridColumns_ - 1U),
            centerColumn + radius);
        const int firstRow = std::max(0, centerRow - radius);
        const int lastRow = std::min(
            static_cast<int>(denseGridRows_ - 1U), centerRow + radius);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = firstColumn; column <= lastColumn; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) *
                    denseGridColumns_ + static_cast<std::size_t>(column);
                if (denseGridWalkable_[index] == 0U)
                    continue;
                const float candidateDistance =
                    distance(point, cellPosition(index));
                if (candidateDistance > maximumDistance + kGeometryEpsilon ||
                    !expandedBlockerIndex_->hasLineOfSight(
                        point, cellPosition(index)))
                    continue;
                if (!selected.has_value() ||
                    candidateDistance < selectedDistance - kDistanceEpsilon ||
                    (std::abs(candidateDistance - selectedDistance) <=
                         kDistanceEpsilon &&
                     index < *selected))
                {
                    selected = index;
                    selectedDistance = candidateDistance;
                }
            }
        }
        return selected;
    };

    // A final partial grid cell can leave the navigable world edge almost one
    // complete cell away from the last sample. The line-of-sight check above
    // makes this wider snap radius safe while allowing actors near that edge
    // to enter the cached graph.
    const float startSnapDistance = denseGridCellSize_ * 1.5F;
    const float goalSnapDistance = std::max(
        goalTolerance,
        denseGridCellSize_ * 1.5F);
    const std::optional<std::size_t> startCell =
        nearestWalkable(start, startSnapDistance);
    const std::optional<std::size_t> goalCell =
        nearestWalkable(goal, goalSnapDistance);
    if (!startCell.has_value() || !goalCell.has_value())
    {
        return std::nullopt;
    }
    if (*startCell == *goalCell)
    {
        return cellPosition(*startCell);
    }

    std::size_t denseGoalCell = *goalCell;
    const std::size_t startCellRow = *startCell / denseGridColumns_;
    const std::size_t startCellColumn = *startCell % denseGridColumns_;
    const std::size_t goalCellRow = *goalCell / denseGridColumns_;
    const std::size_t goalCellColumn = *goalCell % denseGridColumns_;
    const std::size_t startChunkRow =
        startCellRow / portalChunkSizeCells_;
    const std::size_t startChunkColumn =
        startCellColumn / portalChunkSizeCells_;
    const std::size_t goalChunkRow = goalCellRow / portalChunkSizeCells_;
    const std::size_t goalChunkColumn =
        goalCellColumn / portalChunkSizeCells_;
    const std::size_t startChunk =
        startChunkRow * portalChunkColumns_ + startChunkColumn;
    const std::size_t goalChunk =
        goalChunkRow * portalChunkColumns_ + goalChunkColumn;
    if (startChunk != goalChunk && !portalChunkConnections_.empty())
    {
        std::vector<std::size_t> chunkPrevious(
            portalChunkConnections_.size(), kNoNode);
        std::queue<std::size_t> chunkFrontier;
        chunkPrevious[startChunk] = startChunk;
        chunkFrontier.push(startChunk);
        while (!chunkFrontier.empty() &&
               chunkPrevious[goalChunk] == kNoNode)
        {
            const std::size_t current = chunkFrontier.front();
            chunkFrontier.pop();
            const std::size_t row = current / portalChunkColumns_;
            const std::size_t column = current % portalChunkColumns_;
            for (std::size_t direction{};
                 direction < std::size(kDenseGridNeighborOffsets);
                 ++direction)
            {
                if ((portalChunkConnections_[current] &
                     static_cast<std::uint8_t>(1U << direction)) == 0U)
                    continue;
                const int nextColumn = static_cast<int>(column) +
                    kDenseGridNeighborOffsets[direction][0];
                const int nextRow = static_cast<int>(row) +
                    kDenseGridNeighborOffsets[direction][1];
                if (nextColumn < 0 || nextRow < 0 ||
                    nextColumn >= static_cast<int>(portalChunkColumns_) ||
                    nextRow >= static_cast<int>(portalChunkRows_))
                    continue;
                const std::size_t next =
                    static_cast<std::size_t>(nextRow) * portalChunkColumns_ +
                    static_cast<std::size_t>(nextColumn);
                if (chunkPrevious[next] != kNoNode)
                    continue;
                chunkPrevious[next] = current;
                chunkFrontier.push(next);
            }
        }
        if (chunkPrevious[goalChunk] == kNoNode)
            return std::nullopt;
        std::size_t nextChunk = goalChunk;
        while (chunkPrevious[nextChunk] != startChunk)
        {
            nextChunk = chunkPrevious[nextChunk];
            if (nextChunk == kNoNode)
                return std::nullopt;
        }
        const std::size_t nextChunkRow = nextChunk / portalChunkColumns_;
        const std::size_t nextChunkColumn = nextChunk % portalChunkColumns_;
        std::size_t direction{};
        if (nextChunkRow < startChunkRow) direction = 0U;
        else if (nextChunkColumn < startChunkColumn) direction = 1U;
        else if (nextChunkColumn > startChunkColumn) direction = 2U;
        else direction = 3U;
        const std::size_t firstRow =
            startChunkRow * portalChunkSizeCells_;
        const std::size_t lastRow = std::min(
            denseGridRows_, firstRow + portalChunkSizeCells_);
        const std::size_t firstColumn =
            startChunkColumn * portalChunkSizeCells_;
        const std::size_t lastColumn = std::min(
            denseGridColumns_, firstColumn + portalChunkSizeCells_);
        std::optional<std::size_t> selectedPortal;
        std::size_t selectedDistance = std::numeric_limits<std::size_t>::max();
        for (std::size_t row = firstRow; row < lastRow; ++row)
        {
            for (std::size_t column = firstColumn;
                 column < lastColumn; ++column)
            {
                const std::size_t cell = row * denseGridColumns_ + column;
                if ((denseGridConnections_[cell] &
                     static_cast<std::uint8_t>(1U << direction)) == 0U)
                    continue;
                const int neighborColumn = static_cast<int>(column) +
                    kDenseGridNeighborOffsets[direction][0];
                const int neighborRow = static_cast<int>(row) +
                    kDenseGridNeighborOffsets[direction][1];
                if (neighborColumn < 0 || neighborRow < 0)
                    continue;
                const std::size_t neighbor =
                    static_cast<std::size_t>(neighborRow) * denseGridColumns_ +
                    static_cast<std::size_t>(neighborColumn);
                const std::size_t neighborChunk =
                    (static_cast<std::size_t>(neighborRow) /
                         portalChunkSizeCells_) * portalChunkColumns_ +
                    static_cast<std::size_t>(neighborColumn) /
                        portalChunkSizeCells_;
                if (neighborChunk != nextChunk)
                    continue;
                const std::size_t score =
                    std::max(static_cast<std::size_t>(neighborRow), goalCellRow) -
                        std::min(static_cast<std::size_t>(neighborRow), goalCellRow) +
                    std::max(static_cast<std::size_t>(neighborColumn), goalCellColumn) -
                        std::min(static_cast<std::size_t>(neighborColumn), goalCellColumn);
                if (!selectedPortal.has_value() || score < selectedDistance ||
                    (score == selectedDistance && neighbor < *selectedPortal))
                {
                    selectedPortal = neighbor;
                    selectedDistance = score;
                }
            }
        }
        if (!selectedPortal.has_value())
            return std::nullopt;
        denseGoalCell = *selectedPortal;
    }

    std::vector<std::size_t> previous(
        denseGridWalkable_.size(),
        kNoNode);
    std::vector<float> costs(
        denseGridWalkable_.size(),
        std::numeric_limits<float>::infinity());
    using FrontierEntry = std::pair<float, std::size_t>;
    std::priority_queue<FrontierEntry,
                        std::vector<FrontierEntry>,
                        std::greater<FrontierEntry>> frontier;
    const std::size_t goalRow = denseGoalCell / denseGridColumns_;
    const std::size_t goalColumn = denseGoalCell % denseGridColumns_;
    const auto heuristic = [&](std::size_t index)
    {
        const std::size_t row = index / denseGridColumns_;
        const std::size_t column = index % denseGridColumns_;
        return static_cast<float>(
            std::max(row, goalRow) - std::min(row, goalRow) +
            std::max(column, goalColumn) - std::min(column, goalColumn));
    };
    previous[*startCell] = *startCell;
    costs[*startCell] = 0.0F;
    frontier.push({heuristic(*startCell), *startCell});
    while (!frontier.empty() && previous[denseGoalCell] == kNoNode)
    {
        const std::size_t current = frontier.top().second;
        frontier.pop();
        const std::size_t currentRow = current / denseGridColumns_;
        const std::size_t currentColumn = current % denseGridColumns_;
        for (std::size_t direction{};
             direction < std::size(kDenseGridNeighborOffsets);
             ++direction)
        {
            if ((denseGridConnections_[current] &
                 static_cast<std::uint8_t>(1U << direction)) == 0U)
            {
                continue;
            }
            const int nextColumn =
                static_cast<int>(currentColumn) +
                kDenseGridNeighborOffsets[direction][0];
            const int nextRow = static_cast<int>(currentRow) +
                kDenseGridNeighborOffsets[direction][1];
            if (nextColumn < 0 || nextRow < 0 ||
                nextColumn >= static_cast<int>(denseGridColumns_) ||
                nextRow >= static_cast<int>(denseGridRows_))
            {
                continue;
            }
            const std::size_t next =
                static_cast<std::size_t>(nextRow) * denseGridColumns_ +
                static_cast<std::size_t>(nextColumn);
            if (denseGridWalkable_[next] == 0U)
                continue;
            const float candidate = costs[current] + 1.0F;
            if (candidate + kDistanceEpsilon >= costs[next])
                continue;
            costs[next] = candidate;
            previous[next] = current;
            frontier.push({candidate + heuristic(next), next});
        }
    }
    if (previous[denseGoalCell] == kNoNode)
    {
        return std::nullopt;
    }

    std::size_t next = denseGoalCell;
    std::size_t guard{};
    while (previous[next] != *startCell)
    {
        next = previous[next];
        if (next == kNoNode || ++guard > previous.size())
        {
            return std::nullopt;
        }
    }
    return cellPosition(next);
}

std::optional<Vec2> nextRaidSpaceWaypoint(
    const RaidSpaceNavigationQuery &query)
{
    const std::optional<RaidSpaceNavigationField> field =
        RaidSpaceNavigationField::build(
            query.actorSize,
            query.worldSize,
            query.blockers,
            query.clearance);
    return field.has_value()
        ? field->nextWaypoint(
              query.start,
              query.goal,
              query.goalTolerance)
        : std::nullopt;
}
