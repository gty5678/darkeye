#pragma once

#include <QMap>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QWidget>

#include <optional>

class QAbstractItemView;
class QFrame;
class QHelpEvent;
class QPainter;

namespace darkeye
{

[[nodiscard]] const QStringList &workCompletenessKeys();
[[nodiscard]] const QStringList &workCompletenessLabels();

class WorkCompletenessLedStrip final : public QWidget
{
public:
    explicit WorkCompletenessLedStrip(
        const std::optional<QMap<QString, bool>> &completeness = std::nullopt,
        QWidget *parent = nullptr);

    void setCompleteness(const std::optional<QMap<QString, bool>> &completeness);

private:
    QList<QWidget *> m_cells;
    QList<QFrame *> m_indicators;
};

class WorkCompletenessBitsDelegate final : public QStyledItemDelegate
{
public:
    explicit WorkCompletenessBitsDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

    [[nodiscard]] static QString normalizeBits(const QVariant &value);
    [[nodiscard]] static QString tooltipForBits(const QString &bits);
};

} // namespace darkeye
