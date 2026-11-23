#pragma once
#include <QtWidgets>
#include <memory>
#include <vector>
#include "../markdown/block_parser.h"
#include "../resources/resource_manager.h"
namespace mdedit {
class MarkdownPreviewWidget : public QAbstractScrollArea {
    Q_OBJECT
    struct Entry {BlockViewNode block;std::unique_ptr<QTextDocument> text;qreal top=0,height=0;};
    std::vector<Entry> entries;
    QString source;
    QUrl base;
    ResourceManager resources;
    void relayout();
    QTimer windowTimer;
    void updateResourceWindow();
    void buildEntries();
    QString resourceKey(const Entry &entry);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
public:
    explicit MarkdownPreviewWidget(QWidget *parent=nullptr);
    void setSource(const QString &text,const QUrl &baseUrl);
    void refreshAppearance();
    void refreshResources();
signals:
    void sourceActivated(int position);
};
}
