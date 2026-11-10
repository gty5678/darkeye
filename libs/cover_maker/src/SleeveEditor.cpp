#include "cover_maker/SleeveEditor.h"
#include "cover_maker/CoverEditor.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>

namespace darkeye::cover_maker {
namespace {
class SleevePreview final : public QWidget {
public:
    std::function<SleeveDocument()> document;
    explicit SleevePreview(QWidget *parent) : QWidget(parent) { setMinimumHeight(180); setMaximumHeight(250); }
    void paintEvent(QPaintEvent *) override {
        const auto d = document();
        if (!validSleeve(d)) return;
        QPainter p(this); p.fillRect(rect(), QColor("#141b25"));
        QSizeF size(d.outputSize()); size.scale(QSizeF(width() - 32, height() - 32), Qt::KeepAspectRatio);
        const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        paintSleeve(p, target, d);
        p.setPen(QPen(QColor("#9cb6cf"), 1, Qt::DashLine));
        for (const int x : {d.backWidth, d.backWidth + d.spineWidth}) {
            const qreal at = target.left() + x * target.width() / d.outputSize().width();
            p.drawLine(QPointF(at, target.top()), QPointF(at, target.bottom()));
        }
    }
};
}
SleeveEditor::SleeveEditor(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *explanation = new QLabel(QStringLiteral("完整封套：封底 → 书脊 → 封面。分别编辑三个区域，下方实时预览合成结果。尺寸单位为像素。"));
    explanation->setWordWrap(true); root->addWidget(explanation);
    auto *dimensions = new QHBoxLayout;
    const QStringList names{QStringLiteral("封底宽"), QStringLiteral("书脊宽"), QStringLiteral("封面宽"), QStringLiteral("共同高度")};
    for (int i = 0; i < 4; ++i) {
        dimensions->addWidget(new QLabel(names[i]));
        auto *spin = new QSpinBox; spin->setRange(1, 8192); spin->setSuffix(QStringLiteral(" px"));
        spin->setKeyboardTracking(false); spin->setObjectName(QStringLiteral("sleeveDimension%1").arg(i));
        m_dimensions[i] = spin; dimensions->addWidget(spin);
        connect(spin, &QSpinBox::valueChanged, this, [this](int) {
            if (m_loading) return;
            auto d = document(); d.backWidth = m_dimensions[0]->value(); d.spineWidth = m_dimensions[1]->value();
            d.frontWidth = m_dimensions[2]->value(); d.height = m_dimensions[3]->value();
            if (!validSleeve(d)) {
                m_sizeLabel->setText(QStringLiteral("总宽不能超过 8192 px，已恢复原尺寸。"));
                const int old[] = {m_document.backWidth, m_document.spineWidth, m_document.frontWidth, m_document.height};
                for (int j = 0; j < 4; ++j) { const QSignalBlocker blocker(m_dimensions[j]); m_dimensions[j]->setValue(old[j]); }
                return;
            }
            setDocument(d);
        });
    }
    root->addLayout(dimensions);
    auto *modelRatio = new QPushButton(QStringLiteral("恢复当前 DVD 模型比例 · 1525 : 156 : 1519，高 2160"));
    root->addWidget(modelRatio);
    connect(modelRatio, &QPushButton::clicked, this, [this] {
        auto d = document(); const SleeveDocument preset;
        d.backWidth = preset.backWidth; d.spineWidth = preset.spineWidth;
        d.frontWidth = preset.frontWidth; d.height = preset.height; setDocument(d);
    });
    auto *tabs = new QTabWidget; tabs->setObjectName(QStringLiteral("sleevePanels"));
    const QStringList panelNames{QStringLiteral("1 · 封底"), QStringLiteral("2 · 书脊"), QStringLiteral("3 · 封面")};
    for (int i = 0; i < 3; ++i) {
        m_editors[i] = new CoverEditor(nullptr, true); tabs->addTab(m_editors[i], panelNames[i]);
        connect(m_editors[i], &CoverEditor::compositionChanged, this, [this] { if (!m_loading) refresh(); });
    }
    root->addWidget(tabs, 1);
    m_sizeLabel = new QLabel; root->addWidget(m_sizeLabel);
    auto *preview = new SleevePreview(this); preview->document = [this] { return document(); };
    m_preview = preview; root->addWidget(preview);
    auto *actions = new QHBoxLayout;
    auto *exportButton = new QPushButton(QStringLiteral("合成并导出完整封套 PNG…"));
    auto *apply = new QPushButton(QStringLiteral("应用完整封套到当前条目"));
    actions->addWidget(exportButton); actions->addWidget(apply); root->addLayout(actions);
    connect(exportButton, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出完整封套"), QStringLiteral("sleeve.png"), QStringLiteral("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
        QString error;
        if (!saveSleeve(document(), path, &error)) QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        const auto image = renderSleeve(document());
        if (image.isNull()) QMessageBox::warning(this, QStringLiteral("合成失败"), QStringLiteral("请检查尺寸或可用内存。"));
        else emit applyRequested(image);
    });
    setDocument(m_document); tabs->setCurrentIndex(2);
}
SleeveDocument SleeveEditor::document() const
{
    auto d = m_document;
    for (int i = 0; i < 3; ++i) if (m_editors[i]) d.panels[i] = m_editors[i]->document();
    return d;
}
void SleeveEditor::setDocument(const SleeveDocument &d)
{
    if (!validSleeve(d)) return;
    m_loading = true; m_document = d;
    const int values[] = {d.backWidth, d.spineWidth, d.frontWidth, d.height};
    for (int i = 0; i < 4; ++i) { const QSignalBlocker blocker(m_dimensions[i]); m_dimensions[i]->setValue(values[i]); }
    for (int i = 0; i < 3; ++i) {
        auto panel = d.panels[i];
        const auto newSize = panelRect(d, i).size().toSize();
        if (panel.editableImageLayers && panel.outputSize != newSize) {
            for (auto &layer : panel.imageLayers) {
                const auto source = imageLayerSource(layer);
                if (source.isEmpty()) continue;
                const auto center = layer.frame.center();
                layer.frame.setHeight(layer.frame.width() * newSize.width() * source.height() / source.width() / newSize.height());
                layer.frame.moveCenter(center);
            }
        }
        panel.outputSize = newSize; panel.spineText = i == 1; panel.showImagePlaceholders = false;
        m_editors[i]->setDocument(panel);
    }
    m_loading = false; refresh();
}
void SleeveEditor::refresh()
{
    const auto d = document();
    m_sizeLabel->setText(QStringLiteral("合成预览（封底 | 书脊 | 封面） · %1 × %2 px · 宽度 %3 : %4 : %5 · 辅助线不导出")
        .arg(d.outputSize().width()).arg(d.height).arg(d.backWidth).arg(d.spineWidth).arg(d.frontWidth));
    m_preview->update();
}
}
