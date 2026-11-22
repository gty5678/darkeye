#include "GraphScenarios.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace graph_demo {
const QVector<Choice>& positionChoices()
{
    static const QVector<Choice> choices{
        {"disk", "均匀圆盘", "均匀覆盖圆盘，作为一般性能与布局的基线。"},
        {"gaussian", "中心高斯", "中心密集、外围稀疏，观察密度不均时的碰撞和建树成本。"},
        {"clusters", "八个聚团", "最多八个密集团，观察社群分离与远距离近似。"},
        {"grid", "规则网格", "等间距排布，观察对称布局与收敛。"},
        {"ring", "圆环", "节点排成圆环，观察链、环及中心枢纽。"},
        {"overlap", "完全重叠（退化压力）", "所有节点从原点出发，检查重复空间键与碰撞稳定性；建议先用 400–2000 点。"}};
    return choices;
}
const QVector<Choice>& topologyChoices()
{
    static const QVector<Choice> choices{
        {"random", "均匀随机", "随机选择无重复无向边，目标平均度控制边数；可能有孤立点。"},
        {"communities", "社群＋少量桥接", "团内稠密、团间少量桥接，观察社群结构。"},
        {"scale-free", "无标度／多枢纽", "按已有连接数优先连接，形成少量高度节点。"},
        {"grid", "二维网格", "仅连接水平和垂直邻居，平均度接近 4。"},
        {"ring", "闭合环", "节点首尾相连，平均度接近 2。"},
        {"star", "单中心星形", "所有节点连接同一中心，测试极端高度节点。"},
        {"disconnected", "八个独立分量", "最多八个团，各团内部连通，团间没有边。"},
        {"none", "无连边", "只保留斥力、中心力与碰撞，隔离连接力成本。"}};
    return choices;
}
const QVector<Preset>& presets()
{
    static const QVector<Preset> choices{
        {"baseline", "基线：均匀随机", "disk", "random", "推荐基线：位置均匀、平均度 4，用于比较不同节点规模。", 4},
        {"communities", "社群：八团桥接", "clusters", "communities", "推荐布局测试：检查八个社群能否保持清晰分离。", 4},
        {"hubs", "枢纽：无标度网络", "disk", "scale-free", "推荐连接力测试：少量枢纽拥有大量邻居，更接近许多实际关系图。", 4},
        {"lattice", "规则：二维网格", "grid", "grid", "推荐对称性测试：从规则格点出发，观察布局和收敛是否稳定。", 4},
        {"star", "极端：单中心星形", "ring", "star", "推荐高度节点压力测试：观察中心节点、长边和悬浮高亮开销。", 2},
        {"islands", "孤岛：独立社群", "clusters", "disconnected", "推荐多分量测试：检查互不连接的社群如何排布。", 4},
        {"overlap", "退化：原点重叠", "overlap", "random", "推荐正确性压力测试：从完全重叠位置展开，建议先用 400–2000 点。", 2}};
    return choices;
}

