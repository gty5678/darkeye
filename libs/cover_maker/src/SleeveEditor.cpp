#include "cover_maker/SleeveEditor.h"
#include "cover_maker/CoverEditor.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace darkeye::cover_maker {
SleeveEditor::SleeveEditor(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    m_editor = new CoverEditor(nullptr, true); m_editor->setObjectName(QStringLiteral("sleeveComposition"));
    root->addWidget(m_editor, 1);
    connect(m_editor, &CoverEditor::compositionChanged, this, [this] { if (!m_loading) refresh(); });
    auto *actions = new QHBoxLayout;
    auto *exportButton = new QPushButton(QStringLiteral("导出 PNG…"));
    exportButton->setObjectName(QStringLiteral("sleeveExport"));
    auto *apply = new QPushButton(QStringLiteral("应用"));
    m_apply=apply;
    apply->setObjectName(QStringLiteral("sleeveApply"));
    auto *title = new QLabel(QStringLiteral("封套制作器"));
    QFont titleFont = title->font(); titleFont.setPointSize(18); titleFont.setBold(true); title->setFont(titleFont);
    actions->addWidget(title); actions->addStretch();
    auto *guides = new QCheckBox(QStringLiteral("显示参考线与区域名"));
    guides->setObjectName(QStringLiteral("showSleeveGuides"));
    guides->setChecked(true);
    connect(guides, &QCheckBox::toggled, m_editor, &CoverEditor::setGuidesVisible);
    actions->addWidget(guides);
    auto *background = new QPushButton(QStringLiteral("画布底色…"));
    background->setObjectName(QStringLiteral("sleeveBackground"));actions->addWidget(background);
    connect(background,&QPushButton::clicked,m_editor,&CoverEditor::chooseBackgroundColor);
    auto *settings = new QPushButton(QStringLiteral("画布设置…"));
    settings->setObjectName(QStringLiteral("sleeveSettings"));
    actions->addWidget(settings);
    connect(settings, &QPushButton::clicked, this, [this] {
        QDialog dialog(this); dialog.setObjectName(QStringLiteral("sleeveSettingsDialog"));
        dialog.setWindowTitle(QStringLiteral("画布设置"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout; layout->addLayout(form);
        const auto current = document();
        const int values[] = {current.backWidth,current.spineWidth,current.frontWidth,current.height};
        const QStringList names{QStringLiteral("封底宽度"),QStringLiteral("书脊宽度"),QStringLiteral("封面宽度"),QStringLiteral("共同高度")};
        std::array<QSpinBox *,4> dimensions{};
        for(int i=0;i<4;++i) {
            auto *spin = new QSpinBox; spin->setRange(1,8192); spin->setSuffix(QStringLiteral(" px"));
            spin->setKeyboardTracking(false);spin->setValue(values[i]);
            spin->setObjectName(QStringLiteral("sleeveDimension%1").arg(i));dimensions[i]=spin;
            form->addRow(names[i],spin);
        }
        auto *summary = new QLabel; summary->setWordWrap(true); layout->addWidget(summary);
        auto *restore = new QPushButton(QStringLiteral("恢复 DVD 默认比例"));
        restore->setObjectName(QStringLiteral("restoreSleeveDimensions"));layout->addWidget(restore);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        const auto update = [dimensions,summary,buttons] {
            const int width = dimensions[0]->value()+dimensions[1]->value()+dimensions[2]->value();
            const bool valid = width<=8192;
            buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
            summary->setText(valid
                ? QStringLiteral("完整封套：%1 × %2 px\n宽度比例：%3 : %4 : %5").arg(width).arg(dimensions[3]->value())
                    .arg(dimensions[0]->value()).arg(dimensions[1]->value()).arg(dimensions[2]->value())
                : QStringLiteral("总宽不能超过 8192 px，请减小分区宽度。"));
        };
        for(auto *spin:dimensions)connect(spin,&QSpinBox::valueChanged,&dialog,[update](int){update();});
        connect(restore,&QPushButton::clicked,&dialog,[dimensions]{
            const SleeveDocument preset; const int defaults[]={preset.backWidth,preset.spineWidth,preset.frontWidth,preset.height};
            for(int i=0;i<4;++i)dimensions[i]->setValue(defaults[i]);
        });
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        update();dialog.resize(380,300);
        if(dialog.exec()!=QDialog::Accepted)return;
        auto d = document();d.backWidth=dimensions[0]->value();d.spineWidth=dimensions[1]->value();
        d.frontWidth=dimensions[2]->value();d.height=dimensions[3]->value();setDocument(d,false);
    });
    actions->addWidget(exportButton); actions->addWidget(apply); root->insertLayout(0, actions);
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
    setDocument(m_document);
}
void SleeveEditor::setApplyVisible(bool visible) {m_apply->setVisible(visible);}
SleeveDocument SleeveEditor::document() const
{
    auto d = m_document;
    d.unified = true;
    if (m_editor) {
        d.composition = m_editor->document();const auto widths=d.composition.sleeveWidths;
        if(widths[0]>0 && widths[1]>0 && widths[2]>0 && widths[0]+widths[1]+widths[2]==d.composition.outputSize.width()) {
            d.backWidth=widths[0];d.spineWidth=widths[1];d.frontWidth=widths[2];d.height=d.composition.outputSize.height();
        }
    }
    return d;
}
void SleeveEditor::setDocument(const SleeveDocument &d, bool resetHistory)
{
    if (!validSleeve(d)) return;
    const auto composition = unifiedComposition(d);
    m_loading = true; m_document = d;
    m_document.unified = true;
    m_document.composition = composition;
    m_document.composition.sleeveWidths={d.backWidth,d.spineWidth,d.frontWidth};
    m_editor->setDocument(m_document.composition,resetHistory);
    m_loading = false; refresh();
}
void SleeveEditor::refresh()
{
    const auto d = document();
    m_editor->setGuideLines({qreal(d.backWidth)/d.outputSize().width(),
        qreal(d.backWidth+d.spineWidth)/d.outputSize().width()});
}
}
