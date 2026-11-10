#pragma once
#include "CoverDocument.h"
#include <QWidget>

namespace darkeye::cover_maker {
class CoverEditor : public QWidget {
    Q_OBJECT
public:
    explicit CoverEditor(QWidget *parent = nullptr, bool panelMode = false);
    const CoverDocument &document() const { return m_document; }
    void setDocument(const CoverDocument &document);
signals:
    void documentChanged();
    void compositionChanged();
    // The host owns entry selection, persistence and success/failure feedback.
    void applyRequested(const QImage &image);
private:
    CoverDocument m_document;
    QWidget *m_canvas = nullptr;
    void refresh();
};
}
