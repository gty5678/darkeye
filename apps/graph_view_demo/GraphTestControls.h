#pragma once

#include "GraphScenarios.h"
#include <QGroupBox>

class ForceViewRhiWidget;
class QLabel;

namespace graph_demo {

// Runtime editing belongs to the standalone demo, never the application page.
class GraphTestControls final : public QGroupBox
{
public:
    explicit GraphTestControls(ForceViewRhiWidget &view, QWidget *parent = nullptr);
    void setGraphData(const GraphData &graphData);

private:
    QString chooseNode(const QString &title);
    void addNode();
    void editNode();
    void removeNode();
    void addEdge();
    void removeEdge();
    void updateSummary();

    ForceViewRhiWidget &m_view;
    GraphData m_data;
    QLabel *m_summary = nullptr;
};

} // namespace graph_demo
