#pragma once
#include "CoverDocument.h"
#include <QWidget>

class QUndoStack;
namespace darkeye::cover_maker {
class UpscaleClient;
class CoverEditor : public QWidget {
    Q_OBJECT
public:
    explicit CoverEditor(QWidget *parent = nullptr, bool panelMode = false);
    const CoverDocument &document() const { return m_document; }
    void chooseBackgroundColor();
    void configureUpscaleService();
    void setGuidesVisible(bool visible);
    void setGuideLines(const QVector<qreal> &positions);
    void setDocument(const CoverDocument &document, bool resetHistory = true);
    QUndoStack *undoStack() const { return m_history; }
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
signals:
    void documentChanged();
    void compositionChanged();
    // The host owns entry selection, persistence and success/failure feedback.
    void applyRequested(const QImage &image);
private:
    CoverDocument m_document;
    QWidget *m_canvas = nullptr;
    QUndoStack *m_history = nullptr;
    UpscaleClient *m_upscale = nullptr;
    QString m_srUrl, m_srToken;
    QMetaObject::Connection m_srCompletion;
    CoverDocument m_recorded;
    bool m_restoring = false;
    int m_recordedImage = -1, m_recordedText = -1;
    void refresh();
};
}
