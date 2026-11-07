#include "ForcePerformanceOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

ForcePerformanceOverlay::ForcePerformanceOverlay(QWidget* parent) : QFrame(parent)
{
#ifdef Q_OS_WIN
    // Use one CJK-capable UI family for both captions and values. The Windows
    // fixed font (usually Consolas) needs DirectWrite fallback for Chinese.
    const QString uiFamily = QStringLiteral("Microsoft YaHei UI");
    if (QFontDatabase::hasFamily(uiFamily)) {
        QFont uiFont = font();
        uiFont.setFamily(uiFamily);
        setFont(uiFont);
    }
#endif
    setObjectName(QStringLiteral("forcePerformanceOverlay"));
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::ArrowCursor);
    setStyleSheet(QStringLiteral("QFrame#forcePerformanceOverlay { background: palette(window); border: 1px solid palette(mid); border-radius: 6px; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(QStringLiteral("性能监测"), this));
    header->addStretch();
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("closePerformanceOverlay"));
    close->setText(QStringLiteral("×"));
    close->setToolTip(QStringLiteral("隐藏性能表，可通过图内右键菜单再次显示"));
    connect(close, &QToolButton::clicked, this, &ForcePerformanceOverlay::closeRequested);
    header->addWidget(close);
    layout->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* body = new QWidget(scroll);
    auto* form = new QFormLayout(body);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_mode = new QComboBox(body);
    m_mode->setObjectName(QStringLiteral("performanceBackendMode"));
    m_mode->addItem(QStringLiteral("自动"), QStringLiteral("auto"));
    m_mode->addItem(QStringLiteral("GPU"), QStringLiteral("gpu"));
    m_mode->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this]() { emit backendModeChanged(m_mode->currentData().toString()); });
    form->addRow(QStringLiteral("计算后端"), m_mode);
    auto row = [&](const QString& name, const QString& key) {
        auto* value = new QLabel(QStringLiteral("--"), body);
        value->setObjectName(QStringLiteral("performance_") + key);
        value->setWordWrap(true);
        m_values.insert(key, value);
        auto* caption = new QLabel(name, body);
        form->addRow(caption, value);
        return caption;
    };
    row(QStringLiteral("实际后端"), QStringLiteral("backend"));
    row(QStringLiteral("帧率"), QStringLiteral("fps"));
    m_tickCaption = row(QStringLiteral("CPU 单步"), QStringLiteral("tick"));
    row(QStringLiteral("CPU 绘制提交"), QStringLiteral("paint"));
    row(QStringLiteral("缩放"), QStringLiteral("scale"));
    row(QStringLiteral("模拟热度"), QStringLiteral("alpha"));
    m_profile = new QCheckBox(QStringLiteral("GPU 阶段计时"), body);
    m_profile->setObjectName(QStringLiteral("gpuProfilingCheckBox"));
    m_profile->setToolTip(QStringLiteral("当前支持 D3D11；开启增加提交开销，比较正常性能时请关闭。"));
    connect(m_profile, &QCheckBox::toggled, this, &ForcePerformanceOverlay::profilingChanged);
    form->addRow(m_profile);
    row(QStringLiteral("CPU 记录"), QStringLiteral("recordCpuMs"));
    row(QStringLiteral("几何准备"), QStringLiteral("geometryCpuMs"));
    row(QStringLiteral("文字准备"), QStringLiteral("textCpuMs"));
    row(QStringLiteral("GPU 边界"), QStringLiteral("gpuBoundsMs"));
    row(QStringLiteral("GPU 排序"), QStringLiteral("gpuSortMs"));
    row(QStringLiteral("GPU 建树"), QStringLiteral("gpuTreeMs"));
    row(QStringLiteral("GPU 力计算"), QStringLiteral("gpuForceMs"));
    row(QStringLiteral("读回批次"), QStringLiteral("readbackMs"));
    row(QStringLiteral("dispatch/步"), QStringLiteral("dispatches"));
    row(QStringLiteral("模拟步/批次"), QStringLiteral("batchSteps"));
    row(QStringLiteral("已提交模拟步"), QStringLiteral("simulationSteps"));
    row(QStringLiteral("估计完成耗时/步"), QStringLiteral("estimatedStepMs"));
    row(QStringLiteral("绘制路径"), QStringLiteral("drawing"));
    scroll->setWidget(body);
    layout->addWidget(scroll);
    auto* timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this]() { if (isVisible()) refresh(); });
    timer->start();
    hide();
}

void ForcePerformanceOverlay::setBackend(const QString& backend)
{
    m_backend = backend;
    m_tickCaption->setText(backend == QStringLiteral("gpu") ? QStringLiteral("快照延迟") : QStringLiteral("CPU 单步"));
    m_values["tick"]->setToolTip(backend == QStringLiteral("gpu")
        ? QStringLiteral("快照请求至读回回调的延迟，包含排队与帧调度；不是 GPU 物理计算时间。")
        : QStringLiteral("CPU 物理单步与位置快照耗时"));
}
void ForcePerformanceOverlay::setBackendMode(const QString& mode)
{
    const QSignalBlocker block(m_mode);
    m_mode->setCurrentIndex(m_mode->findData(mode));
}
void ForcePerformanceOverlay::setProfilingEnabled(bool enabled)
{
    const QSignalBlocker block(m_profile);
    m_profile->setChecked(enabled);
}
void ForcePerformanceOverlay::refresh()
{
    auto number = [](double value, const QString& suffix = {}) {
        return value < 0 ? QStringLiteral("--") : QString::number(value, 'f', 2) + suffix;
    };
    m_values["backend"]->setText(m_backend.toUpper());
    m_values["fps"]->setText(number(m_fps));
    m_values["tick"]->setText(number(m_tick, QStringLiteral(" ms")));
    m_values["paint"]->setText(number(m_paint, QStringLiteral(" ms")));
    m_values["scale"]->setText(number(m_scale));
    m_values["alpha"]->setText(number(m_alpha));
    const QStringList keys{"recordCpuMs", "geometryCpuMs", "textCpuMs", "gpuBoundsMs", "gpuSortMs", "gpuTreeMs", "gpuForceMs", "readbackMs", "estimatedStepMs"};
    for (const auto& key : keys) m_values[key]->setText(number(m_details.value(key, -1).toDouble(), QStringLiteral(" ms")));
    for (const auto &key : {QStringLiteral("dispatches"), QStringLiteral("batchSteps"), QStringLiteral("simulationSteps")})
        m_values[key]->setText(m_details.contains(key) ? m_details.value(key).toString() : QStringLiteral("--"));
    m_values["drawing"]->setText(m_details.isEmpty() ? QStringLiteral("--") :
        m_details.value("gpuInstances").toBool() ? QStringLiteral("GPU 实例化") : QStringLiteral("CPU 顶点准备"));
}
void ForcePerformanceOverlay::showEvent(QShowEvent* event)
{
    emit profilingChanged(true);
    refresh();
    QFrame::showEvent(event);
}
void ForcePerformanceOverlay::hideEvent(QHideEvent* event)
{
    emit profilingChanged(false);
    QFrame::hideEvent(event);
}
void ForcePerformanceOverlay::mousePressEvent(QMouseEvent* event) { event->accept(); }
void ForcePerformanceOverlay::mouseMoveEvent(QMouseEvent* event) { event->accept(); }
void ForcePerformanceOverlay::mouseReleaseEvent(QMouseEvent* event) { event->accept(); }
void ForcePerformanceOverlay::wheelEvent(QWheelEvent* event) { event->accept(); }
