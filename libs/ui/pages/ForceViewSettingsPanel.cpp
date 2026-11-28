#include "ui/pages/ForceViewSettingsPanel.h"

#include "darkeye_ui/components/AnimatedIndicators.h"
#include "darkeye_ui/components/CollapsibleSection.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "graph_view/GraphViewWidget.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace darkeye {

ForceViewSettingsPanel::ForceViewSettingsPanel(ThemeService &themeService,
                                               graph_view::GraphViewWidget &graph,
                                               QSettings *settings, QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("ForceViewSettingsPanel"));
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(250);
    setMaximumWidth(250);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    content->setObjectName(QStringLiteral("ForceViewSettingsContent"));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const auto addSlider = [this, content, settings](QFormLayout *form, const QString &label,
                                                    const QString &key, int minimum, int maximum,
                                                    int defaultValue, auto apply) {
        auto *slider = new ClickableSlider(Qt::Horizontal, nullptr, content);
        slider->setProperty("graphSetting", key);
        slider->setRange(minimum, maximum);
        slider->setValue(defaultValue);
        // Keep the renderer's initial value until a setting has been saved.
        if (settings != nullptr) {
            bool ok = false;
            const int savedValue = settings->value(key).toInt(&ok);
            if (ok) {
                slider->setValue(savedValue);
                apply(slider->value());
            }
        }
        connect(slider, &QSlider::valueChanged, this, [settings, key, apply](int value) {
            apply(value);
            if (settings != nullptr) settings->setValue(key, value);
        });
        form->addRow(new DesignLabel(label, content), slider);
        return slider;
    };
    const auto addToggle = [this, content, settings, &themeService](QFormLayout *form,
                             const QString &label, const QString &key, auto apply) {
        auto *toggle = new ToggleSwitch(40, 24, &themeService, content);
        toggle->setProperty("graphSetting", key);
        const bool enabled = settings == nullptr || settings->value(key, true).toBool();
        toggle->setChecked(enabled);
        apply(enabled);
        connect(toggle, &ToggleSwitch::toggled, this, [settings, key, apply](bool enabled) {
            apply(enabled);
            if (settings != nullptr) settings->setValue(key, enabled);
        });
        form->addRow(new DesignLabel(label, content), toggle);
    };

    auto *view = graph.view();
    auto *effects = new TokenCollapsibleSection(QStringLiteral("效果"), &themeService, content);
    auto *effectForm = new QFormLayout;
    addSlider(effectForm, QStringLiteral("斥力强度"), QStringLiteral("ManyBodyStrength"),
              1000, 50000, 10000, [view](int value) { view->setManyBodyStrength(float(value)); });
    addSlider(effectForm, QStringLiteral("中心力强度"), QStringLiteral("CenterStrength"),
              1, 50, 10, [view](int value) { view->setCenterStrength(float(value) / 1000.0F); });
    addSlider(effectForm, QStringLiteral("连接力强度"), QStringLiteral("LinkStrength"),
              1, 100, 30, [view](int value) { view->setLinkStrength(float(value) / 100.0F); });
    addSlider(effectForm, QStringLiteral("连接距离"), QStringLiteral("LinkDistance"),
              10, 80, 40, [view](int value) { view->setLinkDistance(float(value)); });
    auto *effectContent = new QWidget(content);
    effectContent->setLayout(effectForm);
    effects->addWidget(effectContent);

    auto *display = new TokenCollapsibleSection(QStringLiteral("显示"), &themeService, content);
    auto *displayForm = new QFormLayout;
    addToggle(displayForm, QStringLiteral("显示箭头"), QStringLiteral("ArrowEnabled"),
              [view](bool enabled) { view->setArrowEnabled(enabled); });
    addSlider(displayForm, QStringLiteral("箭头大小"), QStringLiteral("ArrowSize"),
              3, 30, 10, [view](int value) { view->setArrowScale(float(value) / 10.0F); });
    addToggle(displayForm, QStringLiteral("显示图片"), QStringLiteral("ImageOverlayEnabled"),
              [&graph](bool enabled) { graph.setImageOverlayEnabled(enabled); });
    addSlider(displayForm, QStringLiteral("文字渐隐"), QStringLiteral("TextFade"),
              10, 1000, 100, [view](int value) { view->setTextThresholdFactor(float(value) / 100.0F); });
    addSlider(displayForm, QStringLiteral("节点大小"), QStringLiteral("NodeSize"),
              10, 300, 100, [view](int value) { view->setRadiusFactor(float(value) / 100.0F); });
    addSlider(displayForm, QStringLiteral("连线宽度"), QStringLiteral("LineWidth"),
              10, 300, 60, [view](int value) { view->setSideWidthFactor(float(value) / 100.0F); });
    addSlider(displayForm, QStringLiteral("邻居深度"), QStringLiteral("NeighborDepth"),
              1, 5, 2, [view](int value) { view->setNeighborDepth(value); });
    m_displayForm = displayForm;
    m_graphNeighborDepth = addSlider(displayForm, QStringLiteral("图邻居深度"),
                                     QStringLiteral("GraphNeighborDepth"), 1, 5, 3,
                                     [this](int value) { emit graphNeighborDepthChanged(value); });
    connect(this, &ForceViewSettingsPanel::graphNeighborDepthChanged, &graph,
            &graph_view::GraphViewWidget::setEgoRadius);
    displayForm->setRowVisible(m_graphNeighborDepth, false);
    auto *colorColumn = new QVBoxLayout;
    colorColumn->setContentsMargins(0, 0, 0, 0);
    colorColumn->setSpacing(6);
    const auto addColor = [this, colorColumn, content, settings, &graph](const QString &name,
                                   const QString &group, const QColor &defaultColor) {
        const auto key = QStringLiteral("NodeColor/") + group;
        QColor color = defaultColor;
        if (settings != nullptr) {
            const QColor savedColor(settings->value(key).toString());
            if (savedColor.isValid()) {
                color = savedColor;
                graph.setNodeColor(group, color);
            }
        }
        auto *picker = new ColorPicker(color, false, ColorPicker::Shape::Octagon, content);
        picker->setProperty("graphSetting", key);
        connect(picker, &ColorPicker::colorChanged, this,
                [settings, key, &graph, group](const QString &color) {
            graph.setNodeColor(group, QColor(color));
            if (settings != nullptr) settings->setValue(key, color);
        });
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(new DesignLabel(name, content));
        row->addWidget(picker);
        row->addStretch();
        colorColumn->addLayout(row);
    };
    addColor(QStringLiteral("女优"), QStringLiteral("actress"), QColor(QStringLiteral("#ff99cc")));
    addColor(QStringLiteral("作品"), QStringLiteral("work"), QColor(QStringLiteral("#99ccff")));
    addColor(QStringLiteral("中心"), QStringLiteral("center"), QColor(QStringLiteral("#ffd700")));
    addColor(QStringLiteral("默认"), QStringLiteral("default"), QColor(QStringLiteral("#5c5c5c")));
    displayForm->addRow(new DesignLabel(QStringLiteral("节点颜色"), content), colorColumn);
    auto *displayContent = new QWidget(content);
    displayContent->setLayout(displayForm);
    display->addWidget(displayContent);

    layout->addWidget(effects);
    layout->addWidget(display);
    layout->addStretch();
    setWidget(content);
    updateContentWidth();

    // Match the Python panel: a section toggle is measured after Qt has
    // committed the layout, then the parent repositions/resizes this overlay.
    const auto notifyContentSizeChanged = [this, content] {
        QTimer::singleShot(0, this, [this, content] {
            content->updateGeometry();
            if (content->layout() != nullptr) content->layout()->activate();
            updateGeometry();
            emit contentSizeChanged();
        });
    };
    connect(effects, &TokenCollapsibleSection::toggled, this,
            [notifyContentSizeChanged](bool) { notifyContentSizeChanged(); });
    connect(display, &TokenCollapsibleSection::toggled, this,
            [notifyContentSizeChanged](bool) { notifyContentSizeChanged(); });
}

