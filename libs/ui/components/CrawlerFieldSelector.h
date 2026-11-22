#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

class QCheckBox;
class QGridLayout;

namespace darkeye
{

struct CrawlerFieldDefinition final
{
    QString key;
    QString label;

    bool operator==(const CrawlerFieldDefinition &) const = default;
};

class CrawlerFieldSelector final : public QWidget
{
    Q_OBJECT

public:
    explicit CrawlerFieldSelector(QWidget *parent = nullptr);

    [[nodiscard]] static QList<CrawlerFieldDefinition> availableFields();
    [[nodiscard]] QSet<QString> selectedFields() const;
    void setSelectedFields(const QSet<QString> &fields);
    bool setFieldChecked(const QString &field, bool checked);
    [[nodiscard]] bool isFieldChecked(const QString &field) const;
    void selectAll();
    void clearSelection();
    void invertSelection();
    void appendRowWidget(QWidget *widget, int column = 0, int columnSpan = 1);

signals:
    void selectionChanged(const QSet<QString> &fields);

private:
    void applySelection(const QSet<QString> &fields);

    QGridLayout *m_layout = nullptr;
    QHash<QString, QCheckBox *> m_checkBoxes;
};

} // namespace darkeye
