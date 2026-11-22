#include "GraphTestControls.h"
#include "ForceViewRhiWidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace graph_demo {

GraphTestControls::GraphTestControls(ForceViewRhiWidget &view, QWidget *parent)
    : QGroupBox(QStringLiteral("测试操作"), parent), m_view(view)
{
    auto *layout = new QVBoxLayout(this);
    const auto button = [this, layout](const QString &text, auto callback) {
        auto *control = new QPushButton(text, this);
        layout->addWidget(control);
        connect(control, &QPushButton::clicked, this, callback);
    };
    button(QStringLiteral("适配视图"), [this] { m_view.fitViewToContent(); });
    button(QStringLiteral("重启"), [this] { m_view.restartSimulation(); });
    button(QStringLiteral("暂停"), [this] { m_view.pauseSimulation(); });
    button(QStringLiteral("继续"), [this] { m_view.resumeSimulation(); });
    button(QStringLiteral("加点"), [this] { addNode(); });
    button(QStringLiteral("编辑点"), [this] { editNode(); });
    button(QStringLiteral("减点"), [this] { removeNode(); });
    button(QStringLiteral("加边"), [this] { addEdge(); });
    button(QStringLiteral("减边"), [this] { removeEdge(); });
    button(QStringLiteral("性能表"), [this] {
        m_view.setPerformanceOverlayVisible(!m_view.isPerformanceOverlayVisible());
    });
    m_summary = new QLabel(this);
    layout->addWidget(m_summary);
}

void GraphTestControls::setGraphData(const GraphData &graphData)
{
    m_data = graphData;
    updateSummary();
}

void GraphTestControls::updateSummary()
{
    m_summary->setText(QStringLiteral("当前：%1 节点 · %2 条边")
                           .arg(m_data.ids.size()).arg(m_data.edges.size() / 2));
}

QString GraphTestControls::chooseNode(const QString &title)
{
    if (m_data.ids.isEmpty()) return {};
    bool accepted = false;
    const QString id = QInputDialog::getItem(this, title, QStringLiteral("节点"),
                                            m_data.ids, 0, false, &accepted);
    return accepted ? id : QString();
}

void GraphTestControls::addNode()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("添加测试节点"));
    auto *form = new QFormLayout(&dialog);
    auto *id = new QLineEdit(&dialog);
    auto *label = new QLineEdit(&dialog);
    form->addRow(QStringLiteral("节点 ID"), id);
    form->addRow(QStringLiteral("显示名称"), label);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString nodeId = id->text().trimmed();
    if (nodeId.isEmpty() || m_data.ids.contains(nodeId)) {
        QMessageBox::warning(this, QStringLiteral("无法添加节点"),
                             QStringLiteral("节点 ID 不能为空或重复。"));
        return;
    }
    const QString text = label->text().trimmed().isEmpty() ? nodeId : label->text().trimmed();
    m_view.add_node_runtime(nodeId, m_view.getPanX(), m_view.getPanY(), text, 7.0f,
                            QColor(QStringLiteral("#99ccff")));
    m_data.ids.append(nodeId);
    m_data.labels.append(text);
    m_data.radii.append(7.0f);
    m_data.colors.append(QColor(QStringLiteral("#99ccff")));
    updateSummary();
}

void GraphTestControls::editNode()
{
    const QString id = chooseNode(QStringLiteral("编辑测试节点"));
    if (id.isEmpty()) return;
    const auto index = m_data.ids.indexOf(id);
    bool accepted = false;
    const QString label = QInputDialog::getText(this, QStringLiteral("编辑测试节点"),
        QStringLiteral("显示名称"), QLineEdit::Normal, m_data.labels[index], &accepted);
    if (!accepted) return;
    m_data.labels[index] = label.trimmed().isEmpty() ? id : label.trimmed();
    m_view.update_node_runtime(id, m_data.labels[index], m_data.radii[index], m_data.colors[index]);
}

void GraphTestControls::removeNode()
{
    const QString id = chooseNode(QStringLiteral("删除测试节点"));
    if (id.isEmpty()) return;
    const auto index = m_data.ids.indexOf(id);
    QVector<int> edges;
    for (qsizetype edge = 0; edge < m_data.edges.size(); edge += 2) {
        const int source = m_data.edges[edge], target = m_data.edges[edge + 1];
        if (source == index || target == index) continue;
        edges.append(source > index ? source - 1 : source);
        edges.append(target > index ? target - 1 : target);
    }
    m_data.edges = std::move(edges);
    m_data.ids.removeAt(index);
    m_data.labels.removeAt(index);
    m_data.radii.removeAt(index);
    m_data.colors.removeAt(index);
    m_view.remove_node_runtime(id);
    updateSummary();
}

void GraphTestControls::addEdge()
{
    if (m_data.ids.size() < 2) return;
    const QString source = chooseNode(QStringLiteral("添加边：起点"));
    if (source.isEmpty()) return;
    const QString target = chooseNode(QStringLiteral("添加边：终点"));
    if (target.isEmpty()) return;
    const int u = int(m_data.ids.indexOf(source)), v = int(m_data.ids.indexOf(target));
    if (u == v) return;
    for (qsizetype i = 0; i < m_data.edges.size(); i += 2) {
        if ((m_data.edges[i] == u && m_data.edges[i + 1] == v)
            || (m_data.edges[i] == v && m_data.edges[i + 1] == u)) return;
    }
    m_view.add_edge_runtime(source, target);
    m_data.edges.append(u);
    m_data.edges.append(v);
    updateSummary();
}

void GraphTestControls::removeEdge()
{
    if (m_data.edges.isEmpty()) return;
    QStringList choices;
    for (qsizetype i = 0; i < m_data.edges.size(); i += 2)
        choices.append(QStringLiteral("%1 — %2").arg(m_data.ids[m_data.edges[i]],
                                                    m_data.ids[m_data.edges[i + 1]]));
    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, QStringLiteral("删除测试边"),
        QStringLiteral("边"), choices, 0, false, &accepted);
    if (!accepted) return;
    const auto index = choices.indexOf(choice) * 2;
    m_view.remove_edge_runtime(m_data.ids[m_data.edges[index]], m_data.ids[m_data.edges[index + 1]]);
    m_data.edges.remove(index, 2);
    updateSummary();
}

} // namespace graph_demo
