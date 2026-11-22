#pragma once

#include <QColor>
#include <QStringList>
#include <QVector>

namespace graph_demo {

struct Choice { QString key, name, description; };
struct Preset { QString key, name, positions, topology, description; int degree; };
const QVector<Choice>& positionChoices();
const QVector<Choice>& topologyChoices();
const QVector<Preset>& presets();

struct GraphData {
    QVector<int> edges;
    QVector<float> positions, radii;
    QStringList ids, labels;
    QVector<QColor> colors;
};

// Independent random streams keep positions and attributes identical when
// changing only topology, and edges identical when changing only positions.
GraphData generateGraph(int count, const QString& positions, const QString& topology,
                        int degree, quint32 seed);
}
