#pragma once

#include <QLayout>
#include <QList>

namespace darkeye {

class WaterfallLayout final : public QLayout
{
public:
    explicit WaterfallLayout(QWidget *parent = nullptr, int columnWidth = 220,
                             int margin = 10, int spacing = 10);
    ~WaterfallLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

    int columnWidth() const;
    void setColumnWidth(int width);
    void setUniformRows(bool enabled);

private:
    int layoutHeight(const QRect &rect, bool testOnly) const;

    QList<QLayoutItem *> m_items;
    int m_columnWidth;
    bool m_uniformRows = false;
};

} // namespace darkeye
