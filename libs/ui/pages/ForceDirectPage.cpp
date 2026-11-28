#include "ui/pages/ForceDirectPage.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/StateToggleButton.h"
#include "darkeye_ui/components/TokenControls.h"
#include "graph/GraphManager.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "graph_view/GraphViewWidget.h"
#include "settings/Paths.h"
#include "ui/pages/ForceViewSettingsPanel.h"

#include <QColor>
#include <QLabel>
#include <QPalette>
#include <QSettings>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye {

ForceDirectPage::ForceDirectPage(ThemeService &themeService, graph::GraphManager &graphManager,
                                 QWidget *parent, const settings::Paths *paths, bool localGraph)
    : LazyWidget(parent), m_themeService(themeService), m_graphManager(graphManager)
{
    // QRhiWidget must already belong to the top-level widget tree when that
    // window is shown for the first time.  Keep only the database work lazy;
    // creating the renderer later leaves QRhiWidget without a QRhi forever.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_graphView = new graph_view::GraphViewWidget(m_graphManager, this);
    QSettings *settings = nullptr;
    if (paths != nullptr) {
        m_graphView->setImageDirectories(paths->actressImageDirectory(), paths->workCoverDirectory());
        settings = new QSettings(paths->settingsFile(), QSettings::IniFormat, this);
        settings->beginGroup(localGraph ? QStringLiteral("Graph/Local") : QStringLiteral("Graph/Global"));
    }
    layout->addWidget(m_graphView, 1);

    // The first QRhi render target is not ready when this stacked page first
    // becomes visible. Cover it with the theme background until a frame has
    // actually been submitted, matching Python's seamless first entry.
    m_loadingOverlay = new QLabel(QStringLiteral("正在生成力导向图..."), this);
    m_loadingOverlay->setObjectName(QStringLiteral("graphLoadingOverlay"));
    m_loadingOverlay->setAlignment(Qt::AlignCenter);
    m_loadingOverlay->setAutoFillBackground(true);
    connect(m_graphView->view(), &ForceViewRhiWidget::firstFrameSubmitted,
            m_loadingOverlay, [overlay = m_loadingOverlay] {
                // Submission precedes composition into the window. Keep the
                // cover through the next display refresh to avoid a black gap.
                QTimer::singleShot(32, overlay, &QWidget::hide);
            });

    if (!localGraph) {
        m_favoriteOnly = new TokenCheckBox(QStringLiteral("仅显示收藏作品图"), this);
        const auto key = QStringLiteral("FavoriteOnly");
        m_favoriteOnly->setProperty("graphSetting", key);
        m_favoriteOnly->setMinimumWidth(160);
        connect(m_favoriteOnly, &TokenCheckBox::toggled, m_graphView,
                &graph_view::GraphViewWidget::setFavoriteOnly);
        if (settings != nullptr) {
            m_favoriteOnly->setChecked(settings->value(key, false).toBool());
            connect(m_favoriteOnly, &TokenCheckBox::toggled, this,
                    [settings, key](bool enabled) { settings->setValue(key, enabled); });
        }
    }

    m_settingsButton = new StateToggleButton(QStringLiteral("settings"), QStringLiteral("x"),
                                              24, 32, &m_themeService, this);
    m_settingsButton->setToolTip(QStringLiteral("关系图设置"));
    m_settingsPanel = new ForceViewSettingsPanel(m_themeService, *m_graphView, settings, this);
    m_settingsPanel->hide();
    m_egoRadius = m_settingsPanel->graphNeighborDepth();

    connect(m_settingsButton, &StateToggleButton::stateChanged, this, [this](bool visible) {
        m_settingsPanel->setVisible(visible);
        updateOverlayGeometry();
    });
    connect(m_settingsPanel, &ForceViewSettingsPanel::contentSizeChanged, this,
            &ForceDirectPage::updateOverlayGeometry);
    connect(m_settingsPanel, &ForceViewSettingsPanel::graphNeighborDepthChanged,
            this, [this](int radius) { m_egoRadius = radius; });
    connect(m_graphView, &graph_view::GraphViewWidget::nodeLeftClicked, this,
            [this](const QString &nodeId) {
                bool ok = false;
                const qint64 id = nodeId.mid(1).toLongLong(&ok);
                if (!ok || id <= 0) return;
                if (nodeId.startsWith(QLatin1Char('w'))) emit workRequested(id);
                if (nodeId.startsWith(QLatin1Char('a'))) emit actressRequested(id);
            });
    connect(&m_themeService, &ThemeService::themeChanged, this,
            [this](ThemeId) { applyTheme(); });

    applyTheme();
}

