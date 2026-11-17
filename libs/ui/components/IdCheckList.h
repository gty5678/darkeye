#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QLineEdit;
class QListWidget;

namespace darkeye
{

struct IdLabelOption final
{
    qint64 id = 0;
    QString label;
    QString group;
    QString imagePath;
};

class IdCheckList final : public QWidget
{
    Q_OBJECT

public:
    explicit IdCheckList(QString title = {}, QWidget *parent = nullptr);

    void setOptions(const QList<IdLabelOption> &options);
    [[nodiscard]] QList<IdLabelOption> options() const;
    [[nodiscard]] QList<qint64> selectedIds() const;
    void setSelectedIds(const QList<qint64> &ids);
    void clearSelection();

signals:
    void selectionChanged();

private:
    void applyFilter(const QString &text);

    QList<IdLabelOption> m_options;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};

} // namespace darkeye
