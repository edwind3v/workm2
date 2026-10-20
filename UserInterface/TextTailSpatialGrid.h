#pragma once

#ifdef ENABLE_FIX_MOBS_LAG
#include "../EterLib/TextPerformance.h"
#include <array>
#include <cmath>
#include <limits>

// Broad phase only: the caller's original rectangle test and ordering win.
class CTextTailSpatialGrid
{
    using TTail = CPythonTextTail::TTextTail;
    struct SNode { int x, y, previous, next, itemNext; std::size_t item; };
    struct SItem { int first = -1; unsigned visited = 0; bool overflow = false; };
    static constexpr unsigned BucketCount = 1024;
    static constexpr std::size_t MaxNodes = 65536;
    std::array<int, BucketCount> m_buckets{};
    std::vector<SNode> m_nodes;
    std::vector<SItem> m_items;
    std::vector<std::size_t> m_overflow;
    const std::vector<TTail*>* m_tails = nullptr;
    int m_free = -1;
    unsigned m_query = 0;

    static unsigned Bucket(int x, int y)
    { return (static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u) & (BucketCount - 1); }
    static bool Cells(TTail* tail, int& x0, int& y0, int& x1, int& y1)
    {
        const double left = std::floor((tail->x + tail->xStart) / 64.0);
        const double right = std::floor((tail->x + tail->xEnd) / 64.0);
        const double top = std::floor((tail->y + tail->yStart) / 64.0);
        const double bottom = std::floor((tail->y + tail->yEnd) / 64.0);
        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) || !std::isfinite(bottom) ||
            left < -1000000 || right > 1000000 || top < -1000000 || bottom > 1000000 ||
            right < left || bottom < top || (right - left + 1) * (bottom - top + 1) > 64) return false;
        x0 = static_cast<int>(left); x1 = static_cast<int>(right);
        y0 = static_cast<int>(top); y1 = static_cast<int>(bottom);
        return true;
    }
    void Remove(std::size_t index)
    {
        int nodeIndex = m_items[index].first;
        while (nodeIndex >= 0)
        {
            auto& node = m_nodes[nodeIndex];
            const int next = node.itemNext;
            if (node.previous >= 0) m_nodes[node.previous].next = node.next;
            else m_buckets[Bucket(node.x, node.y)] = node.next;
            if (node.next >= 0) m_nodes[node.next].previous = node.previous;
            node.next = m_free; m_free = nodeIndex;
            nodeIndex = next;
        }
        m_items[index].first = -1;
    }
    void Insert(std::size_t index)
    {
        auto& item = m_items[index];
        int x0, y0, x1, y1;
        bool valid = Cells((*m_tails)[index], x0, y0, x1, y1);
        if (valid)
        {
            for (int y = y0; y <= y1 && valid; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    int nodeIndex;
                    if (m_free >= 0) { nodeIndex = m_free; m_free = m_nodes[m_free].next; }
                    else
                    {
                        if (m_nodes.size() == MaxNodes) { valid = false; break; }
                        if (m_nodes.size() == m_nodes.capacity()) CTextPerformance::Get().Add(CTextPerformance::CapacityGrowths);
                        nodeIndex = static_cast<int>(m_nodes.size());
                        m_nodes.push_back({});
                    }
                    const unsigned bucket = Bucket(x, y);
                    m_nodes[nodeIndex] = { x, y, -1, m_buckets[bucket], item.first, index };
                    if (m_buckets[bucket] >= 0) m_nodes[m_buckets[bucket]].previous = nodeIndex;
                    m_buckets[bucket] = nodeIndex;
                    item.first = nodeIndex;
                }
        }
        if (!valid)
        {
            Remove(index);
            if (!item.overflow)
            {
                if (m_overflow.size() == m_overflow.capacity()) CTextPerformance::Get().Add(CTextPerformance::CapacityGrowths);
                m_overflow.push_back(index);
                item.overflow = true;
            }
        }
        // Overflow membership lasts for this arrangement pass, so no stale slots.
    }

public:
    static constexpr std::size_t None = static_cast<std::size_t>(-1);
    void Build(const std::vector<TTail*>& tails)
    {
        m_tails = &tails;
        m_buckets.fill(-1); m_nodes.clear(); m_overflow.clear(); m_free = -1; m_query = 0;
        if (tails.size() > m_items.capacity()) CTextPerformance::Get().Add(CTextPerformance::CapacityGrowths);
        m_items.assign(tails.size(), SItem{});
        for (std::size_t i = 0; i < tails.size(); ++i) Insert(i);
    }
    void Moved(std::size_t index) { Remove(index); Insert(index); }
    template<class TIntersect> std::size_t FirstOverlap(std::size_t index, TIntersect intersect)
    {
        if (++m_query == 0)
        { for (auto& item : m_items) item.visited = 0; ++m_query; }
        std::size_t result = None;
        auto consider = [&](std::size_t other) {
            if (other == index || other >= result || m_items[other].visited == m_query) return;
            m_items[other].visited = m_query;
            CTextPerformance::Get().Add(CTextPerformance::OverlapChecks);
            if (intersect((*m_tails)[index], (*m_tails)[other])) result = other;
        };
        int x0, y0, x1, y1;
        if (!Cells((*m_tails)[index], x0, y0, x1, y1))
        { for (std::size_t i = 0; i < m_tails->size(); ++i) consider(i); return result; }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int node = m_buckets[Bucket(x, y)]; node >= 0; node = m_nodes[node].next)
                    if (m_nodes[node].x == x && m_nodes[node].y == y) consider(m_nodes[node].item);
        for (auto other : m_overflow) consider(other);
        return result;
    }
};
#endif
