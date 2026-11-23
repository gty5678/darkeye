#pragma once
#include <QtWidgets>
#include "../../markdown/renderer.h"

// A formula preview is part of the editor's layout, not a nested scroll area.
class MathPreviewWidget : public QWidget {
    QTextDocument previewDocument;
    qreal scale=1;
public:
    explicit MathPreviewWidget(QWidget *parent):QWidget(parent),previewDocument(this){
        previewDocument.setObjectName("mathPreviewDocument");
        installFormulaImageHandler(&previewDocument);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setFormula(const QString &markdown,const QFont &font,int width){
        previewDocument.setDefaultFont(font);
        QStringList errors;
        renderMarkdown(&previewDocument,markdown,errors);
        previewDocument.setTextWidth(qMax(1,width));
        // Formula objects do not wrap. Fit their full vector geometry in the
        // available width, retaining the aspect ratio and all formula content.
        const qreal naturalWidth=qMax(qreal(width),previewDocument.idealWidth());
        previewDocument.setTextWidth(naturalWidth);
        scale=qMin(qreal(1),qreal(width)/qMax(qreal(1),naturalWidth));
        setFixedHeight(qCeil(previewDocument.size().height()*scale)+2);
        update();
    }
protected:
    void paintEvent(QPaintEvent *)override{
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        painter.scale(scale,scale);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette=palette();
        previewDocument.documentLayout()->draw(&painter,context);
    }
};
