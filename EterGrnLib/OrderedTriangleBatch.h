#pragma once

#include <cstddef>
#include <vector>

// A draw-order-preserving copy of one mesh/material-type list. In particular,
// never sort by material: alpha and additive passes depend on triangle order.
namespace OrderedTriangleBatch
{
struct Group
{
    int firstIndex;
    int triangleCount;
    unsigned material;
    unsigned sourceDraws = 1;
};

struct Result
{
    std::vector<unsigned short> indices;
    std::vector<Group> groups;
};

inline bool Build(const unsigned short* indices, int indexCount,
    const std::vector<Group>& groups, unsigned maxTriangles, Result& result)
{
    result = Result{};
    if (!indices || indexCount <= 0 || groups.size() < 2 || !maxTriangles)
        return false;

    // Validate before allocating; unusual or overlapping topology that would
    // expand the buffer beyond the original size retains its original draws.
    std::size_t total = 0;
    bool canMerge = false;
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        const Group& group = groups[i];
        if (group.firstIndex < 0 || group.firstIndex > indexCount ||
            group.triangleCount <= 0 ||
            group.triangleCount > (indexCount - group.firstIndex) / 3 ||
            static_cast<unsigned>(group.triangleCount) > maxTriangles)
            return false;
        total += static_cast<std::size_t>(group.triangleCount) * 3;
        if (total > static_cast<std::size_t>(indexCount)) return false;
        if (i && groups[i - 1].material == group.material &&
            static_cast<unsigned>(groups[i - 1].triangleCount) <=
                maxTriangles - static_cast<unsigned>(group.triangleCount))
            canMerge = true;
    }
    if (!canMerge) return false;

    result.indices.reserve(total);
    result.groups.reserve(groups.size());
    for (const Group& group : groups)
    {
        const int first = static_cast<int>(result.indices.size());
        const auto begin = indices + group.firstIndex;
        result.indices.insert(result.indices.end(), begin, begin + group.triangleCount * 3);
        if (!result.groups.empty() && result.groups.back().material == group.material &&
            static_cast<unsigned>(result.groups.back().triangleCount) <=
                maxTriangles - static_cast<unsigned>(group.triangleCount))
        {
            result.groups.back().triangleCount += group.triangleCount;
            result.groups.back().sourceDraws += group.sourceDraws;
        }
        else
            result.groups.push_back({first, group.triangleCount, group.material, group.sourceDraws});
    }
    return true;
}
}