void ForceViewSettingsPanel::setGraphNeighborDepthVisible(bool visible)
{
    m_displayForm->setRowVisible(m_graphNeighborDepth, visible);
    QTimer::singleShot(0, this, [this] {
        widget()->updateGeometry();
        widget()->layout()->activate();
        updateGeometry();
        emit contentSizeChanged();
    });
}

int ForceViewSettingsPanel::graphNeighborDepth() const
{
    return m_graphNeighborDepth->value();
}

void ForceViewSettingsPanel::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    updateContentWidth();
}

void ForceViewSettingsPanel::updateContentWidth()
{
    QWidget *content = widget();
    if (content == nullptr || viewport()->width() <= 0) return;
    // Same as Python's _update_content_width(): when the vertical scrollbar
    // consumes space, reflow the form to the remaining viewport width instead
    // of keeping a wider child clipped on the right.
    content->setFixedWidth(viewport()->width());
    content->setMinimumHeight(0);
    if (content->layout() != nullptr) content->layout()->activate();
}

int ForceViewSettingsPanel::preferredHeight() const
{
    QWidget *content = widget();
    if (content == nullptr) return QScrollArea::sizeHint().height();
    if (content->layout() != nullptr) content->layout()->activate();
    // Include the scroll area's frame so a panel assigned exactly this height
    // still has a viewport large enough for all folded/expanded content.
    return content->sizeHint().height() + frameWidth() * 2;
}

} // namespace darkeye
