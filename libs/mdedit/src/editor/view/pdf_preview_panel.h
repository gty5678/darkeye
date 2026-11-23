#pragma once
#include <QtWidgets>
namespace mdedit {
struct PdfPreviewPlacement {
    QString key;
    QUrl file;
    QRect geometry;
    bool ready=false;
};
// Owns PDF panels and their documents; the editor supplies source-derived geometry.
class PdfPreviewPanels {
    Q_DISABLE_COPY_MOVE(PdfPreviewPanels)
    QHash<QString,QWidget *> panels;
public:
    PdfPreviewPanels()=default;
    ~PdfPreviewPanels();
    void sync(QAbstractScrollArea *outer,const QList<PdfPreviewPlacement> &placements);
};
QWidget *createPdfPreviewPanel(QAbstractScrollArea *outer,QWidget *parent,const QUrl &file);
}