void ForceDirectPage::lazyLoad()
{
    m_graphManager.scheduleInitialize();
    m_settingsPanel->setGraphNeighborDepthVisible(false);
    m_needsRefresh = false;
}

void ForceDirectPage::showEvent(QShowEvent *event)
{
    const bool initialized = isInitialized();
    LazyWidget::showEvent(event);
    if (initialized && m_needsRefresh) refreshGraph();
    updateOverlayGeometry();
}

void ForceDirectPage::resizeEvent(QResizeEvent *event)
{
    LazyWidget::resizeEvent(event);
    updateOverlayGeometry();
}

void ForceDirectPage::updateOverlayGeometry()
{
    constexpr int margin = 2;
    if (width() <= 0 || height() <= 0 || m_settingsButton == nullptr) return;
    if (m_loadingOverlay != nullptr && m_loadingOverlay->isVisible()) {
        m_loadingOverlay->setGeometry(rect());
        m_loadingOverlay->raise();
    }
    const QSize buttonSize = m_settingsButton->size();
    m_settingsButton->move(width() - buttonSize.width() - margin, margin);
    m_settingsButton->raise();
    if (m_settingsPanel != nullptr && m_settingsPanel->isVisible()) {
        const int panelWidth = qMin(250, width() - margin * 2);
        // Start compact while all sections are folded.  On expansion, grow to
        // the content height, bounded only by the available graph viewport.
        const int panelHeight = qMin(m_settingsPanel->preferredHeight(),
                                     height() - margin * 2);
        m_settingsPanel->resize(panelWidth, panelHeight);
        m_settingsPanel->move(width() - panelWidth - margin, margin);
        m_settingsPanel->raise();
        m_settingsButton->raise();
    }
    if (m_favoriteOnly != nullptr && m_favoriteOnly->isVisible()) {
        const QSize toggleSize = m_favoriteOnly->sizeHint();
        m_favoriteOnly->resize(toggleSize);
        m_favoriteOnly->move(qMax(margin, width() - toggleSize.width() - margin),
                             qMax(margin, height() - toggleSize.height() - margin));
        m_favoriteOnly->raise();
    }
}

void ForceDirectPage::refreshGraph()
{
    m_needsRefresh = true;
    if (isInitialized() && isVisible() && m_graphView != nullptr) {
        m_graphView->refreshGraph();
        m_needsRefresh = false;
    }
}

void ForceDirectPage::setEgoGraph(const QString &centerId, int radius)
{
    initialize();
    m_settingsPanel->setGraphNeighborDepthVisible(!centerId.isEmpty());
    if (radius >= 0) m_egoRadius = radius;
    m_graphView->setEgoGraph(centerId, m_egoRadius);
}

void ForceDirectPage::showFilteredWorks(QSet<qint64> workIds)
{
    initialize();
    m_settingsPanel->setGraphNeighborDepthVisible(false);
    QSet<QString> nodeIds;
    nodeIds.reserve(workIds.size());
    for (qint64 id : workIds) nodeIds.insert(QStringLiteral("w%1").arg(id));
    m_graphView->showFilteredWorks(std::move(nodeIds));
}

void ForceDirectPage::showFilteredActresses(const QSet<qint64> &actressIds)
{
    initialize();
    m_settingsPanel->setGraphNeighborDepthVisible(false);
    QSet<QString> nodeIds;
    nodeIds.reserve(actressIds.size());
    for (qint64 id : actressIds) nodeIds.insert(QStringLiteral("a%1").arg(id));
    m_graphView->showFilteredActresses(std::move(nodeIds));
}

void ForceDirectPage::showEmptyGraph()
{
    initialize();
    m_settingsPanel->setGraphNeighborDepthVisible(false);
    if (m_graphView != nullptr) m_graphView->showEmpty();
}

void ForceDirectPage::applyTheme()
{
    if (m_graphView == nullptr) return;
    auto *view = m_graphView->view();
    const ThemeTokens tokens = m_themeService.currentTokens();
    const QColor background(tokens.pageBackground);
    view->setBackgroundColor(background);
    view->setEdgeColor(QColor(tokens.border));
    view->setEdgeDimColor(QColor(tokens.pageBackground));
    view->setBaseColor(QColor(tokens.text));
    view->setDimColor(QColor(tokens.pageBackground));
    view->setHoverColor(QColor(tokens.primary));
    view->setTextColor(QColor(tokens.text));
    view->setTextDimColor(QColor(tokens.pageBackground));
    QPalette overlayPalette = m_loadingOverlay->palette();
    overlayPalette.setColor(QPalette::Window, background);
    overlayPalette.setColor(QPalette::WindowText, QColor(tokens.text));
    m_loadingOverlay->setPalette(overlayPalette);
}

} // namespace darkeye
