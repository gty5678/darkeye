#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMainWindow>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPushButton>
#include <QVector>
#include <QStringList>
#include <QDebug>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QWheelEvent>
#include <QtWidgets/QRhiWidget>
#include <algorithm>
#include <cstdlib>
#include <cmath>

#include "ForceViewRhiWidget.h"
#include "GraphScenarios.h"
#include "GraphTestControls.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
                                   QStringLiteral("验证首帧 QRhi 关系图渲染后退出"));
    parser.addOption(smokeOption);
    QCommandLineOption backendOption(QStringLiteral("backend"), QStringLiteral("Layout backend: auto, gpu, cpu"), QStringLiteral("mode"), QStringLiteral("auto"));
    QCommandLineOption nodesOption(QStringLiteral("nodes"), QStringLiteral("Initial node count"), QStringLiteral("count"), QStringLiteral("400"));
    parser.addOption(backendOption);
    parser.addOption(nodesOption);
    parser.addOption({"preset", "Scenario preset: baseline, communities, hubs, lattice, star, islands, overlap", "name", "baseline"});
    parser.addOption({"positions", "Override initial positions: disk, gaussian, clusters, grid, ring, overlap", "mode"});
    parser.addOption({"topology", "Override edges: random, communities, scale-free, grid, ring, star, disconnected, none", "mode"});
    parser.addOption({"degree", "Target mean degree (1..16; structured graphs have fixed degree)", "count"});
    parser.addOption({"seed", "Random seed for reproducible graphs (0..2147483647)", "number", "42"});
    QCommandLineOption textMotionOption(QStringLiteral("text-motion-smoke"), QStringLiteral("Check viewport stability while live telemetry updates"));
    parser.addOption(textMotionOption);
    parser.process(app);

    // Main window
    QMainWindow mainWin;
    mainWin.setWindowTitle("ForceView QRhi Graph Demo");
    mainWin.resize(1100, 700);

    QWidget* central = new QWidget(&mainWin);
    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(8);

    ForceViewRhiWidget* view = new ForceViewRhiWidget(central);
    view->setSimulationBackendMode(parser.value(backendOption));
    // Diagnostics are explicitly enabled here, never by the production page.
    view->enablePerformanceDiagnostics();
    view->setMinimumSize(400, 300);
    mainLayout->addWidget(view, 1);

    // Right panel: switch graph
    QWidget* panel = new QWidget(central);
    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* groupBox = new QGroupBox("切换图", panel);
    QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);
    auto* instructions = new QLabel("选择参数后点击应用；相同种子可复现。", groupBox);
    instructions->setWordWrap(true);
    groupLayout->addWidget(instructions);

    QSpinBox* nodeCountSpin = new QSpinBox(groupBox);
    nodeCountSpin->setRange(1, 100000);
    bool validCount = false, validSeed = false;
    const int initialCount = parser.value(nodesOption).toInt(&validCount);
    const int initialSeed = parser.value("seed").toInt(&validSeed);
    if (!validCount || initialCount < 1 || initialCount > 100000 || !validSeed || initialSeed < 0) {
        qCritical("Invalid node count or seed"); return 1;
    }
    nodeCountSpin->setValue(initialCount);
    nodeCountSpin->setSuffix(" 节点");

    auto* presetCombo = new QComboBox(groupBox);
    auto* positionCombo = new QComboBox(groupBox);
    auto* topologyCombo = new QComboBox(groupBox);
    for (auto* combo : {presetCombo, positionCombo, topologyCombo}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }
    for (const auto& preset : graph_demo::presets()) presetCombo->addItem(preset.name, preset.key);
    for (const auto& choice : graph_demo::positionChoices()) {
        positionCombo->addItem(choice.name, choice.key);
        positionCombo->setItemData(positionCombo->count() - 1, choice.description, Qt::ToolTipRole);
    }
    for (const auto& choice : graph_demo::topologyChoices()) {
        topologyCombo->addItem(choice.name, choice.key);
        topologyCombo->setItemData(topologyCombo->count() - 1, choice.description, Qt::ToolTipRole);
    }
    auto* degreeSpin = new QSpinBox(groupBox);
    degreeSpin->setRange(1, 16); degreeSpin->setValue(4);
    auto* seedSpin = new QSpinBox(groupBox);
    seedSpin->setRange(0, 2147483647); seedSpin->setValue(initialSeed);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow("节点数量", nodeCountSpin);
    form->addRow("推荐组合", presetCombo);
    form->addRow("初始位置", positionCombo);
    form->addRow("关联结构", topologyCombo);
    form->addRow("目标平均度", degreeSpin);
    form->addRow("随机种子", seedSpin);
    groupLayout->addLayout(form);
    auto* description = new QLabel(groupBox);
    description->setWordWrap(true);
    groupLayout->addWidget(description);
    QPushButton* applyBtn = new QPushButton("应用 / 重置布局", groupBox);
    auto* newSeedBtn = new QPushButton("换种子并生成", groupBox);
    groupLayout->addWidget(applyBtn); groupLayout->addWidget(newSeedBtn);
    auto* summary = new QLabel(groupBox);
    summary->setWordWrap(true);
    groupLayout->addWidget(summary);
    auto updateChoiceDescription = [=]() {
        const QString topology = topologyCombo->currentData().toString();
        degreeSpin->setEnabled(topology == "random" || topology == "communities" || topology == "scale-free" || topology == "disconnected");
        description->setText(positionCombo->currentData(Qt::ToolTipRole).toString() + "\n\n"
            + topologyCombo->currentData(Qt::ToolTipRole).toString());
    };
    auto selectPreset = [=](int index) {
        const auto& preset = graph_demo::presets()[index];
        const QSignalBlocker positionsBlock(positionCombo), topologyBlock(topologyCombo);
        positionCombo->setCurrentIndex(positionCombo->findData(preset.positions));
        topologyCombo->setCurrentIndex(topologyCombo->findData(preset.topology));
        degreeSpin->setValue(preset.degree);
        updateChoiceDescription();
    };
    QObject::connect(presetCombo, &QComboBox::currentIndexChanged, &mainWin, selectPreset);
    QObject::connect(positionCombo, &QComboBox::currentIndexChanged, &mainWin, updateChoiceDescription);
    QObject::connect(topologyCombo, &QComboBox::currentIndexChanged, &mainWin, updateChoiceDescription);
    const int presetIndex = presetCombo->findData(parser.value("preset"));
    if (presetIndex < 0) { qCritical("Unknown graph preset"); return 1; }
    presetCombo->setCurrentIndex(presetIndex); selectPreset(presetIndex);
    for (const auto& option : {QStringLiteral("positions"), QStringLiteral("topology")}) {
        if (!parser.isSet(option)) continue;
        auto* combo = option == "positions" ? positionCombo : topologyCombo;
        const int index = combo->findData(parser.value(option));
        if (index < 0) { qCritical() << "Unknown" << option; return 1; }
        combo->setCurrentIndex(index);
    }
    if (parser.isSet("degree")) {
        bool valid = false;
        const int value = parser.value("degree").toInt(&valid);
        if (!valid || value < 1 || value > 16) { qCritical("Invalid mean degree"); return 1; }
        degreeSpin->setValue(value);
    }
    panelLayout->addWidget(groupBox);

    auto *testControls = new graph_demo::GraphTestControls(*view, panel);
    panelLayout->addWidget(testControls);

    panelLayout->addStretch();
    auto* sideScroll = new QScrollArea(central);
    sideScroll->setWidgetResizable(true);
    sideScroll->setFixedWidth(280);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setWidget(panel);
    mainLayout->addWidget(sideScroll, 0);

    mainWin.setCentralWidget(central);

    auto applyGraph = [=]() {
        const int count = nodeCountSpin->value();
        const auto graph = graph_demo::generateGraph(count, positionCombo->currentData().toString(),
            topologyCombo->currentData().toString(), degreeSpin->value(), quint32(seedSpin->value()));
        view->setGraph(count, graph.edges, graph.positions, graph.ids, graph.labels, graph.radii, graph.colors);
        testControls->setGraphData(graph);
        summary->setText(QString("当前图：%1 节点 · %2 条边\n实际平均度：%3\n%4 / %5\n种子：%6")
            .arg(count).arg(graph.edges.size() / 2).arg(double(graph.edges.size()) / count, 0, 'f', 2)
            .arg(positionCombo->currentText(), topologyCombo->currentText()).arg(seedSpin->value()));
    };
    QObject::connect(applyBtn, &QPushButton::clicked, &mainWin, applyGraph);
    QObject::connect(newSeedBtn, &QPushButton::clicked, &mainWin, [=]() {
        seedSpin->setValue(seedSpin->value() == seedSpin->maximum() ? 0 : seedSpin->value() + 1);
        applyGraph();
    });
    applyGraph();

    QObject::connect(view, &ForceViewRhiWidget::nodeLeftClicked,
                     [](const QString& nodeId) { qDebug() << "Left-clicked node: id[" << nodeId << "]"; });
    QObject::connect(view, &ForceViewRhiWidget::nodeHovered,
                     [](const QString& nodeId) { if (!nodeId.isEmpty()) qDebug() << "Hovered node: id:" << nodeId; });

    if (parser.isSet(smokeOption)) {
        QObject::connect(view, &ForceViewRhiWidget::paintTimeUpdated, &app, [&app](float) { app.exit(0); });
    }

    if (parser.isSet(textMotionOption)) {
        mainWin.setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
        view->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    mainWin.show();
    QFile motionLog(QCoreApplication::applicationDirPath() + QStringLiteral("/graph-text-motion.tsv"));
    QElapsedTimer motionClock;
    QSet<quint64> viewportSizes;
    QTimer motionSampler;
    if (parser.isSet(textMotionOption)) {
        if (!motionLog.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 4;
        motionClock.start(); motionSampler.setInterval(16);
        QTimer::singleShot(250, &app, [view]() {
            const QPointF center(view->rect().center());
            for (int step = 0; step < 40 && view->getZoom() < 1.1f; ++step) {
                QWheelEvent wheel(center, view->mapToGlobal(center.toPoint()), QPoint(), QPoint(0, 120),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(view, &wheel);
            }
        });
        for (int sample = 1; sample <= 2; ++sample) QTimer::singleShot(sample * 750, &app, [view, sample, &mainWin]() {
            if (auto* canvas = view->findChild<QRhiWidget*>())
                canvas->grabFramebuffer().save(QCoreApplication::applicationDirPath() + QStringLiteral("/graph-text-motion-%1.png").arg(sample));
            if (sample == 1) mainWin.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/graph-demo-layout.png"));
        });
        QObject::connect(&motionSampler, &QTimer::timeout, &app, [&]() {
            if (motionClock.elapsed() < 500) return;
            viewportSizes.insert((quint64(view->width()) << 32) | quint32(view->height()));
            QTextStream stream(&motionLog);
            stream << motionClock.elapsed() << '\t' << view->width() << '\t' << view->height() << '\t' << view->getZoom() << '\n';
        });
        motionSampler.start();
        QTimer::singleShot(2500, &app, [&]() { motionLog.flush(); app.exit(viewportSizes.size() == 1 ? 0 : 3); });
    }
    if (parser.isSet(smokeOption)) {
        QTimer::singleShot(5000, &app, [&app] {
            qCritical("ForceView QRhi smoke test timed out before the first rendered frame");
            app.exit(2);
        });
    }
    return app.exec();
}



