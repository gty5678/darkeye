#include "cover_maker/CoverEditor.h"
#include "cover_maker/UpscaleClient.h"
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainterPath>
#include <QKeyEvent>
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
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QIcon>
#include <QToolButton>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUndoStack>
#include <QUndoCommand>
#include <QDateTime>
#include <QAction>
#include <QTreeWidget>
#include <QScrollBar>
#include <QGridLayout>
#include <limits>
#include <algorithm>
#include <cmath>
#include <functional>

namespace darkeye::cover_maker {
namespace {
class DocumentCommand final : public QUndoCommand {
public:
    CoverDocument before, after;
    std::function<void(const CoverDocument &,int,int)> restore;
    int beforeImage,beforeText,afterImage,afterText;
    quintptr key;
    qint64 timestamp=QDateTime::currentMSecsSinceEpoch();
    bool first=true;
    DocumentCommand(CoverDocument a,CoverDocument b,std::function<void(const CoverDocument &,int,int)> callback,quintptr mergeKey,int bi,int bt,int ai,int at)
        : before(std::move(a)),after(std::move(b)),restore(std::move(callback)),beforeImage(bi),beforeText(bt),afterImage(ai),afterText(at),key(mergeKey) {setText(QStringLiteral("编辑封套"));}
    void undo() override {restore(before,beforeImage,beforeText);}
    void redo() override {if(first)first=false;else restore(after,afterImage,afterText);}
    int id() const override {return key?1:-1;}
    bool mergeWith(const QUndoCommand *command) override {
        const auto *other=dynamic_cast<const DocumentCommand *>(command);
        if(!other || key!=other->key || other->timestamp-timestamp>700)return false;
        after=other->after;afterImage=other->afterImage;afterText=other->afterText;timestamp=other->timestamp;return true;
    }
};
class Canvas final : public QWidget {
public:
    CoverDocument &doc;
    QVector<qreal> guides;
    bool guidesVisible = true;
    int selected = -1;
    int selectedText = -1;
    std::function<void(int)> textSelected;
    std::function<void(int)> selectionChanged;
    std::function<void()> edited;
    std::function<void()> selectionUiChanged;
    std::function<void()> cropStateChanged;
    QPlainTextEdit *inlineText=nullptr;
    std::function<void(const QString &)> inlineEdited;
    std::function<void(const QStringList &,QPointF)> imagesDropped;
    void endTextEditing() {if(inlineText)inlineText->hide();}
    void startTextEditing() {
        if(selectedText<0 || !editableSelection())return;
        const auto box=textLayerBounds(doc.textLayers[selectedText],doc.outputSize);
        const auto a=area();const QPointF center(a.x()+box.center().x()*a.width()/doc.outputSize.width(),a.y()+box.center().y()*a.height()/doc.outputSize.height());
        const int w=std::min(width()-16,std::max(220,qRound(box.width()*a.width()/doc.outputSize.width()+32)));
        const int h=std::min(height()-16,std::max(80,qRound(box.height()*a.height()/doc.outputSize.height()+24)));
        inlineText->setGeometry(std::clamp(qRound(center.x()-w/2),8,std::max(8,width()-w-8)),std::clamp(qRound(center.y()-h/2),8,std::max(8,height()-h-8)),w,h);
        {const QSignalBlocker blocker(inlineText);inlineText->setPlainText(doc.textLayers[selectedText].text);}
        inlineText->show();inlineText->raise();inlineText->selectAll();inlineText->setFocus();
    }
    bool eventFilter(QObject *object,QEvent *event) override {
        if(object==inlineText && event->type()==QEvent::KeyPress) {
            const auto *key=static_cast<QKeyEvent *>(event);
            if(key->key()==Qt::Key_Escape || ((key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter) && !(key->modifiers()&Qt::ShiftModifier))) {
                endTextEditing();setFocus();return true;
            }
        }
        if(object==inlineText && event->type()==QEvent::FocusOut)endTextEditing();
        return QWidget::eventFilter(object,event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if(event->button()!=Qt::LeftButton)return;
        if(cropping)return;mousePressEvent(event);dragging=false;startTextEditing();
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(!cropping && event->mimeData()->hasUrls())for(const auto &url:event->mimeData()->urls())if(url.isLocalFile()){event->acceptProposedAction();return;}
        event->ignore();
    }
    void dropEvent(QDropEvent *event) override {
        if(cropping || !area().contains(event->position())){event->ignore();return;}
        QStringList paths;for(const auto &url:event->mimeData()->urls())if(url.isLocalFile())paths.append(url.toLocalFile());
        if(paths.isEmpty())return;
        const auto a=area();if(imagesDropped)imagesDropped(paths,{(event->position().x()-a.x())/a.width(),(event->position().y()-a.y())/a.height()});
        event->acceptProposedAction();
    }
    bool snapping = true;
    qreal snapX = -1, snapY = -1;
    bool inInteraction() const {return dragging || cropping;}
    bool editableSelection() const {
        if(selectedText>=0 && selectedText<doc.textLayers.size())return !doc.textLayers[selectedText].locked;
        return selected>=0 && selected<doc.imageLayers.size() && !doc.imageLayers[selected].locked;
    }
    QRectF bounds() const {
        if(selectedText>=0 && selectedText<doc.textLayers.size())return textLayerBounds(doc.textLayers[selectedText],doc.outputSize);
        if(selected>=0 && selected<doc.imageLayers.size())return imageLayerRect(doc.imageLayers[selected],doc.outputSize);
        return {};
    }
    void translateSelection(const QPointF &delta) {
        if(selectedText>=0)doc.textLayers[selectedText].center+=QPointF(delta.x()/doc.outputSize.width(),delta.y()/doc.outputSize.height());
        else if(selected>=0)doc.imageLayers[selected].frame.translate(delta.x()/doc.outputSize.width(),delta.y()/doc.outputSize.height());
    }
    QPointF snapCorrection() {
        snapX=snapY=-1;
        const auto box=bounds();if(box.isEmpty())return {};
        QVector<qreal> xs{0,doc.outputSize.width()/2.,qreal(doc.outputSize.width())};
        QVector<qreal> ys{0,doc.outputSize.height()/2.,qreal(doc.outputSize.height())};
        if(guides.size()==2) {
            xs.append(guides[0]*doc.outputSize.width());xs.append(guides[1]*doc.outputSize.width());
            xs.append(guides[0]*doc.outputSize.width()/2);xs.append((guides[0]+guides[1])*doc.outputSize.width()/2);xs.append((guides[1]+1)*doc.outputSize.width()/2);
        }
        for(auto ref:orderedLayers(doc)) {
            if((ref.text && ref.index==selectedText)||(!ref.text && ref.index==selected))continue;
            if(ref.text?!doc.textLayers[ref.index].visible:!doc.imageLayers[ref.index].visible)continue;
            const auto other=ref.text?textLayerBounds(doc.textLayers[ref.index],doc.outputSize):imageLayerRect(doc.imageLayers[ref.index],doc.outputSize);
            xs<<other.left()<<other.center().x()<<other.right();ys<<other.top()<<other.center().y()<<other.bottom();
        }
        const qreal tolerance=6*doc.outputSize.width()/area().width();qreal dx=tolerance+1,dy=tolerance+1;
        for(qreal x:xs)for(qreal edge:{box.left(),box.center().x(),box.right()})if(std::abs(x-edge)<std::abs(dx)){dx=x-edge;snapX=x;}
        for(qreal y:ys)for(qreal edge:{box.top(),box.center().y(),box.bottom()})if(std::abs(y-edge)<std::abs(dy)){dy=y-edge;snapY=y;}
        if(std::abs(dx)>tolerance){dx=0;snapX=-1;}if(std::abs(dy)>tolerance){dy=0;snapY=-1;}
        return {dx,dy};
    }
    void alignSelection(int mode,int target) {
        if(!editableSelection() || cropping)return;
        QRectF region(QPointF(),QSizeF(doc.outputSize));
        if(target>0 && guides.size()==2) {
            const qreal edges[]={0,guides[0],guides[1],1};
            region={edges[target-1]*doc.outputSize.width(),0,(edges[target]-edges[target-1])*doc.outputSize.width(),qreal(doc.outputSize.height())};
        }
        const auto box=bounds();QPointF delta;
        if(mode==0)delta.setX(region.left()-box.left());if(mode==1)delta.setX(region.center().x()-box.center().x());if(mode==2)delta.setX(region.right()-box.right());
        if(mode==3)delta.setY(region.top()-box.top());if(mode==4)delta.setY(region.center().y()-box.center().y());if(mode==5)delta.setY(region.bottom()-box.bottom());
        translateSelection(delta);update();if(edited)edited();
    }
    bool cropping = false;
    ImageLayer cropOriginal;
    QRectF fullImageFrame;
    int cropHandle = 0;
    QRectF cropDragFrame;
    QPointF cropDragStart;
    void beginCrop() {
        if(!editableSelection() || selected<0 || selected>=doc.imageLayers.size() || doc.imageLayers[selected].image.isNull()) return;
        cropOriginal=doc.imageLayers[selected];
        const auto c=cropOriginal.crop; const auto f=cropOriginal.frame;
        if(c.isEmpty() || f.isEmpty())return;
        fullImageFrame={f.x()-c.x()*f.width()/c.width(),f.y()-c.y()*f.height()/c.height(),f.width()/c.width(),f.height()/c.height()};
        cropping=true;dragging=false;setFocus();
        if(cropStateChanged)cropStateChanged();update();
    }
    void finishCrop(bool accept) {
        if(!cropping)return;
        if(!accept && selected>=0 && selected<doc.imageLayers.size())doc.imageLayers[selected]=cropOriginal;
        cropping=false;dragging=false;cropHandle=0;
        if(cropStateChanged)cropStateChanged();if(edited)edited();update();
    }
    QRectF displayRect(const QRectF &normalized) const {
        const auto a=area();return {a.x()+normalized.x()*a.width(),a.y()+normalized.y()*a.height(),normalized.width()*a.width(),normalized.height()*a.height()};
    }
    void keyPressEvent(QKeyEvent *event) override {
        if(cropping && (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Escape)) {
            finishCrop(event->key()!=Qt::Key_Escape);event->accept();return;
        }
        QWidget::keyPressEvent(event);
    }
    explicit Canvas(CoverDocument &d, QWidget *parent) : QWidget(parent), doc(d) {
        setObjectName(QStringLiteral("coverCanvas"));setAcceptDrops(true);
        inlineText=new QPlainTextEdit(this);inlineText->setObjectName(QStringLiteral("inlineTextEditor"));
        inlineText->setUndoRedoEnabled(false);inlineText->setStyleSheet(QStringLiteral("QPlainTextEdit { background: #ffffff; color: #20242b; border: 2px solid #5b9cf6; border-radius: 4px; padding: 4px; }"));
        inlineText->setToolTip(QStringLiteral("直接输入文字，Enter 完成，Shift+Enter 换行"));inlineText->hide();inlineText->installEventFilter(this);
        connect(inlineText,&QPlainTextEdit::textChanged,this,[this]{if(inlineEdited)inlineEdited(inlineText->toPlainText());});
        setMinimumSize(300, 320); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::OpenHandCursor);
    }
    QRectF area() const {
        QSizeF size(doc.outputSize);
        size.scale(QSizeF(std::max(1, width() - 48), std::max(1, height() - 48)), Qt::KeepAspectRatio);
        size *= viewZoom;
        return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2) + viewOffset, size};
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), QColor("#141b25"));
        const QRectF a = area(); paintCover(p, a, doc);
        if (guidesVisible && guides.size() == 2) {
            p.save(); p.setPen(QPen(QColor("#a4d8ff"),1,Qt::DashLine));
            for (qreal position : guides) {
                const qreal x=a.left()+position*a.width();
                p.drawLine(QPointF(x,a.top()),QPointF(x,a.bottom()));
            }
            const qreal edges[]={0,guides[0],guides[1],1};
            const QStringList labels{QStringLiteral("封底"),QStringLiteral("书脊"),QStringLiteral("封面")};
            p.setPen(QColor("#b9d2e8"));
            for(int i=0;i<3;++i) p.drawText(QRectF(a.left()+edges[i]*a.width()-8,a.top()-23,
                (edges[i+1]-edges[i])*a.width()+16,20),Qt::AlignCenter,labels[i]);
            p.restore();
        }
        p.save();p.setClipRect(a);p.setPen(QPen(QColor("#ff79ba"),1));
        if(snapX>=0)p.drawLine(QPointF(a.x()+snapX*a.width()/doc.outputSize.width(),a.top()),QPointF(a.x()+snapX*a.width()/doc.outputSize.width(),a.bottom()));
        if(snapY>=0)p.drawLine(QPointF(a.left(),a.y()+snapY*a.height()/doc.outputSize.height()),QPointF(a.right(),a.y()+snapY*a.height()/doc.outputSize.height()));
        p.restore();
        if (selectedText >= 0 && selectedText < doc.textLayers.size()) {
            p.save(); p.translate(a.topLeft());
            p.scale(a.width() / doc.outputSize.width(), a.height() / doc.outputSize.height());
            const auto &layer = doc.textLayers[selectedText];
            p.setPen(QPen(QColor("#ffcf57"), 2 * doc.outputSize.width() / a.width(), Qt::DashLine));
            p.drawRect(textLayerBounds(layer,doc.outputSize)); p.restore(); return;
        }
        if (selected < 0 || selected >= doc.imageLayers.size()) return;
        const QRectF frame = imageLayerRect(doc.imageLayers[selected],doc.outputSize);
        const QRectF display(a.topLeft() + QPointF(frame.x()*a.width()/doc.outputSize.width(),frame.y()*a.height()/doc.outputSize.height()),
                             QSizeF(frame.width()*a.width()/doc.outputSize.width(),frame.height()*a.height()/doc.outputSize.height()));
        if(cropping) {
            p.save();p.setClipRect(a);
            QPainterPath outside;outside.setFillRule(Qt::OddEvenFill);outside.addRect(displayRect(fullImageFrame));outside.addRect(display);
            p.setClipPath(outside,Qt::IntersectClip);p.setOpacity(.3);
            p.drawImage(displayRect(fullImageFrame),doc.imageLayers[selected].image);p.restore();
            p.setPen(QPen(QColor("#ffcf57"),2));p.setBrush(Qt::NoBrush);p.drawRect(display);
            p.setBrush(QColor("#ffcf57"));
            for(const auto &point : {display.topLeft(),display.topRight(),display.bottomLeft(),display.bottomRight(),
                QPointF(display.center().x(),display.top()),QPointF(display.center().x(),display.bottom()),
                QPointF(display.left(),display.center().y()),QPointF(display.right(),display.center().y())})
                p.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
            return;
        }
        p.setPen(QPen(QColor("#57bbff"),2,Qt::DashLine)); p.drawRect(display);
        p.setBrush(QColor("#57bbff"));
        for (const auto &point : {display.topLeft(),display.topRight(),display.bottomLeft(),display.bottomRight()})
            p.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
    }
    QPointF documentPoint(const QPointF &point) const {
        const auto a=area(); return {(point.x()-a.x())*doc.outputSize.width()/a.width(),(point.y()-a.y())*doc.outputSize.height()/a.height()};
    }
    void updateHoverCursor(const QPointF &point) {
        if(editableSelection() && selectedText<0 && selected>=0 && selected<doc.imageLayers.size()) {
            const auto frame=displayRect(doc.imageLayers[selected].frame);
            const QPointF corners[]={frame.topLeft(),frame.topRight(),frame.bottomLeft(),frame.bottomRight()};
            for(int i=0;i<4;++i) {
                const auto delta=point-corners[i];
                if(std::abs(delta.x())<=8 && std::abs(delta.y())<=8) {
                    setCursor(i==0 || i==3?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor);return;
                }
            }
        }
        setCursor(Qt::OpenHandCursor);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if(event->button()==Qt::MiddleButton && !dragging) {
            endTextEditing();panning=true;panLast=event->position();
            setCursor(Qt::ClosedHandCursor);event->accept();return;
        }
        if(panning)return;
        if (event->button()!=Qt::LeftButton) return;
        endTextEditing();
        const auto pos=documentPoint(event->position()); resizing=false;
        if(cropping) {
            const auto f=displayRect(doc.imageLayers[selected].frame);const auto point=event->position();
            cropHandle=0;
            if(point.y()>=f.top()-8 && point.y()<=f.bottom()+8) {
                if(std::abs(point.x()-f.left())<=8)cropHandle|=1;
                if(std::abs(point.x()-f.right())<=8)cropHandle|=2;
            }
            if(point.x()>=f.left()-8 && point.x()<=f.right()+8) {
                if(std::abs(point.y()-f.top())<=8)cropHandle|=4;
                if(std::abs(point.y()-f.bottom())<=8)cropHandle|=8;
            }
            if(!cropHandle && f.contains(point))cropHandle=16;
            cropDragFrame=doc.imageLayers[selected].frame;
            cropDragStart={(point.x()-area().x())/area().width(),(point.y()-area().y())/area().height()};
            dragging=cropHandle!=0;return;
        }
        // Handles take precedence even when an image edge is just outside the panel.
        if (editableSelection() && selectedText < 0 && selected>=0 && selected<doc.imageLayers.size()) {
            const auto frame=imageLayerRect(doc.imageLayers[selected],doc.outputSize);
            const QPointF corners[]={frame.topLeft(),frame.topRight(),frame.bottomLeft(),frame.bottomRight()};
            for (int i=0;i<4;++i) {
                const auto delta=pos-corners[i]; const qreal radius=8*doc.outputSize.width()/area().width();
                if (std::abs(delta.x())<=radius && std::abs(delta.y())<=radius) {
                    anchor=corners[3-i]; originalCorner=corners[i];
                    setCursor(i==0 || i==3?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor);
                    resizing=true; dragging=true; return;
                }
            }
        }
        if (!area().contains(event->position())) return;
        selectedText=-1;selected=-1;
        auto order=orderedLayers(doc);
        for(auto it=order.crbegin();it!=order.crend();++it) {
            if(it->text) {
                const auto &layer=doc.textLayers[it->index];
                if(layer.visible && !layer.locked && !layer.text.isEmpty() && textLayerRect(layer).contains(textLayerTransform(layer,doc.outputSize).inverted().map(pos))) {selectedText=it->index;break;}
            } else {const auto &layer=doc.imageLayers[it->index];if(layer.visible && !layer.locked && imageLayerRect(layer,doc.outputSize).contains(pos)){selected=it->index;break;}}
        }
        if(textSelected)textSelected(selectedText);
        if(selectionChanged) selectionChanged(selected);
        last=event->position(); dragging=selectedText>=0 || selected>=0; setCursor(Qt::ClosedHandCursor); update();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(panning) {
            viewOffset+=event->position()-panLast;panLast=event->position();
            update();event->accept();return;
        }
        if (!dragging) {updateHoverCursor(event->position());return;}
        if(cropping) {
            const auto a=area();const QPointF point((event->position().x()-a.x())/a.width(),(event->position().y()-a.y())/a.height());
            const auto delta=point-cropDragStart;auto frame=cropDragFrame;
            const qreal minWidth=std::min(cropDragFrame.width(),fullImageFrame.width()/doc.imageLayers[selected].image.width());
            const qreal minHeight=std::min(cropDragFrame.height(),fullImageFrame.height()/doc.imageLayers[selected].image.height());
            if(cropHandle==16) {
                frame.moveLeft(std::clamp(frame.left()+delta.x(),fullImageFrame.left(),fullImageFrame.right()-frame.width()));
                frame.moveTop(std::clamp(frame.top()+delta.y(),fullImageFrame.top(),fullImageFrame.bottom()-frame.height()));
            } else {
                if(cropHandle&1)frame.setLeft(std::clamp(frame.left()+delta.x(),fullImageFrame.left(),frame.right()-minWidth));
                if(cropHandle&2)frame.setRight(std::clamp(frame.right()+delta.x(),frame.left()+minWidth,fullImageFrame.right()));
                if(cropHandle&4)frame.setTop(std::clamp(frame.top()+delta.y(),fullImageFrame.top(),frame.bottom()-minHeight));
                if(cropHandle&8)frame.setBottom(std::clamp(frame.bottom()+delta.y(),frame.top()+minHeight,fullImageFrame.bottom()));
            }
            auto &layer=doc.imageLayers[selected];layer.frame=frame;
            layer.crop={(frame.x()-fullImageFrame.x())/fullImageFrame.width(),(frame.y()-fullImageFrame.y())/fullImageFrame.height(),
                        frame.width()/fullImageFrame.width(),frame.height()/fullImageFrame.height()};
            update();if(edited)edited();return;
        }
        if(!editableSelection())return;
        if (selectedText >= 0 && selectedText < doc.textLayers.size()) {
            auto &layer = doc.textLayers[selectedText];
            const QPointF delta = event->position() - last;
            layer.center.setX(std::clamp(layer.center.x() + delta.x() / area().width(), 0., 1.));
            layer.center.setY(std::clamp(layer.center.y() + delta.y() / area().height(), 0., 1.));
            if(snapping && !(event->modifiers()&Qt::AltModifier))translateSelection(snapCorrection());else snapX=snapY=-1;
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
            if(snapping && !(event->modifiers()&Qt::AltModifier))translateSelection(snapCorrection());else snapX=snapY=-1;
        }
        last=event->position(); update(); if(edited) edited();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(event->button()==Qt::MiddleButton && panning) {
            panning=false;updateHoverCursor(event->position());event->accept();return;
        }
        if(event->button()!=Qt::LeftButton || panning)return;
        dragging=false;resizing=false;snapX=snapY=-1;
        updateHoverCursor(event->position());update();if(edited)edited();
    }
    void scaleImage(qreal factor) {
        if(!editableSelection() || cropping || selected<0 || selected>=doc.imageLayers.size()) return;
        auto &frame=doc.imageLayers[selected].frame;
        const qreal width=frame.width()*factor, height=frame.height()*factor;
        if(width<.001 || height<.001 || width>20 || height>20) return;
        const auto center=frame.center(); frame.setSize({width,height}); frame.moveCenter(center);
        update(); if(edited) edited();
    }
    void wheelEvent(QWheelEvent *event) override {
        if(dragging || panning) {event->accept();return;}
        const qreal delta=event->pixelDelta().isNull()?event->angleDelta().y():event->pixelDelta().y();
        const qreal nextZoom=std::clamp(viewZoom*std::pow(1.0015,delta),.1,10.);
        const auto previous=area();
        const QPointF point=event->position();
        const QPointF relative((point.x()-previous.x())/previous.width(),(point.y()-previous.y())/previous.height());
        viewZoom=nextZoom;
        const auto next=area();
        viewOffset+=point-QPointF(next.x()+relative.x()*next.width(),next.y()+relative.y()*next.height());
        endTextEditing();update();event->accept();
    }

