#include "GraphScenarios.h"
#include <QSet>
#include <QTest>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <vector>

class GraphDemoDataTest final : public QObject {
    Q_OBJECT
private slots:
    void validGraphs_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("positions");
        QTest::addColumn<QString>("topology");
        for (int count : {1, 2, 17, 400})
            for (const auto& position : graph_demo::positionChoices())
                for (const auto& topology : graph_demo::topologyChoices())
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(count).arg(position.key, topology.key)))
                        << count << position.key << topology.key;
    }
    void validGraphs()
    {
        QFETCH(int, count); QFETCH(QString, positions); QFETCH(QString, topology);
        const auto graph = graph_demo::generateGraph(count, positions, topology, 16, 42);
        QCOMPARE(graph.positions.size(), count * 2);
        QCOMPARE(graph.ids.size(), count); QCOMPARE(graph.labels.size(), count);
        QCOMPARE(graph.radii.size(), count); QCOMPARE(graph.colors.size(), count);
        for (float value : graph.positions) QVERIFY(std::isfinite(value));
        QCOMPARE(graph.edges.size() % 2, 0);
        QSet<quint64> unique;
        for (int i = 0; i < graph.edges.size(); i += 2) {
            const int a = graph.edges[i], b = graph.edges[i + 1];
            QVERIFY(a >= 0 && a < b && b < count);
            const quint64 key = (quint64(quint32(a)) << 32) | quint32(b);
            QVERIFY(!unique.contains(key)); unique.insert(key);
        }
        if (positions == "overlap") for (float value : graph.positions) QCOMPARE(value, 0.0f);
        if (topology == "none") QVERIFY(graph.edges.isEmpty());
        if (topology == "star") {
            QCOMPARE(graph.edges.size() / 2, count - 1);
            for (int i = 0; i < graph.edges.size(); i += 2) QCOMPARE(graph.edges[i], 0);
        }
        if (topology == "random") QCOMPARE(graph.edges.size() / 2, std::min(qint64(count) * 16 / 2, qint64(count) * (count - 1) / 2));
    }
    void reproducibleIndependentStreams()
    {
        const auto original = graph_demo::generateGraph(400, "disk", "random", 4, 42);
        const auto repeated = graph_demo::generateGraph(400, "disk", "random", 4, 42);
        QCOMPARE(original.positions, repeated.positions); QCOMPARE(original.edges, repeated.edges);
        const auto newTopology = graph_demo::generateGraph(400, "disk", "scale-free", 4, 42);
        QCOMPARE(original.positions, newTopology.positions); QCOMPARE(original.colors, newTopology.colors);
        const auto newPositions = graph_demo::generateGraph(400, "clusters", "random", 4, 42);
        QCOMPARE(original.edges, newPositions.edges);
        const auto newSeed = graph_demo::generateGraph(400, "disk", "random", 4, 43);
        QVERIFY(original.edges != newSeed.edges); QVERIFY(original.positions != newSeed.positions);
    }
    void communityComponents()
    {
        for (int count : {1, 2, 17, 400, 100000}) for (bool connected : {false, true}) {
            const auto graph = graph_demo::generateGraph(count, "clusters", connected ? "communities" : "disconnected", 4, 42);
            std::vector<int> parent(count);
            std::iota(parent.begin(), parent.end(), 0);
            auto root = [&](int index) { while (parent[index] != index) { parent[index] = parent[parent[index]]; index = parent[index]; } return index; };
            int components = count;
            for (int i = 0; i < graph.edges.size(); i += 2) {
                const int a = root(graph.edges[i]), b = root(graph.edges[i + 1]);
                if (a != b) { parent[a] = b; --components; }
            }
            QCOMPARE(components, connected ? 1 : std::min(count, 8));
        }
    }
    void largeSparseGraphs()
    {
        for (const auto& topology : graph_demo::topologyChoices()) {
            const auto graph = graph_demo::generateGraph(100000, "disk", topology.key, 4, 42);
            QCOMPARE(graph.positions.size(), 200000);
            QVERIFY(graph.edges.size() <= 100000 * 4 + 16);
        }
    }
};
QTEST_GUILESS_MAIN(GraphDemoDataTest)
#include "GraphDemoDataTest.moc"