GraphData generateGraph(int count, const QString& positionMode, const QString& topology, int degree, quint32 seed)
{
    GraphData data;
    if (count <= 0) return data;
    std::mt19937 positions(seed), attributes(seed ^ 0x9e3779b9u), links(seed ^ 0x85ebca6bu);
    std::uniform_real_distribution<float> unit(0, 1);
    std::normal_distribution<float> gaussian(0, 1);
    constexpr float tau = 6.28318530718f;
    const float extent = std::sqrt(float(count)) * 25 + 150;
    const int groups = std::min(count, 8);
    const int columns = int(std::ceil(std::sqrt(double(count))));
    const int rows = (count + columns - 1) / columns;
    data.positions.resize(count * 2);
    data.radii.reserve(count); data.ids.reserve(count); data.labels.reserve(count); data.colors.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int group = int((qint64(i + 1) * groups - 1) / count);
        float x = 0, y = 0;
        if (positionMode == "grid") {
            x = (i % columns - (columns - 1) * 0.5f) * 30;
            y = (i / columns - (rows - 1) * 0.5f) * 30;
        } else if (positionMode == "ring") {
            const float angle = tau * i / count;
            x = std::cos(angle) * extent * 0.65f; y = std::sin(angle) * extent * 0.65f;
        } else if (positionMode == "gaussian") {
            x = gaussian(positions) * extent * 0.25f; y = gaussian(positions) * extent * 0.25f;
        } else if (positionMode == "clusters") {
            const float angle = tau * group / groups;
            x = std::cos(angle) * extent * 0.65f + gaussian(positions) * extent * 0.04f;
            y = std::sin(angle) * extent * 0.65f + gaussian(positions) * extent * 0.04f;
        } else if (positionMode != "overlap") {
            const float angle = tau * unit(positions), radius = extent * std::sqrt(unit(positions));
            x = std::cos(angle) * radius; y = std::sin(angle) * radius;
        }
        data.positions[2 * i] = x; data.positions[2 * i + 1] = y;
        data.ids << QStringLiteral("ID%1").arg(i); data.labels << QStringLiteral("N%1").arg(i);
        data.radii << 4.0f + float(attributes() % 7);
        data.colors << QColor::fromHsv(group * 360 / groups, 150 + int(attributes() % 50), 220);
    }
    QSet<quint64> used;
    auto addEdge = [&](int a, int b) {
        if (a == b) return false;
        if (a > b) std::swap(a, b);
        const quint64 key = (quint64(quint32(a)) << 32) | quint32(b);
        if (used.contains(key)) return false;
        used.insert(key); data.edges << a << b;
        return true;
    };
    // Generate a uniform subset without rejection loops, including dense tiny
    // graphs. Floyd sampling is linear in the requested edges; mapping each
    // sampled pair index to its endpoints takes a binary search.
    auto randomEdges = [&](int start, int end, qint64 requested) {
        const int size = end - start;
        const qint64 total = qint64(size) * (size - 1) / 2;
        const qint64 amount = std::min(total, requested);
        QSet<qint64> chosen;
        auto rowStart = [size](qint64 row) { return row * (2LL * size - row - 1) / 2; };
        for (qint64 j = total - amount; j < total; ++j) {
            const qint64 candidate = std::uniform_int_distribution<qint64>(0, j)(links);
            const qint64 index = chosen.contains(candidate) ? j : candidate;
            chosen.insert(index);
            qint64 low = 0, high = size - 1;
            while (low + 1 < high) {
                const qint64 mid = (low + high) / 2;
                if (rowStart(mid) <= index) low = mid; else high = mid;
            }
            addEdge(start + int(low), start + int(low + 1 + index - rowStart(low)));
        }
    };
    const int average = std::clamp(degree, 1, 16);
    if (topology == "random") {
        randomEdges(0, count, qint64(count) * average / 2);
    } else if (topology == "grid") {
        for (int i = 0; i < count; ++i) {
            if (i % columns + 1 < columns && i + 1 < count) addEdge(i, i + 1);
            if (i + columns < count) addEdge(i, i + columns);
        }
    } else if (topology == "ring" || topology == "star") {
        for (int i = 1; i < count; ++i) addEdge(topology == "star" ? 0 : i - 1, i);
        if (topology == "ring" && count > 2) addEdge(count - 1, 0);
    } else if (topology == "communities" || topology == "disconnected") {
        for (int g = 0; g < groups; ++g) {
            const int start = int(qint64(g) * count / groups), end = int(qint64(g + 1) * count / groups);
            // A local chain guarantees each component is internally connected.
            for (int i = start + 1; i < end; ++i) addEdge(i - 1, i);
            randomEdges(start, end, qint64(end - start) * std::max(0, average - 2) / 2);
            if (topology == "communities" && g) addEdge(start - 1, start);
        }
    } else if (topology == "scale-free") {
        std::vector<int> endpoints;
        if (count > 1) { addEdge(0, 1); endpoints = {0, 1}; }
        const int attachment = std::max(1, average / 2);
        for (int i = 2; i < count; ++i) {
            QSet<int> neighbors;
            const int wanted = std::min(i, attachment);
            while (neighbors.size() < wanted) {
                const int target = endpoints[std::uniform_int_distribution<size_t>(0, endpoints.size() - 1)(links)];
                if (neighbors.contains(target)) continue;
                neighbors.insert(target); addEdge(i, target);
            }
            // Keep insertion order stable instead of iterating a salted QSet.
            const int edgeStart = int(data.edges.size()) - 2 * wanted;
            for (int k = edgeStart; k < data.edges.size(); ++k) endpoints.push_back(data.edges[k]);
        }
    }
    return data;
}
}
