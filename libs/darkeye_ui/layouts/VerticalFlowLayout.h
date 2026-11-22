#pragma once

#include <QLayout>
#include <QList>

namespace darkeye {

// 从上向下填充，空间不足时向左换列，对应 Python 组件库的 VFlowLayout。
class VerticalFlowLayout final : public QLayout
{
public:
    explicit VerticalFlowLayout(QWidget *parent = nullptr, int margin = 0,
                                int spacing = 10);
    ~VerticalFlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

    int widthForHeight(int height) const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;

    QList<QLayoutItem *> m_items;
};

} // namespace darkeye
