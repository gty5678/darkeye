#pragma once
#include "SleeveDocument.h"
#include <QWidget>
class QSpinBox;
class QLabel;
namespace darkeye::cover_maker {
class CoverEditor;
class SleeveEditor : public QWidget {
    Q_OBJECT
public:
    explicit SleeveEditor(QWidget *parent = nullptr);
    SleeveDocument document() const;
    void setDocument(const SleeveDocument &document);
signals:
    void applyRequested(const QImage &image);
private:
    SleeveDocument m_document;
    std::array<CoverEditor *, 3> m_editors{};
    std::array<QSpinBox *, 4> m_dimensions{};
    QWidget *m_preview = nullptr;
    QLabel *m_sizeLabel = nullptr;
    bool m_loading = false;
    void refresh();
};
}