private:
    qreal viewZoom = 1;
    QPointF viewOffset;
    bool panning = false;
    QPointF panLast;
    bool resizing = false;
    QPointF anchor, originalCorner;
    bool dragging = false;
    QPointF last;
};
}
CoverEditor::CoverEditor(QWidget *parent, bool panelMode) : QWidget(parent)
{
    m_upscale=new UpscaleClient(this);
    m_srUrl=qEnvironmentVariable("DARKEYE_SR_URL",QSettings().value(QStringLiteral("coverMaker/srUrl"),QStringLiteral("http://127.0.0.1:8443/v1/upscale")).toString());
    m_srToken=qEnvironmentVariable("DARKEYE_SR_TOKEN");
    m_history=new QUndoStack(this);m_history->setUndoLimit(100);
    auto *root = new QHBoxLayout(this);
    auto *canvas = new Canvas(m_document, this); m_canvas = canvas;
    auto *canvasColumn = new QVBoxLayout;
    auto *tools = new QHBoxLayout; tools->setContentsMargins(0,0,0,0);
    auto *undo = new QPushButton(QStringLiteral("撤销"));undo->setObjectName(QStringLiteral("undoCover"));
    auto *redo = new QPushButton(QStringLiteral("重做"));redo->setObjectName(QStringLiteral("redoCover"));
    tools->addWidget(undo);tools->addWidget(redo);undo->setEnabled(false);redo->setEnabled(false);
    connect(undo,&QPushButton::clicked,this,[this,canvas]{if(canvas->cropping)canvas->finishCrop(false);else m_history->undo();});
    connect(redo,&QPushButton::clicked,this,[this,canvas]{if(canvas->cropping)canvas->finishCrop(false);m_history->redo();});
    connect(m_history,&QUndoStack::canUndoChanged,undo,&QPushButton::setEnabled);connect(m_history,&QUndoStack::canRedoChanged,redo,&QPushButton::setEnabled);
    auto *undoAction=new QAction(QStringLiteral("撤销"),this);connect(undoAction,&QAction::triggered,undo,&QPushButton::click);undoAction->setShortcut(QKeySequence::Undo);undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(undoAction);
    auto *redoAction=new QAction(QStringLiteral("重做"),this);connect(redoAction,&QAction::triggered,redo,&QPushButton::click);redoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(redoAction);
    canvasColumn->addLayout(tools); canvasColumn->addWidget(canvas,1); root->addLayout(canvasColumn,1);
    const auto addTool = [tools, this](QPushButton *button, bool text) {
        QPixmap pixmap(28,28); pixmap.fill(Qt::transparent); QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(palette().color(QPalette::ButtonText),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(text) {p.drawLine(5,6,19,6);p.drawLine(12,6,12,23);p.drawLine(8,23,16,23);}
        else {p.drawRoundedRect(QRectF(3,5,19,18),2,2);p.drawEllipse(QRectF(6,8,4,4));p.drawLine(4,21,11,14);p.drawLine(11,14,17,20);p.drawLine(17,20,21,16);}
        p.fillRect(QRectF(18,0,10,10),palette().brush(QPalette::Window));p.drawLine(23,1,23,9);p.drawLine(19,5,27,5);p.end();
        const QString label=text?QStringLiteral("添加文字"):QStringLiteral("添加图片");
        button->setText({});button->setIcon(QIcon(pixmap));button->setIconSize({28,28});button->setFixedSize(44,40);
        button->setToolTip(label);button->setAccessibleName(label);tools->insertWidget(text?1:0,button);
    };
    canvas->edited = [this] { refresh(); };
    auto *panel = new QWidget(this); panel->setMinimumWidth(280);
    auto *form = new QFormLayout(panel);
    auto *heading = new QLabel(QStringLiteral("封面制作器"));
    QFont font = heading->font(); font.setPointSize(18); font.setBold(true); heading->setFont(font);
    form->addRow(heading);
    auto *advanced = new QWidget;advanced->setObjectName(QStringLiteral("advancedOptions"));
    auto *advancedForm=new QFormLayout(advanced);advancedForm->setContentsMargins(0,8,0,0);
    auto *more=new QToolButton;more->setObjectName(QStringLiteral("moreOptions"));more->setText(QStringLiteral("更多"));more->setCheckable(true);more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);more->setArrowType(Qt::RightArrow);
    connect(more,&QToolButton::toggled,this,[advanced,more](bool shown){advanced->setVisible(shown);more->setArrowType(shown?Qt::DownArrow:Qt::RightArrow);});
    advanced->hide();
    auto *layers = new QTreeWidget;layers->setObjectName(QStringLiteral("allLayers"));
    layers->setHeaderLabels({QStringLiteral("图层（从上到下）"),QStringLiteral("显示"),QStringLiteral("锁定")});
    layers->setRootIsDecorated(false);layers->setMaximumHeight(180);layers->setMinimumHeight(100);
    layers->setColumnWidth(0,170);layers->setColumnWidth(1,42);layers->setColumnWidth(2,42);advancedForm->addRow(layers);
    auto *layerActions=new QWidget;auto *layerRow=new QHBoxLayout(layerActions);layerRow->setContentsMargins(0,0,0,0);layerActions->setFixedHeight(30);
    auto *layerUp=new QPushButton(QStringLiteral("↑"));layerUp->setObjectName(QStringLiteral("layerUp"));
    auto *layerDown=new QPushButton(QStringLiteral("↓"));layerDown->setObjectName(QStringLiteral("layerDown"));
    auto *layerCopy=new QPushButton(QStringLiteral("复制"));layerCopy->setObjectName(QStringLiteral("duplicateLayer"));
    auto *layerDelete=new QPushButton(QStringLiteral("删除"));layerDelete->setObjectName(QStringLiteral("deleteLayer"));
    for(auto *button:{layerUp,layerDown,layerCopy,layerDelete})layerRow->addWidget(button);advancedForm->addRow(layerActions);
    const auto syncLayers=[this,canvas,layers,layerUp,layerDown,layerCopy,layerDelete] {
        if(layers->property("selecting").toBool())return;
        const QSignalBlocker blocker(layers);const int scroll=layers->verticalScrollBar()->value();layers->clear();
        auto order=orderedLayers(m_document);
        for(auto it=order.crbegin();it!=order.crend();++it) {
            const auto ref=*it;const bool visible=ref.text?m_document.textLayers[ref.index].visible:m_document.imageLayers[ref.index].visible;
            const bool locked=ref.text?m_document.textLayers[ref.index].locked:m_document.imageLayers[ref.index].locked;
            const QString name=ref.text?QStringLiteral("文 · ")+m_document.textLayers[ref.index].text.left(20).replace(u'\n',u' '):QStringLiteral("图 · ")+m_document.imageLayers[ref.index].name;
            auto *item=new QTreeWidgetItem(layers,{name});item->setData(0,Qt::UserRole,ref.text);item->setData(0,Qt::UserRole+1,ref.index);
            item->setCheckState(1,visible?Qt::Checked:Qt::Unchecked);item->setCheckState(2,locked?Qt::Checked:Qt::Unchecked);
            if((ref.text && ref.index==canvas->selectedText)||(!ref.text && ref.index==canvas->selected))layers->setCurrentItem(item);
        }
        layers->verticalScrollBar()->setValue(scroll);
        const bool valid=canvas->selected>=0 || canvas->selectedText>=0;
        const bool editable=valid && canvas->editableSelection() && !canvas->cropping;
        layerUp->setEnabled(editable);layerDown->setEnabled(editable);layerDelete->setEnabled(editable);layerCopy->setEnabled(valid && !canvas->cropping);
        layers->setEnabled(!canvas->cropping);
    };
    connect(layers,&QTreeWidget::currentItemChanged,this,[canvas,layers](QTreeWidgetItem *item,QTreeWidgetItem *) {
        if(!item)return;layers->setProperty("selecting",true);
        const bool text=item->data(0,Qt::UserRole).toBool();const int index=item->data(0,Qt::UserRole+1).toInt();
        canvas->selectedText=text?index:-1;canvas->selected=text?-1:index;
        if(canvas->textSelected)canvas->textSelected(canvas->selectedText);
        if(canvas->selectionChanged)canvas->selectionChanged(canvas->selected);
        layers->setProperty("selecting",false);if(canvas->selectionUiChanged)canvas->selectionUiChanged();canvas->update();
    });
    connect(layers,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column){
        if(column!=1 && column!=2)return;const bool text=item->data(0,Qt::UserRole).toBool();const int i=item->data(0,Qt::UserRole+1).toInt();
        const bool value=item->checkState(column)==Qt::Checked;
        if(text){if(column==1)m_document.textLayers[i].visible=value;else m_document.textLayers[i].locked=value;}
        else {if(column==1)m_document.imageLayers[i].visible=value;else m_document.imageLayers[i].locked=value;}
        refresh();emit documentChanged();
    });
    const auto moveLayer=[this,canvas](int delta){
        if(!canvas->editableSelection() || canvas->cropping)return;
        auto order=orderedLayers(m_document);const LayerRef selected{canvas->selectedText>=0,canvas->selectedText>=0?canvas->selectedText:canvas->selected};
        const int from=order.indexOf(selected),to=from+delta;if(from<0 || to<0 || to>=order.size())return;
        order.swapItemsAt(from,to);int z=0;for(auto ref:order){if(ref.text)m_document.textLayers[ref.index].z=z++;else m_document.imageLayers[ref.index].z=z++;}refresh();
    };
    connect(layerUp,&QPushButton::clicked,this,[moveLayer]{moveLayer(1);});connect(layerDown,&QPushButton::clicked,this,[moveLayer]{moveLayer(-1);});
    connect(layerCopy,&QPushButton::clicked,this,[this,canvas]{
        if(canvas->cropping)return;const int z=nextLayerZ(m_document);
        if(canvas->selectedText>=0){auto layer=m_document.textLayers[canvas->selectedText];layer.center+=QPointF(.02,.02);layer.z=z;layer.locked=false;m_document.textLayers.append(layer);canvas->selectedText=m_document.textLayers.size()-1;}
        else if(canvas->selected>=0){auto layer=m_document.imageLayers[canvas->selected];layer.frame.translate(.02,.02);layer.z=z;layer.locked=false;m_document.imageLayers.append(layer);canvas->selected=m_document.imageLayers.size()-1;}
        else return;emit documentChanged();refresh();
    });
    connect(layerDelete,&QPushButton::clicked,this,[this,canvas]{
        if(!canvas->editableSelection() || canvas->cropping)return;
        if(canvas->selectedText>=0)m_document.textLayers.removeAt(canvas->selectedText);else m_document.imageLayers.removeAt(canvas->selected);
        canvas->selected=canvas->selectedText=-1;emit documentChanged();refresh();
    });
    auto *snap = new QCheckBox(QStringLiteral("吸附"));snap->setObjectName(QStringLiteral("snapLayers"));snap->setChecked(true);snap->setToolTip(QStringLiteral("拖动时自动吸附，按住 Alt 可暂时关闭"));tools->addWidget(snap);
    connect(snap,&QCheckBox::toggled,this,[canvas](bool enabled){canvas->snapping=enabled;canvas->snapX=canvas->snapY=-1;canvas->update();});
    auto *alignTarget=new QComboBox;alignTarget->setObjectName(QStringLiteral("alignTarget"));alignTarget->addItems({QStringLiteral("整张画布"),QStringLiteral("封底范围"),QStringLiteral("书脊范围"),QStringLiteral("封面范围")});advancedForm->addRow(QStringLiteral("对齐到"),alignTarget);
    auto *alignment=new QWidget;auto *alignGrid=new QGridLayout(alignment);alignGrid->setContentsMargins(0,0,0,0);
    const QStringList alignNames{QStringLiteral("左对齐"),QStringLiteral("水平居中"),QStringLiteral("右对齐"),QStringLiteral("顶对齐"),QStringLiteral("垂直居中"),QStringLiteral("底对齐")};
    for(int mode=0;mode<6;++mode){auto *button=new QPushButton(alignNames[mode]);button->setObjectName(QStringLiteral("align%1").arg(mode));alignGrid->addWidget(button,mode/3,mode%3);connect(button,&QPushButton::clicked,this,[canvas,alignTarget,mode]{canvas->alignSelection(mode,alignTarget->currentIndex());});}advancedForm->addRow(alignment);

    auto *properties = new QStackedWidget; properties->setObjectName(QStringLiteral("selectionProperties"));
    auto *emptyPage = new QWidget; emptyPage->setObjectName(QStringLiteral("emptyProperties"));
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    auto *emptyHint = new QLabel(QStringLiteral("拖入图片，或点击左上角添加文字。\n点击画布中的对象即可编辑。"));emptyHint->setWordWrap(true);
    emptyLayout->addWidget(emptyHint);emptyLayout->addStretch();
    auto *imagePage = new QWidget; imagePage->setObjectName(QStringLiteral("imageProperties"));
    auto *imagePageLayout = new QVBoxLayout(imagePage);imagePageLayout->setContentsMargins(0,0,0,0);
    auto *imageForm = new QFormLayout;imagePageLayout->addLayout(imageForm);imagePageLayout->addStretch();imageForm->setContentsMargins(0,8,0,8);imageForm->setAlignment(Qt::AlignTop);
    auto *textPage = new QWidget; textPage->setObjectName(QStringLiteral("textProperties"));
    auto *textPageLayout = new QVBoxLayout(textPage);textPageLayout->setContentsMargins(0,0,0,0);
    auto *textForm = new QFormLayout;textPageLayout->addLayout(textForm);textPageLayout->addStretch();textForm->setContentsMargins(0,8,0,8);textForm->setAlignment(Qt::AlignTop);
    properties->addWidget(emptyPage);properties->addWidget(imagePage);properties->addWidget(textPage);
    auto *ratio = new QComboBox;
    ratio->addItems({QStringLiteral("2:3 · 1200 × 1800"), QStringLiteral("3:4 · 1200 × 1600"), QStringLiteral("16:9 · 1920 × 1080")});
    form->addRow(QStringLiteral("画布比例"), ratio);
    if (panelMode) form->setRowVisible(ratio, false);
    auto *slot=new QComboBox; slot->setObjectName(QStringLiteral("imageLayers")); slot->setPlaceholderText(QStringLiteral("请选择图片图层")); imageForm->addRow(QStringLiteral("图片图层"),slot);imageForm->setRowVisible(slot,false);
    auto *import = new QPushButton; import->setObjectName(QStringLiteral("addImage")); addTool(import,false);
    auto *reset = new QPushButton(QStringLiteral("完整原图适应画布")); reset->setObjectName(QStringLiteral("resetImage")); imageForm->addRow(reset);
    auto *fitLeft=new QPushButton(QStringLiteral("原图适应左半封面"));fitLeft->setObjectName(QStringLiteral("fitImageLeft"));imageForm->addRow(fitLeft);
    auto *fitRight=new QPushButton(QStringLiteral("原图适应右半封面"));fitRight->setObjectName(QStringLiteral("fitImageRight"));imageForm->addRow(fitRight);
    auto *crop = new QPushButton(QStringLiteral("画布内裁剪")); crop->setObjectName(QStringLiteral("cropImage")); imageForm->addRow(crop);
    auto *enhance=new QPushButton(QStringLiteral("高清增强…"));enhance->setObjectName(QStringLiteral("upscaleImage"));imageForm->addRow(enhance);
    auto *cancelEnhance=new QPushButton(QStringLiteral("取消增强"));cancelEnhance->setObjectName(QStringLiteral("cancelUpscale"));tools->addWidget(cancelEnhance);cancelEnhance->hide();
    auto *srSettings=new QPushButton(QStringLiteral("超分服务设置…"));advancedForm->addRow(srSettings);
    connect(srSettings,&QPushButton::clicked,this,&CoverEditor::configureUpscaleService);
    connect(m_upscale,&UpscaleClient::busyChanged,this,[enhance,cancelEnhance](bool busy){enhance->setText(busy?QStringLiteral("正在增强…"):QStringLiteral("高清增强…"));enhance->setEnabled(!busy);cancelEnhance->setVisible(busy);});
    connect(cancelEnhance,&QPushButton::clicked,this,[this]{disconnect(m_srCompletion);m_upscale->cancel();});
    connect(m_upscale,&UpscaleClient::failed,this,[this](const QString &message){disconnect(m_srCompletion);QMessageBox::warning(this,QStringLiteral("高清增强失败"),message);});
    connect(enhance,&QPushButton::clicked,this,[this,canvas]{
        const int index=canvas->selected;
        if(index<0 || index>=m_document.imageLayers.size() || !canvas->editableSelection() || canvas->cropping || m_upscale->busy())return;
        QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("高清增强"));auto *layout=new QFormLayout(&dialog);
        auto *notice=new QLabel(QStringLiteral("所选图片将上传至超分服务，增强成功后可撤销恢复原图。"));notice->setWordWrap(true);layout->addRow(notice);
        auto *scale=new QComboBox;scale->addItem(QStringLiteral("2 倍"),2);scale->addItem(QStringLiteral("4 倍"),4);layout->addRow(QStringLiteral("放大倍数"),scale);
        auto *configure=new QPushButton(QStringLiteral("服务设置…"));layout->addRow(configure);connect(configure,&QPushButton::clicked,this,&CoverEditor::configureUpscaleService);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return;
        const auto original=m_document.imageLayers[index];const auto baseline=m_document;
        // Reject stale replies after deletion, reordering, undo, or a newly loaded document.
        disconnect(m_srCompletion);
        auto connection=std::make_shared<QMetaObject::Connection>();
        *connection=connect(m_upscale,&UpscaleClient::completed,this,[this,canvas,index,baseline,connection](const QImage &image){
            disconnect(*connection);
            if(!(m_document==baseline)){QMessageBox::information(this,QStringLiteral("增强已完成"),QStringLiteral("编辑内容已变化，本次结果未覆盖当前图片。请重新选择图片增强。"));return;}
            m_document.imageLayers[index].image=image;canvas->selected=index;canvas->selectedText=-1;emit documentChanged();refresh();
        });
        m_upscale->start(QUrl(m_srUrl),m_srToken,original.image,scale->currentData().toInt());
        m_srCompletion=*connection;
        if(!m_upscale->busy())disconnect(m_srCompletion);
    });
    auto *cropActions=new QWidget;cropActions->setFixedHeight(30);auto *cropRow=new QHBoxLayout(cropActions);cropRow->setContentsMargins(0,0,0,0);
    auto *cropDone=new QPushButton(QStringLiteral("完成裁剪"));cropDone->setObjectName(QStringLiteral("finishCrop"));
    auto *cropCancel=new QPushButton(QStringLiteral("取消裁剪"));cropCancel->setObjectName(QStringLiteral("cancelCrop"));
    cropRow->addWidget(cropDone);cropRow->addWidget(cropCancel);imageForm->addRow(cropActions);cropActions->hide();
    auto *zoomButtons=new QWidget;zoomButtons->setFixedHeight(30); auto *zoomRow=new QHBoxLayout(zoomButtons); zoomRow->setContentsMargins(0,0,0,0);
    auto *smaller=new QPushButton(QStringLiteral("缩小 −")); auto *larger=new QPushButton(QStringLiteral("放大 +"));
    zoomRow->addWidget(smaller);zoomRow->addWidget(larger);imageForm->addRow(zoomButtons);imageForm->setRowVisible(zoomButtons,false);
    auto *clear = new QPushButton(QStringLiteral("删除当前图片")); clear->setObjectName(QStringLiteral("removeImage")); imageForm->addRow(clear);imageForm->setRowVisible(clear,true);
    auto *order=new QWidget;order->setFixedHeight(30);auto *orderRow=new QHBoxLayout(order);orderRow->setContentsMargins(0,0,0,0);
    auto *raise=new QPushButton(QStringLiteral("上移一层"));auto *lower=new QPushButton(QStringLiteral("下移一层"));
    orderRow->addWidget(raise);orderRow->addWidget(lower);imageForm->addRow(order);imageForm->setRowVisible(order,false);
    const auto syncImages=[this,canvas,slot,reset,fitLeft,fitRight,crop,smaller,larger,clear,raise,lower,enhance] {
        const QSignalBlocker blocker(slot);slot->clear();
        for(int i=0;i<m_document.imageLayers.size();++i) slot->addItem(QStringLiteral("%1 · %2").arg(i+1).arg(m_document.imageLayers[i].name));
        slot->setCurrentIndex(canvas->selected);
        const bool valid=canvas->editableSelection() && !canvas->cropping && canvas->selected>=0 && canvas->selected<m_document.imageLayers.size();
        if(canvas->selectionUiChanged)canvas->selectionUiChanged();
        enhance->setEnabled(valid && !m_upscale->busy());
        for(QWidget *w : std::initializer_list<QWidget *>{reset,fitLeft,fitRight,crop,smaller,larger,clear,raise,lower}) w->setEnabled(valid);
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
    const auto loadImages=[this,canvas,syncImages](const QStringList &paths,QPointF point,bool dropped){
        canvas->endTextEditing();int loaded=0;
        for(const auto &path:paths) {
            QImageReader reader(path);reader.setAutoTransform(true);const auto original=reader.size();
            if(original.width()>4096 || original.height()>4096)reader.setScaledSize(original.scaled(4096,4096,Qt::KeepAspectRatio));
            const auto image=reader.read();
            if(image.isNull()){QMessageBox::warning(this,QStringLiteral("读取失败"),QFileInfo(path).fileName()+QStringLiteral(": ")+reader.errorString());continue;}
            enableImageLayers(m_document);auto layer=makeImageLayer(image,m_document.outputSize,QFileInfo(path).fileName());layer.z=nextLayerZ(m_document);
            if(dropped)layer.frame.moveCenter(QPointF(std::clamp(point.x()+loaded*.025,0.,1.),std::clamp(point.y()+loaded*.025,0.,1.)));
            m_document.imageLayers.append(layer);canvas->selected=m_document.imageLayers.size()-1;++loaded;
        }
        if(!loaded)return;
        canvas->selectedText=-1;if(canvas->textSelected)canvas->textSelected(-1);syncImages();refresh();
    };
    canvas->imagesDropped=[loadImages](const QStringList &paths,QPointF point){loadImages(paths,point,true);};
    connect(import,&QPushButton::clicked,this,[this,loadImages]{
        const auto paths=QFileDialog::getOpenFileNames(this,QStringLiteral("添加图片"),{},QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"));
        loadImages(paths,{},false);
    });
    const auto fitOriginal=[this,canvas](int region){
        const int i=canvas->selected;if(i<0 || i>=m_document.imageLayers.size())return;
        if(!canvas->editableSelection() || canvas->cropping)return;
        auto &layer=m_document.imageLayers[i];
        if(layer.image.isNull() || m_document.outputSize.isEmpty())return;
        qreal left=0,width=1;
        if(region==1)width=canvas->guides.size()==2?canvas->guides[0]:.5;
        if(region==2){left=canvas->guides.size()==2?canvas->guides[1]:.5;width=1-left;}
        const qreal height=qreal(layer.image.height())*m_document.outputSize.width()*width
            /layer.image.width()/m_document.outputSize.height();
        layer.crop=QRectF(0,0,1,1);
        layer.frame=QRectF(left,(1-height)/2,width,height);
        refresh();
    };
    connect(reset,&QPushButton::clicked,this,[fitOriginal]{fitOriginal(0);});
    connect(fitLeft,&QPushButton::clicked,this,[fitOriginal]{fitOriginal(1);});
    connect(fitRight,&QPushButton::clicked,this,[fitOriginal]{fitOriginal(2);});
    connect(clear,&QPushButton::clicked,this,[this,canvas,syncImages]{
        const int i=canvas->selected;if(i<0 || i>=m_document.imageLayers.size())return;
        m_document.imageLayers.removeAt(i);canvas->selected=std::min(i,int(m_document.imageLayers.size())-1);syncImages();refresh();
    });
    connect(crop,&QPushButton::clicked,this,[canvas]{canvas->beginCrop();});
    connect(cropDone,&QPushButton::clicked,this,[canvas]{canvas->finishCrop(true);});
    connect(cropCancel,&QPushButton::clicked,this,[canvas]{canvas->finishCrop(false);});
    canvas->cropStateChanged=[canvas,cropActions,slot,ratio,import,syncImages]{
        cropActions->setVisible(canvas->cropping);slot->setEnabled(!canvas->cropping);
        ratio->setEnabled(!canvas->cropping);import->setEnabled(!canvas->cropping);syncImages();
    };
    connect(raise,&QPushButton::clicked,this,[moveLayer]{moveLayer(1);});
    connect(lower,&QPushButton::clicked,this,[moveLayer]{moveLayer(-1);});
    syncImages();
    form->addRow(properties);
    form->addRow(more);form->addRow(advanced);advanced->hide();
    auto *textChoice = new QComboBox; textChoice->setObjectName(QStringLiteral("textLayers")); textChoice->setPlaceholderText(QStringLiteral("请选择文字图层"));
    textForm->addRow(QStringLiteral("文字图层"), textChoice);textForm->setRowVisible(textChoice,false);
    auto *textActions = new QWidget; textActions->setFixedHeight(30); auto *textButtons = new QHBoxLayout(textActions); textButtons->setContentsMargins(0,0,0,0);
    auto *addText = new QPushButton(QStringLiteral("添加文字")); addText->setObjectName(QStringLiteral("addText"));
    auto *removeText = new QPushButton(QStringLiteral("删除文字")); removeText->setObjectName(QStringLiteral("removeText"));
    addTool(addText,true);textButtons->addWidget(removeText); textForm->addRow(textActions);textForm->setRowVisible(textActions,true);
    if (!panelMode) {
        auto *background = new QPushButton(QStringLiteral("画布底色…"));tools->addWidget(background);
        connect(background,&QPushButton::clicked,this,&CoverEditor::chooseBackgroundColor);
    }
    tools->addStretch();
    auto *content = new QPlainTextEdit; content->setObjectName(QStringLiteral("textContent")); content->setFixedHeight(72);
    textForm->addRow(QStringLiteral("内容"), content);
    auto *family = new QComboBox; family->setObjectName(QStringLiteral("textFont"));
    family->addItem(QStringLiteral("系统默认"), QString{});
    for (const auto &name : QFontDatabase::families()) family->addItem(name, name);
    textForm->addRow(QStringLiteral("字体"), family);
    auto *size = new QSpinBox; size->setObjectName(QStringLiteral("textSize")); size->setRange(6,1000); size->setSuffix(QStringLiteral(" px"));
    textForm->addRow(QStringLiteral("字号"), size);
    auto *bold = new QCheckBox(QStringLiteral("加粗")); textForm->addRow(bold);
    auto *writing = new QComboBox; writing->setObjectName(QStringLiteral("textWritingMode"));
    writing->addItems({QStringLiteral("横排"), QStringLiteral("竖排（文字正立）")});
    textForm->addRow(QStringLiteral("文字排列"), writing);
    auto *horizontalSpacing = new QSpinBox;horizontalSpacing->setObjectName(QStringLiteral("horizontalTextSpacing"));
    horizontalSpacing->setRange(-1000,1000);horizontalSpacing->setSingleStep(1);horizontalSpacing->setAccelerated(true);horizontalSpacing->setSuffix(QStringLiteral(" px"));
    horizontalSpacing->setToolTip(QStringLiteral("默认 0 使用字体原间距；正数拉开、负数收紧左右间距，单位为导出像素。"));
    textForm->addRow(QStringLiteral("左右字间距"),horizontalSpacing);textForm->setRowVisible(horizontalSpacing,false);
    auto *spacing = new QSpinBox; spacing->setObjectName(QStringLiteral("verticalTextSpacing"));
    spacing->setRange(-1000,1000); spacing->setSingleStep(1); spacing->setAccelerated(true); spacing->setSuffix(QStringLiteral(" px"));
    spacing->setToolTip(QStringLiteral("默认 0 按字号紧凑排列；正数拉开、负数收紧上下间距，单位为导出像素。"));
    textForm->addRow(QStringLiteral("上下字间距"),spacing); textForm->setRowVisible(spacing,false);
    auto *rotation = new QDoubleSpinBox; rotation->setObjectName(QStringLiteral("textRotation"));
    rotation->setRange(-360,360);rotation->setDecimals(2);rotation->setSingleStep(1);rotation->setSuffix(QStringLiteral("°"));
    rotation->setAccelerated(true);rotation->setToolTip(QStringLiteral("输入旋转角度，支持负数及小数；正数顺时针。"));
    textForm->addRow(QStringLiteral("旋转"), rotation);
    auto *color = new QPushButton(QStringLiteral("文字颜色…")); textForm->addRow(color);
    auto *advancedText=new QWidget;advancedText->setObjectName(QStringLiteral("advancedTextProperties"));
    auto *advancedTextForm=new QFormLayout(advancedText);advancedTextForm->setContentsMargins(0,8,0,0);advancedForm->addRow(advancedText);
    auto *stroke=new QCheckBox(QStringLiteral("描边"));stroke->setObjectName(QStringLiteral("textStroke"));advancedTextForm->addRow(stroke);
    auto *strokeWidth=new QSpinBox;strokeWidth->setRange(1,100);strokeWidth->setSuffix(QStringLiteral(" px"));strokeWidth->setObjectName(QStringLiteral("textStrokeWidth"));advancedTextForm->addRow(QStringLiteral("描边宽度"),strokeWidth);
    auto *strokeColor=new QPushButton(QStringLiteral("描边颜色…"));advancedTextForm->addRow(strokeColor);
    auto *shadow=new QCheckBox(QStringLiteral("阴影"));shadow->setObjectName(QStringLiteral("textShadow"));advancedTextForm->addRow(shadow);
    auto *shadowX=new QSpinBox;auto *shadowY=new QSpinBox;shadowX->setRange(-200,200);shadowY->setRange(-200,200);
    shadowX->setObjectName(QStringLiteral("textShadowX"));shadowY->setObjectName(QStringLiteral("textShadowY"));
    advancedTextForm->addRow(QStringLiteral("阴影 X"),shadowX);advancedTextForm->addRow(QStringLiteral("阴影 Y"),shadowY);
    auto *shadowColor=new QPushButton(QStringLiteral("阴影颜色…"));advancedTextForm->addRow(shadowColor);
    auto *textBackground=new QCheckBox(QStringLiteral("文字底色"));textBackground->setObjectName(QStringLiteral("textBackground"));advancedTextForm->addRow(textBackground);
    auto *backgroundColor=new QPushButton(QStringLiteral("文字底色颜色…"));advancedTextForm->addRow(backgroundColor);
    const auto syncEffects=[this,canvas,stroke,strokeWidth,strokeColor,shadow,shadowX,shadowY,shadowColor,textBackground,backgroundColor,advancedTextForm]{
        const int i=canvas->selectedText;const bool valid=i>=0 && i<m_document.textLayers.size();
        for(QWidget *w:std::initializer_list<QWidget *>{stroke,strokeWidth,strokeColor,shadow,shadowX,shadowY,shadowColor,textBackground,backgroundColor})w->setEnabled(valid && !m_document.textLayers[i].locked);
        advancedTextForm->setRowVisible(strokeWidth,valid && m_document.textLayers[i].stroke);
        advancedTextForm->setRowVisible(strokeColor,valid && m_document.textLayers[i].stroke);
        for(QWidget *w:std::initializer_list<QWidget *>{shadowX,shadowY,shadowColor})advancedTextForm->setRowVisible(w,valid && m_document.textLayers[i].shadow);
        advancedTextForm->setRowVisible(backgroundColor,valid && m_document.textLayers[i].background);
        if(!valid)return;const auto &layer=m_document.textLayers[i];
        const QSignalBlocker a(stroke),b(strokeWidth),c(shadow),d(shadowX),e(shadowY),f(textBackground);
        stroke->setChecked(layer.stroke);strokeWidth->setValue(layer.strokeWidth);shadow->setChecked(layer.shadow);
        shadowX->setValue(qRound(layer.shadowOffset.x()));shadowY->setValue(qRound(layer.shadowOffset.y()));textBackground->setChecked(layer.background);
    };
    connect(stroke,&QCheckBox::toggled,this,[this,canvas](bool value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].stroke=value;refresh();});
    connect(strokeWidth,&QSpinBox::valueChanged,this,[this,canvas](int value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].strokeWidth=value;refresh();});
    connect(shadow,&QCheckBox::toggled,this,[this,canvas](bool value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].shadow=value;refresh();});
    connect(shadowX,&QSpinBox::valueChanged,this,[this,canvas](int value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].shadowOffset.setX(value);refresh();});
    connect(shadowY,&QSpinBox::valueChanged,this,[this,canvas](int value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].shadowOffset.setY(value);refresh();});
    connect(textBackground,&QCheckBox::toggled,this,[this,canvas](bool value){if(canvas->selectedText<0)return;m_document.textLayers[canvas->selectedText].background=value;refresh();});
    const auto chooseEffectColor=[this,canvas](int effect){
        if(canvas->selectedText<0)return;auto &layer=m_document.textLayers[canvas->selectedText];
        QColor &color=effect==0?layer.strokeColor:effect==1?layer.shadowColor:layer.textBackground;
        const auto chosen=QColorDialog::getColor(color,this,QStringLiteral("选择颜色"),QColorDialog::ShowAlphaChannel);
        if(chosen.isValid()){color=chosen;refresh();}
    };
    connect(strokeColor,&QPushButton::clicked,this,[chooseEffectColor]{chooseEffectColor(0);});connect(shadowColor,&QPushButton::clicked,this,[chooseEffectColor]{chooseEffectColor(1);});connect(backgroundColor,&QPushButton::clicked,this,[chooseEffectColor]{chooseEffectColor(2);});
    const auto syncText = [this, canvas, content, family, size, bold, writing, spacing, horizontalSpacing, textForm, rotation, color, removeText, syncEffects] {
        syncEffects();
        const int i = canvas->selectedText;
        const bool valid = i >= 0 && i < m_document.textLayers.size();
        if(canvas->selectionUiChanged)canvas->selectionUiChanged();
        for (QWidget *w : std::initializer_list<QWidget *>{content, family, size, bold, writing, spacing, horizontalSpacing, rotation, color, removeText}) w->setEnabled(valid && !m_document.textLayers[i].locked);
        const QSignalBlocker a(content), b(family), c(size), d(bold), e(rotation), f(writing), g(spacing), h(horizontalSpacing);
        textForm->setRowVisible(spacing,valid && m_document.textLayers[i].vertical);
        textForm->setRowVisible(horizontalSpacing,valid && !m_document.textLayers[i].vertical);
        if (!valid) { content->clear(); return; }
        const auto &layer = m_document.textLayers[i];
        content->setPlainText(layer.text); spacing->setValue(layer.verticalSpacing);horizontalSpacing->setValue(layer.horizontalSpacing);
        int fontIndex = family->findData(layer.fontFamily);
        if (fontIndex < 0) { family->addItem(layer.fontFamily, layer.fontFamily); fontIndex = family->count() - 1; }
        family->setCurrentIndex(fontIndex); size->setValue(layer.fontSize);
        bold->setChecked(layer.bold); writing->setCurrentIndex(layer.vertical ? 1 : 0); rotation->setValue(std::fmod(layer.rotation,360.));
    };
    const auto reloadTextList = [this, canvas, textChoice, syncText] {
        const QSignalBlocker blocker(textChoice); textChoice->clear();
        for (int i = 0; i < m_document.textLayers.size(); ++i)
            textChoice->addItem(QStringLiteral("%1 · %2").arg(i+1).arg(m_document.textLayers[i].text.left(18).replace(u'\n', u' ')));
        textChoice->setCurrentIndex(canvas->selectedText); syncText();
    };
    connect(textChoice, &QComboBox::currentIndexChanged, this, [canvas, syncText](int i) {
        if(i>=0)canvas->finishCrop(true);
        canvas->selectedText = i;
        if(i>=0) {canvas->selected=-1;if(canvas->selectionChanged)canvas->selectionChanged(-1);}
        syncText(); canvas->update();
    });
    canvas->textSelected = [textChoice](int i) { textChoice->setCurrentIndex(i); };
    connect(addText, &QPushButton::clicked, this, [this, canvas, reloadTextList, syncImages] {
        canvas->finishCrop(true);
        enableTextLayers(m_document); TextLayer layer;
        layer.fontSize = std::max(6, qRound(m_document.outputSize.width() * (m_document.spineText ? .38 : .065)));
        layer.vertical = m_document.spineText;
        layer.z=nextLayerZ(m_document);m_document.textLayers.append(layer); canvas->selectedText = int(m_document.textLayers.size()) - 1;
        if(canvas->selectedText>=0) {canvas->selected=-1;syncImages();}
        reloadTextList(); refresh();canvas->startTextEditing();
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
    connect(writing, &QComboBox::currentIndexChanged, this, [this, canvas, rotation, spacing, horizontalSpacing, textForm](int value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        auto &layer = m_document.textLayers[i]; layer.vertical = value == 1;
        textForm->setRowVisible(spacing,layer.vertical);textForm->setRowVisible(horizontalSpacing,!layer.vertical);
        layer.rotation = 0;
        const QSignalBlocker blocker(rotation); rotation->setValue(0); refresh();
    });
    connect(horizontalSpacing,&QSpinBox::valueChanged,this,[this,canvas](int value){
        const int i=canvas->selectedText;if(i<0 || i>=m_document.textLayers.size())return;
        m_document.textLayers[i].horizontalSpacing=value;refresh();
    });
    connect(spacing, &QSpinBox::valueChanged, this, [this,canvas](int value) {
        const int i=canvas->selectedText;if(i<0 || i>=m_document.textLayers.size())return;
        m_document.textLayers[i].verticalSpacing=value;refresh();
    });
    connect(rotation, &QDoubleSpinBox::valueChanged, this, [this, canvas](double value) {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        m_document.textLayers[i].rotation = value; refresh();
    });
    connect(color, &QPushButton::clicked, this, [this, canvas] {
        const int i = canvas->selectedText; if (i < 0 || i >= m_document.textLayers.size()) return;
        const auto chosen = QColorDialog::getColor(m_document.textLayers[i].color, this, QStringLiteral("文字颜色"));
        if (chosen.isValid()) { m_document.textLayers[i].color = chosen; refresh(); }
    });
    connect(this, &CoverEditor::documentChanged, this, reloadTextList);
    syncText();
    auto *help = new QLabel(QStringLiteral("拖动摆放 · 中键拖动画布 · 滚轮缩放画布 · 控制点缩放图片 · 双击改字"));
    help->setWordWrap(true); form->addRow(help);
    auto *exportButton = new QPushButton(QStringLiteral("导出 PNG…")); form->addRow(exportButton);
    auto *apply = new QPushButton(QStringLiteral("应用到当前条目")); form->addRow(apply);
    if (panelMode) {
        form->setRowVisible(exportButton, false); form->setRowVisible(apply, false);
    }
    canvas->selectionUiChanged = [this, canvas, properties, heading, syncLayers, syncEffects, advancedText] {
        syncEffects();
        advancedText->setVisible(canvas->selectedText>=0);
        if(!m_restoring && m_recorded==m_document){m_recordedImage=canvas->selected;m_recordedText=canvas->selectedText;}
        syncLayers();
        if(canvas->selectedText>=0 && canvas->selectedText<canvas->doc.textLayers.size()) {
            properties->setCurrentIndex(2);heading->setText(QStringLiteral("文字属性"));
        } else if(canvas->selected>=0 && canvas->selected<canvas->doc.imageLayers.size()) {
            properties->setCurrentIndex(1);heading->setText(canvas->cropping?QStringLiteral("裁剪图片"):QStringLiteral("图片属性"));
        } else {properties->setCurrentIndex(0);heading->setText(QStringLiteral("属性"));}
    };
    canvas->selectionUiChanged();
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
    canvas->inlineEdited=[content](const QString &text){content->setPlainText(text);};
    canvas->inlineText->installEventFilter(this);
    connect(this,&CoverEditor::documentChanged,canvas,[canvas]{canvas->endTextEditing();});
    content->setUndoRedoEnabled(false);content->installEventFilter(this);
    for(auto *input:findChildren<QLineEdit *>())input->installEventFilter(this);
    m_recorded=m_document;
    connect(this, &CoverEditor::documentChanged, this, [this, ratio] {
        const QSignalBlocker a(ratio);
        ratio->setCurrentIndex(m_document.outputSize == QSize(1200,1800) ? 0 : m_document.outputSize == QSize(1200,1600) ? 1 : m_document.outputSize == QSize(1920,1080) ? 2 : -1);

    });
}
void CoverEditor::setDocument(const CoverDocument &document, bool resetHistory)
{
    if (document.outputSize.isEmpty() || document.outputSize.width() > 8192 || document.outputSize.height() > 8192) return;
    static_cast<Canvas *>(m_canvas)->finishCrop(true);
    m_document = document; enableTextLayers(m_document); enableImageLayers(m_document); normalizeLayerOrder(m_document);
    auto *canvas = static_cast<Canvas *>(m_canvas); canvas->selected = m_document.imageLayers.isEmpty() ? -1 : 0; canvas->selectedText = m_document.textLayers.isEmpty() ? -1 : 0;
    if(canvas->selectedText>=0) canvas->selected=-1;
    if(resetHistory){m_history->clear();m_recorded=m_document;m_recordedImage=canvas->selected;m_recordedText=canvas->selectedText;}
    emit documentChanged(); refresh();
}
void CoverEditor::configureUpscaleService()
{
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("超分服务设置"));auto *form=new QFormLayout(&dialog);
    auto *url=new QLineEdit(m_srUrl);url->setMinimumWidth(380);form->addRow(QStringLiteral("POST 地址"),url);
    auto *token=new QLineEdit(m_srToken);token->setEchoMode(QLineEdit::Password);form->addRow(QStringLiteral("访问令牌"),token);
    auto *hint=new QLabel(QStringLiteral("使用 HTTPS；本机调试允许 HTTP。令牌只保留在本次运行中。"));hint->setWordWrap(true);form->addRow(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(!UpscaleClient::validEndpoint(QUrl(url->text().trimmed()))){QMessageBox::warning(&dialog,QStringLiteral("地址无效"),QStringLiteral("请输入 HTTPS 地址，或本机 HTTP 调试地址。"));return;}dialog.accept();});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){m_srUrl=url->text().trimmed();m_srToken=token->text();QSettings().setValue(QStringLiteral("coverMaker/srUrl"),m_srUrl);}
}
void CoverEditor::chooseBackgroundColor()
{
    const auto color = QColorDialog::getColor(m_document.backgroundColor,this,QStringLiteral("选择画布底色"));
    if(color.isValid()) {m_document.backgroundColor=color;refresh();}
}
void CoverEditor::setGuidesVisible(bool visible)
{
    static_cast<Canvas *>(m_canvas)->guidesVisible = visible;
    m_canvas->update();
}
void CoverEditor::setGuideLines(const QVector<qreal> &positions)
{
    static_cast<Canvas *>(m_canvas)->guides = positions; m_canvas->update();
}
bool CoverEditor::eventFilter(QObject *object,QEvent *event)
{
    if(event->type()==QEvent::KeyPress) {
        auto *key=static_cast<QKeyEvent *>(event);
        if(key->matches(QKeySequence::Undo)){auto *canvas=static_cast<Canvas *>(m_canvas);if(canvas->cropping)canvas->finishCrop(false);else m_history->undo();return true;}
        if(key->matches(QKeySequence::Redo)){m_history->redo();return true;}
    }
    return QWidget::eventFilter(object,event);
}
void CoverEditor::refresh()
{
    auto *canvas=static_cast<Canvas *>(m_canvas);
    if(!m_restoring && !canvas->inInteraction() && !(m_recorded==m_document)) {
        quintptr mergeKey=0;
        if(qobject_cast<QPlainTextEdit *>(sender()) || qobject_cast<QSpinBox *>(sender()) || qobject_cast<QDoubleSpinBox *>(sender()) || qobject_cast<QComboBox *>(sender()))
            mergeKey=reinterpret_cast<quintptr>(sender())^quintptr((canvas->selectedText+2)*37+(canvas->selected+2)*127);
        auto restore=[this,canvas](const CoverDocument &document,int image,int text){
            m_restoring=true;m_document=document;m_recorded=document;
            canvas->selected=std::min(image,int(m_document.imageLayers.size())-1);m_recordedImage=canvas->selected;
            canvas->selectedText=std::min(text,int(m_document.textLayers.size())-1);m_recordedText=canvas->selectedText;
            emit documentChanged();m_restoring=false;canvas->update();emit compositionChanged();
        };
        m_history->push(new DocumentCommand(m_recorded,m_document,restore,mergeKey,m_recordedImage,m_recordedText,canvas->selected,canvas->selectedText));m_recorded=m_document;m_recordedImage=canvas->selected;m_recordedText=canvas->selectedText;
    }
    if(canvas->selectionUiChanged)canvas->selectionUiChanged();
    m_canvas->update();emit compositionChanged();
}
}
