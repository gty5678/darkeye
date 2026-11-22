#pragma once
#include "SleeveDocument.h"
#include <QWidget>
class QPushButton;
namespace darkeye::cover_maker {
class CoverEditor;
class SleeveEditor : public QWidget {
    Q_OBJECT
public:
    explicit SleeveEditor(QWidget *parent = nullptr);
    SleeveDocument document() const;
    void setApplyVisible(bool visible);
    void setDocument(const SleeveDocument &document, bool resetHistory = true);
signals:
    void applyRequested(const QImage &image);
private:
    SleeveDocument m_document;
    CoverEditor *m_editor = nullptr;
    bool m_loading = false;
    QPushButton *m_apply = nullptr;
    void refresh();
};
}
