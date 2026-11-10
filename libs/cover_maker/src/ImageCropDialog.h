#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>

namespace darkeye::cover_maker {
class CropCanvas final : public QWidget {
public:
    QImage image;
    QRectF crop;
    explicit CropCanvas(const QImage &source, const QRectF &initial, QWidget *parent)
        : QWidget(parent), image(source), crop(initial) { setMinimumSize(480,360); setCursor(Qt::CrossCursor); }
    QRectF area() const {
        QSizeF size(image.size()); size.scale(QSizeF(width()-32,height()-32),Qt::KeepAspectRatio);
        return {QPointF((width()-size.width())/2,(height()-size.height())/2),size};
    }
    QPointF normalizedPoint(QPointF point) const {
        const auto a=area();
        return {std::clamp((point.x()-a.x())/a.width(),0.,1.),std::clamp((point.y()-a.y())/a.height(),0.,1.)};
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(),QColor("#141b25")); const auto a=area();
        p.drawImage(a,image); p.fillRect(a,QColor(0,0,0,130));
        const QRectF target(a.x()+crop.x()*a.width(),a.y()+crop.y()*a.height(),crop.width()*a.width(),crop.height()*a.height());
        p.save(); p.setClipRect(target); p.drawImage(a,image); p.restore();
        p.setPen(QPen(QColor("#ffcf57"),2)); p.drawRect(target);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if(e->button()!=Qt::LeftButton || !area().contains(e->position())) return;
        previous=crop; start=normalizedPoint(e->position()); dragging=true;
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(!dragging) return;
        const auto candidate=QRectF(start,normalizedPoint(e->position())).normalized();
        if(candidate.width()*image.width()>=1 && candidate.height()*image.height()>=1) crop=candidate;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(!dragging || e->button()!=Qt::LeftButton) return;
        mouseMoveEvent(e); dragging=false;
        if(QRectF(start,normalizedPoint(e->position())).normalized().width()*image.width()<1
            || QRectF(start,normalizedPoint(e->position())).normalized().height()*image.height()<1) crop=previous;
        update();
    }
private:
    bool dragging=false;
    QPointF start;
    QRectF previous;
};
class ImageCropDialog final : public QDialog {
public:
    CropCanvas *canvas;
    explicit ImageCropDialog(const QImage &image, const QRectF &crop, QWidget *parent) : QDialog(parent) {
        setWindowTitle(QStringLiteral("裁剪图片")); auto *layout=new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("在原图上拖动框选保留范围。裁剪不修改原图，可随时恢复。")));
        canvas=new CropCanvas(image,crop,this); layout->addWidget(canvas,1);
        auto *reset=new QPushButton(QStringLiteral("恢复完整原图")); layout->addWidget(reset);
        connect(reset,&QPushButton::clicked,this,[this]{canvas->crop={0,0,1,1};canvas->update();});
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject); resize(720,600);
    }
};
}
