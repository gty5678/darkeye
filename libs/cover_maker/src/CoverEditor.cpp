#include "cover_maker/CoverEditor.h"
#include "ImageCropDialog.h"
#include <QFileInfo>
#include <QComboBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <algorithm>
#include <cmath>
#include <functional>

namespace darkeye::cover_maker {
namespace {
class Canvas final : public QWidget {
public:
    CoverDocument &doc;
    int selected = -1;
    int selectedText = -1;
    std::function<void(int)> textSelected;
    std::function<void(int)> selectionChanged;
    std::function<void()> edited;
    explicit Canvas(CoverDocument &d, QWidget *parent) : QWidget(parent), doc(d) {
        setObjectName(QStringLiteral("coverCanvas"));
        setMinimumSize(300, 320); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::OpenHandCursor);
    }
    QRectF area() const {
        QSizeF size(doc.outputSize);
        size.scale(QSizeF(std::max(1, width() - 48), std::max(1, height() - 48)), Qt::KeepAspectRatio);
        return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), QColor("#141b25"));
        const QRectF a = area(); paintCover(p, a, doc);
        if (selectedText >= 0 && selectedText < doc.textLayers.size()) {
            p.save(); p.translate(a.topLeft());
            p.scale(a.width() / doc.outputSize.width(), a.height() / doc.outputSize.height());
            const auto &layer = doc.textLayers[selectedText];
            p.setTransform(textLayerTransform(layer, doc.outputSize), true);
            p.setPen(QPen(QColor("#ffcf57"), 2 * doc.outputSize.width() / a.width(), Qt::DashLine));
            p.drawRect(textLayerRect(layer)); p.restore(); return;
        }
        if (selected < 0 || selected >= doc.imageLayers.size()) return;
        const QRectF frame = imageLayerRect(doc.imageLayers[selected],doc.outputSize);
        const QRectF display(a.topLeft() + QPointF(frame.x()*a.width()/doc.outputSize.width(),frame.y()*a.height()/doc.outputSize.height()),
                             QSizeF(frame.width()*a.width()/doc.outputSize.width(),frame.height()*a.height()/doc.outputSize.height()));
        p.setPen(QPen(QColor("#57bbff"),2,Qt::DashLine)); p.drawRect(display);
        p.setBrush(QColor("#57bbff"));
        for (const auto &point : {display.topLeft(),display.topRight(),display.bottomLeft(),display.bottomRight()})
            p.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
    }
    QPointF documentPoint(const QPointF &point) const {
        const auto a=area(); return {(point.x()-a.x())*doc.outputSize.width()/a.width(),(point.y()-a.y())*doc.outputSize.height()/a.height()};
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button()!=Qt::LeftButton) return;
        const auto pos=documentPoint(event->position()); resizing=false;
        // Handles take precedence even when an image edge is just outside the panel.
        if (selectedText < 0 && selected>=0 && selected<doc.imageLayers.size()) {
            const auto frame=imageLayerRect(doc.imageLayers[selected],doc.outputSize);
            const QPointF corners[]={frame.topLeft(),frame.topRight(),frame.bottomLeft(),frame.bottomRight()};
            for (int i=0;i<4;++i) {
                const auto delta=pos-corners[i]; const qreal radius=8*doc.outputSize.width()/area().width();
                if (std::abs(delta.x())<=radius && std::abs(delta.y())<=radius) {
                    anchor=corners[3-i]; originalCorner=corners[i];
                    resizing=true; dragging=true; return;
                }
            }
        }
        if (!area().contains(event->position())) return;
        selectedText=-1;
        for(int i=int(doc.textLayers.size())-1;i>=0;--i) {
            const auto &layer=doc.textLayers[i];
            if(!layer.text.isEmpty() && textLayerRect(layer).contains(textLayerTransform(layer,doc.outputSize).inverted().map(pos))) {selectedText=i;break;}
        }
        if(textSelected) textSelected(selectedText);
        selected=-1;
        if(selectedText<0) for(int i=int(doc.imageLayers.size())-1;i>=0;--i)
            if(imageLayerRect(doc.imageLayers[i],doc.outputSize).contains(pos)) {selected=i;break;}
        if(selectionChanged) selectionChanged(selected);
        last=event->position(); dragging=selectedText>=0 || selected>=0; setCursor(Qt::ClosedHandCursor); update();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!dragging) return;
        if (selectedText >= 0 && selectedText < doc.textLayers.size()) {
            auto &layer = doc.textLayers[selectedText];
            const QPointF delta = event->position() - last;
            layer.center.setX(std::clamp(layer.center.x() + delta.x() / area().width(), 0., 1.));
            layer.center.setY(std::clamp(layer.center.y() + delta.y() / area().height(), 0., 1.));
            last = event->position(); update(); if (edited) edited(); return;
        }
        if(selected<0 || selected>=doc.imageLayers.size()) return;
        auto &layer=doc.imageLayers[selected];
        if(resizing) {
            const auto vector=originalCorner-anchor;
            const auto current=documentPoint(event->position())-anchor;
            const qreal factor=std::clamp(QPointF::dotProduct(current,vector)/QPointF::dotProduct(vector,vector),.05,20.);
            const QRectF frame(anchor,anchor+vector*factor);
            const auto normalized=frame.normalized();
            if (normalized.width()/doc.outputSize.width()<.001 || normalized.height()/doc.outputSize.height()<.001
                || normalized.width()/doc.outputSize.width()>20 || normalized.height()/doc.outputSize.height()>20) return;
            layer.frame={normalized.x()/doc.outputSize.width(),normalized.y()/doc.outputSize.height(),
                         normalized.width()/doc.outputSize.width(),normalized.height()/doc.outputSize.height()};
        } else {
            const QPointF delta=event->position()-last;
            layer.frame.translate(delta.x()/area().width(),delta.y()/area().height());
        }
        last=event->position(); update(); if(edited) edited();
    }
    void mouseReleaseEvent(QMouseEvent *) override { dragging=false; resizing=false; setCursor(Qt::OpenHandCursor); }
    void scaleImage(qreal factor) {
        if(selected<0 || selected>=doc.imageLayers.size()) return;
        auto &frame=doc.imageLayers[selected].frame;
        const qreal width=frame.width()*factor, height=frame.height()*factor;
        if(width<.001 || height<.001 || width>20 || height>20) return;
        const auto center=frame.center(); frame.setSize({width,height}); frame.moveCenter(center);
        update(); if(edited) edited();
    }
    void wheelEvent(QWheelEvent *event) override {
        if(selectedText>=0 || selected<0 || !area().contains(event->position())) {event->ignore();return;}
        scaleImage(std::pow(1.0015,event->angleDelta().y())); event->accept();
    }

