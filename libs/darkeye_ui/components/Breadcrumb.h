#pragma once

#include <QStringList>
#include <QWidget>

class QHBoxLayout;

namespace darkeye {

class Breadcrumb final : public QWidget
{
    Q_OBJECT

public:
    explicit Breadcrumb(const QStringList &items = {}, QWidget *parent = nullptr);

    QStringList items() const;
    int currentIndex() const;
    void setItems(const QStringList &items);
    void setCurrentIndex(int index);

signals:
    void crumbClicked(int index, const QString &text);

private:
    void rebuild();

    QStringList m_items;
    int m_currentIndex = -1;
    QHBoxLayout *m_layout = nullptr;
};

} // namespace darkeye
