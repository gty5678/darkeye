#include "darkeye_ui/components/MakerComboDelegate.h"

#include "darkeye_ui/components/DesignInput.h"

#include <QLineEdit>

#include <utility>

namespace darkeye
{

MakerComboDelegate::MakerComboDelegate(QList<MakerOption> makers, int makerColumn,
                                       QObject *parent)
    : QStyledItemDelegate(parent), m_makers(std::move(makers)), m_makerColumn(makerColumn)
{
}

void MakerComboDelegate::setMakers(const QList<MakerOption> &makers)
{
    m_makers = makers;
}

QWidget *MakerComboDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &,
                                          const QModelIndex &index) const
{
    if (index.column() == m_makerColumn)
        return new MakerSelector(m_makers, parent);
    return new DesignLineEdit(parent);
}

void MakerComboDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *maker = qobject_cast<MakerSelector *>(editor))
    {
        const QVariant value = index.data(Qt::EditRole);
        maker->setMaker(value.isValid() && !value.toString().trimmed().isEmpty()
                            ? std::optional<qint64>(value.toLongLong())
                            : std::nullopt);
        return;
    }
    if (auto *lineEdit = qobject_cast<QLineEdit *>(editor))
        lineEdit->setText(index.data(Qt::EditRole).toString());
}

void MakerComboDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                      const QModelIndex &index) const
{
    if (auto *maker = qobject_cast<MakerSelector *>(editor))
    {
        const std::optional<qint64> makerId = maker->maker();
        model->setData(index, makerId.has_value() ? QVariant::fromValue(*makerId) : QVariant{},
                       Qt::EditRole);
        return;
    }
    if (auto *lineEdit = qobject_cast<QLineEdit *>(editor))
        model->setData(index, lineEdit->text(), Qt::EditRole);
}

void MakerComboDelegate::updateEditorGeometry(QWidget *editor,
                                              const QStyleOptionViewItem &option,
                                              const QModelIndex &) const
{
    editor->setGeometry(option.rect);
}

} // namespace darkeye