private:
    bool resizing = false;
    QPointF anchor, originalCorner;
    bool dragging = false;
    QPointF last;
};
}
CoverEditor::CoverEditor(QWidget *parent, bool panelMode) : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    auto *canvas = new Canvas(m_document, this); m_canvas = canvas;
    root->addWidget(canvas, 1);
    canvas->edited = [this] { emit compositionChanged(); };
    auto *panel = new QWidget(this); panel->setMinimumWidth(280);
    auto *form = new QFormLayout(panel);
    auto *heading = new QLabel(QStringLiteral("封面制作器"));
    QFont font = heading->font(); font.setPointSize(18); font.setBold(true); heading->setFont(font);
    form->addRow(heading);
    auto *ratio = new QComboBox;
    ratio->addItems({QStringLiteral("2:3 · 1200 × 1800"), QStringLiteral("3:4 · 1200 × 1600"), QStringLiteral("16:9 · 1920 × 1080")});
    form->addRow(QStringLiteral("画布比例"), ratio);
    if (panelMode) form->setRowVisible(ratio, false);
    auto *layout = new QComboBox; layout->setObjectName(QStringLiteral("imageLayout"));
    layout->addItems({QStringLiteral("自由摆放"),QStringLiteral("单图铺满"),QStringLiteral("上下双图"),QStringLiteral("主图 + 小图")});
    form->addRow(QStringLiteral("快速布局"),layout);
    auto *slot=new QComboBox; slot->setObjectName(QStringLiteral("imageLayers")); slot->setPlaceholderText(QStringLiteral("请选择图片图层")); form->addRow(QStringLiteral("图片图层"),slot);
    auto *import = new QPushButton(QStringLiteral("添加图片…")); form->addRow(import);
    auto *reset = new QPushButton(QStringLiteral("完整原图适应画布")); reset->setObjectName(QStringLiteral("resetImage")); form->addRow(reset);
    auto *crop = new QPushButton(QStringLiteral("裁剪当前图片…")); crop->setObjectName(QStringLiteral("cropImage")); form->addRow(crop);
    auto *zoomButtons=new QWidget; auto *zoomRow=new QHBoxLayout(zoomButtons); zoomRow->setContentsMargins(0,0,0,0);
    auto *smaller=new QPushButton(QStringLiteral("缩小 −")); auto *larger=new QPushButton(QStringLiteral("放大 +"));
    zoomRow->addWidget(smaller);zoomRow->addWidget(larger);form->addRow(zoomButtons);
    auto *clear = new QPushButton(QStringLiteral("删除当前图片")); clear->setObjectName(QStringLiteral("removeImage")); form->addRow(clear);
    auto *order=new QWidget;auto *orderRow=new QHBoxLayout(order);orderRow->setContentsMargins(0,0,0,0);
    auto *raise=new QPushButton(QStringLiteral("上移一层"));auto *lower=new QPushButton(QStringLiteral("下移一层"));
    orderRow->addWidget(raise);orderRow->addWidget(lower);form->addRow(order);
    const auto syncImages=[this,canvas,slot,reset,crop,smaller,larger,clear,raise,lower] {
        const QSignalBlocker blocker(slot);slot->clear();
        for(int i=0;i<m_document.imageLayers.size();++i) slot->addItem(QStringLiteral("%1 · %2").arg(i+1).arg(m_document.imageLayers[i].name));
        slot->setCurrentIndex(canvas->selected);
        const bool valid=canvas->selected>=0 && canvas->selected<m_document.imageLayers.size();
        for(QWidget *w : std::initializer_list<QWidget *>{reset,crop,smaller,larger,clear,raise,lower}) w->setEnabled(valid);
    };
    connect(slot,&QComboBox::currentIndexChanged,this,[canvas,syncImages](int i){
        canvas->selected=i;
        if(i>=0) {canvas->selectedText=-1;if(canvas->textSelected) canvas->textSelected(-1);}
        syncImages();canvas->update();
    });
    canvas->selectionChanged=[syncImages](int){syncImages();};
    connect(this,&CoverEditor::documentChanged,this,syncImages);
    connect(smaller,&QPushButton::clicked,this,[canvas]{canvas->scaleImage(1/1.1);});
    connect(larger,&QPushButton::clicked,this,[canvas]{canvas->scaleImage(1.1);});
    connect(import,&QPushButton::clicked,this,[this,canvas,syncImages]{
        const auto paths=QFileDialog::getOpenFileNames(this,QStringLiteral("添加图片"),{},QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"));
        for(const auto &path:paths) {
            QImageReader reader(path);reader.setAutoTransform(true);const auto original=reader.size();
            if(original.width()>4096 || original.height()>4096) reader.setScaledSize(original.scaled(4096,4096,Qt::KeepAspectRatio));
            const auto image=reader.read();
            if(image.isNull()) {QMessageBox::warning(this,QStringLiteral("读取失败"),QFileInfo(path).fileName()+QStringLiteral(": ")+reader.errorString());continue;}
            enableImageLayers(m_document);m_document.imageLayers.append(makeImageLayer(image,m_document.outputSize,QFileInfo(path).fileName()));
            canvas->selected=int(m_document.imageLayers.size())-1;
        }
        if(paths.isEmpty()) return;
        canvas->selectedText=-1;if(canvas->textSelected) canvas->textSelected(-1);syncImages();refresh();
    });
    connect(reset,&QPushButton::clicked,this,[this,canvas]{
        const int i=canvas->selected;if(i<0 || i>=m_document.imageLayers.size())return;
        auto &layer=m_document.imageLayers[i];layer=makeImageLayer(layer.image,m_document.outputSize,layer.name);refresh();
    });
    connect(clear,&QPushButton::clicked,this,[this,canvas,syncImages]{
        const int i=canvas->selected;if(i<0 || i>=m_document.imageLayers.size())return;
        m_document.imageLayers.removeAt(i);canvas->selected=std::min(i,int(m_document.imageLayers.size())-1);syncImages();refresh();
    });
    connect(crop,&QPushButton::clicked,this,[this,canvas]{
        const int i=canvas->selected;if(i<0 || i>=m_document.imageLayers.size())return;
        auto &layer=m_document.imageLayers[i];ImageCropDialog dialog(layer.image,layer.crop,this);
        if(dialog.exec()!=QDialog::Accepted)return;
        const auto center=layer.frame.center();layer.crop=dialog.canvas->crop;
        const auto source=imageLayerSource(layer);
        const qreal height=layer.frame.width()*m_document.outputSize.width()*source.height()/source.width()/m_document.outputSize.height();
        layer.frame.setHeight(height);layer.frame.moveCenter(center);refresh();
    });
    const auto reorder=[this,canvas,syncImages](int delta){
        const int from=canvas->selected,to=from+delta;
        if(from<0 || to<0 || to>=m_document.imageLayers.size())return;
        m_document.imageLayers.swapItemsAt(from,to);canvas->selected=to;syncImages();refresh();
    };
    connect(raise,&QPushButton::clicked,this,[reorder]{reorder(1);});
    connect(lower,&QPushButton::clicked,this,[reorder]{reorder(-1);});
    connect(layout,&QComboBox::activated,this,[this,canvas,syncImages](int value){
        if(value<=0)return;
        const auto preset=static_cast<Layout>(value-1);
        for(int i=0;i<std::min(2,int(m_document.imageLayers.size()));++i) {
            const auto frame=slotRect(preset,i,QSizeF(m_document.outputSize));if(frame.isEmpty())continue;
            auto &layer=m_document.imageLayers[i];ImageSlot source;source.image=layer.image;
            const auto image=imageRect(source,frame);
            layer.frame={frame.x()/m_document.outputSize.width(),frame.y()/m_document.outputSize.height(),frame.width()/m_document.outputSize.width(),frame.height()/m_document.outputSize.height()};
            layer.crop={(frame.x()-image.x())/image.width(),(frame.y()-image.y())/image.height(),frame.width()/image.width(),frame.height()/image.height()};
        }
        canvas->selectedText=-1;if(canvas->textSelected)canvas->textSelected(-1);syncImages();refresh();
    });
    syncImages();
    auto *background = new QPushButton(QStringLiteral("设置区域底色…")); form->addRow(background);
    connect(background, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(m_document.backgroundColor, this, QStringLiteral("选择底色"));
        if (color.isValid()) { m_document.backgroundColor = color; refresh(); }
    });
    auto *textChoice = new QComboBox; textChoice->setObjectName(QStringLiteral("textLayers")); textChoice->setPlaceholderText(QStringLiteral("请选择文字图层"));
    form->addRow(QStringLiteral("文字图层"), textChoice);
    auto *textActions = new QWidget; auto *textButtons = new QHBoxLayout(textActions); textButtons->setContentsMargins(0,0,0,0);
    auto *addText = new QPushButton(QStringLiteral("添加文字")); addText->setObjectName(QStringLiteral("addText"));
    auto *removeText = new QPushButton(QStringLiteral("删除文字")); removeText->setObjectName(QStringLiteral("removeText"));
    textButtons->addWidget(addText); textButtons->addWidget(removeText); form->addRow(textActions);
    auto *content = new QPlainTextEdit; content->setObjectName(QStringLiteral("textContent")); content->setFixedHeight(72);
    form->addRow(QStringLiteral("内容"), content);
    auto *family = new QComboBox; family->setObjectName(QStringLiteral("textFont"));
    family->addItem(QStringLiteral("系统默认"), QString{});
    for (const auto &name : QFontDatabase::families()) family->addItem(name, name);
    form->addRow(QStringLiteral("字体"), family);
    auto *size = new QSpinBox; size->setObjectName(QStringLiteral("textSize")); size->setRange(6,1000); size->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("字号"), size);
    auto *bold = new QCheckBox(QStringLiteral("加粗")); form->addRow(bold);
    auto *writing = new QComboBox; writing->setObjectName(QStringLiteral("textWritingMode"));
    writing->addItems({QStringLiteral("横排"), QStringLiteral("竖排（文字正立）")});
    form->addRow(QStringLiteral("文字排列"), writing);
    auto *rotation = new QComboBox; rotation->setObjectName(QStringLiteral("textRotation")); rotation->addItems({QStringLiteral("0°"),QStringLiteral("90°（书脊）"),QStringLiteral("180°"),QStringLiteral("270°")});
    form->addRow(QStringLiteral("旋转"), rotation);
    auto *color = new QPushButton(QStringLiteral("文字颜色…")); form->addRow(color);
    const auto syncText = [this, canvas, content, family, size, bold, writing, rotation, color, removeText] {
        const int i = canvas->selectedText;
        const bool valid = i >= 0 && i < m_document.textLayers.size();
        for (QWidget *w : std::initializer_list<QWidget *>{content, family, size, bold, writing, rotation, color, removeText}) w->setEnabled(valid);
        const QSignalBlocker a(content), b(family), c(size), d(bold), e(rotation), f(writing);
        if (!valid) { content->clear(); return; }
        const auto &layer = m_document.textLayers[i];
        content->setPlainText(layer.text);
        int fontIndex = family->findData(layer.fontFamily);
        if (fontIndex < 0) { family->addItem(layer.fontFamily, layer.fontFamily); fontIndex = family->count() - 1; }
        family->setCurrentIndex(fontIndex); size->setValue(layer.fontSize);
        bold->setChecked(layer.bold); writing->setCurrentIndex(layer.vertical ? 1 : 0); rotation->setCurrentIndex((qRound(layer.rotation / 90) % 4 + 4) % 4);
    };
    const auto reloadTextList = [this, canvas, textChoice, syncText] {
        const QSignalBlocker blocker(textChoice); textChoice->clear();
        for (int i = 0; i < m_document.textLayers.size(); ++i)
            textChoice->addItem(QStringLiteral("%1 · %2").arg(i+1).arg(m_document.textLayers[i].text.left(18).replace(u'\n', u' ')));
        textChoice->setCurrentIndex(canvas->selectedText); syncText();
    };
    connect(textChoice, &QComboBox::currentIndexChanged, this, [canvas, syncText](int i) {
        canvas->selectedText = i;
        if(i>=0) {canvas->selected=-1;if(canvas->selectionChanged)canvas->selectionChanged(-1);}
        syncText(); canvas->update();
    });
    canvas->textSelected = [textChoice](int i) { textChoice->setCurrentIndex(i); };
    connect(addText, &QPushButton::clicked, this, [this, canvas, reloadTextList, syncImages] {
        enableTextLayers(m_document); TextLayer layer;
        layer.fontSize = std::max(6, qRound(m_document.outputSize.width() * (m_document.spineText ? .38 : .065)));
        layer.vertical = m_document.spineText;
        m_document.textLayers.append(layer); canvas->selectedText = int(m_document.textLayers.size()) - 1;
        if(canvas->selectedText>=0) {canvas->selected=-1;syncImages();}
        reloadTextList(); refresh();
    });
    connect(removeText, &QPushButton::clicked, this, [this, canvas, reloadTextList, syncImages] {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers.removeAt(i); canvas->selectedText = std::min(i, int(m_document.textLayers.size()) - 1);
        if(canvas->selectedText>=0) {canvas->selected=-1;syncImages();}
        reloadTextList(); refresh();
    });
    connect(content, &QPlainTextEdit::textChanged, this, [this, canvas, content, textChoice] {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].text = content->toPlainText();
        textChoice->setItemText(i, QStringLiteral("%1 · %2").arg(i+1).arg(content->toPlainText().left(18).replace(u'\n', u' '))); refresh();
    });
    connect(family, &QComboBox::currentIndexChanged, this, [this, canvas, family](int) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].fontFamily = family->currentData().toString(); refresh();
    });
    connect(size, &QSpinBox::valueChanged, this, [this, canvas](int value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].fontSize = value; refresh();
    });
    connect(bold, &QCheckBox::toggled, this, [this, canvas](bool value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].bold = value; refresh();
    });
    connect(writing, &QComboBox::currentIndexChanged, this, [this, canvas, rotation](int value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        auto &layer = m_document.textLayers[i]; layer.vertical = value == 1;
        layer.rotation = 0;
        const QSignalBlocker blocker(rotation); rotation->setCurrentIndex(0); refresh();
    });
    connect(rotation, &QComboBox::currentIndexChanged, this, [this, canvas](int value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].rotation = value * 90; refresh();
    });
    connect(color, &QPushButton::clicked, this, [this, canvas] {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        const auto chosen = QColorDialog::getColor(m_document.textLayers[i].color, this, QStringLiteral("文字颜色"));
        if (chosen.isValid()) { m_document.textLayers[i].color = chosen; refresh(); }
    });
    connect(this, &CoverEditor::documentChanged, this, reloadTextList);
    syncText();
    auto *help = new QLabel(QStringLiteral("点击文字并拖动可移动位置。\n图片可拖动，滚轮或角点缩放。\n点击裁剪按钮框选保留区域。\n选框不会导出；字号为成品像素。"));
    help->setWordWrap(true); form->addRow(help);
    auto *exportButton = new QPushButton(QStringLiteral("导出 PNG…")); form->addRow(exportButton);
    auto *apply = new QPushButton(QStringLiteral("应用到当前条目")); form->addRow(apply);
    if (panelMode) {
        heading->setText(QStringLiteral("当前区域编辑"));
        form->setRowVisible(exportButton, false); form->setRowVisible(apply, false);
    }
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFixedWidth(330);
    scroll->setWidget(panel); root->addWidget(scroll);
    connect(ratio, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const QSize sizes[] = {{1200,1800},{1200,1600},{1920,1080}};
        if (i >= 0 && i < 3) m_document.outputSize = sizes[i];
        refresh();
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出封面"), QStringLiteral("cover.png"), QStringLiteral("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
        QString error;
        if (!saveCover(m_document, path, &error)) QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        const QImage image = renderCover(m_document);
        if (!image.isNull()) emit applyRequested(image);
    });
    connect(this, &CoverEditor::documentChanged, this, [this, ratio, layout] {
        const QSignalBlocker a(ratio), b(layout);
        ratio->setCurrentIndex(m_document.outputSize == QSize(1200,1800) ? 0 : m_document.outputSize == QSize(1200,1600) ? 1 : m_document.outputSize == QSize(1920,1080) ? 2 : -1);
        layout->setCurrentIndex(0);

    });
}
void CoverEditor::setDocument(const CoverDocument &document)
{
    if (document.outputSize.isEmpty() || document.outputSize.width() > 8192 || document.outputSize.height() > 8192) return;
    m_document = document; enableTextLayers(m_document); enableImageLayers(m_document);
    auto *canvas = static_cast<Canvas *>(m_canvas); canvas->selected = m_document.imageLayers.isEmpty() ? -1 : 0; canvas->selectedText = m_document.textLayers.isEmpty() ? -1 : 0;
    if(canvas->selectedText>=0) canvas->selected=-1;
    emit documentChanged(); refresh();
}
void CoverEditor::refresh() { m_canvas->update(); emit compositionChanged(); }
}
