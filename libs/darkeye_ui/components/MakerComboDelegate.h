#pragma once

#include "darkeye_ui/components/MakerSelector.h"

#include <QStyledItemDelegate>

namespace darkeye
{

/** Table editor equivalent of Python's MakerComboDelegate.
 *
 * The configured maker column stores its stable ID while presenting the
 * human-readable maker name. Other columns retain ordinary line-edit
 * editing, so a single delegate can be used for mixed management tables.
 */
class MakerComboDelegate final : public QStyledItemDelegate
{
public:
    explicit MakerComboDelegate(QList<MakerOption> makers = {}, int makerColumn = 2,
                                QObject *parent = nullptr);

    void setMakers(const QList<MakerOption> &makers);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;

private:
    QList<MakerOption> m_makers;
    int m_makerColumn = 2;
};

} // namespace darkeye
