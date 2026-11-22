#include "ui/components/IdCheckList.h"

#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/TokenViews.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSet>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace darkeye
{

IdCheckList::IdCheckList(QString title, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("IdCheckList"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *heading = new QHBoxLayout;
    heading->addWidget(new DesignLabel(std::move(title), this));
    m_search = new DesignLineEdit(this);
    m_search->setObjectName(QStringLiteral("IdCheckListSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    heading->addWidget(m_search, 1);
    layout->addLayout(heading);
    m_list = new TokenListWidget(this);
    m_list->setObjectName(QStringLiteral("IdCheckListItems"));
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);
    connect(m_search, &QLineEdit::textChanged, this, &IdCheckList::applyFilter);
    connect(m_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { emit selectionChanged(); });
}

void IdCheckList::setOptions(const QList<IdLabelOption> &options)
{
    const QList<qint64> selectedList = selectedIds();
    const QSet<qint64> selected(selectedList.cbegin(), selectedList.cend());
    const QSignalBlocker blocker(m_list);
    m_options = options;
    m_list->clear();
    for (const IdLabelOption &option : m_options)
    {
        const QString text = option.group.trimmed().isEmpty()
                                 ? option.label
                                 : QStringLiteral("%1 · %2").arg(option.group, option.label);
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, option.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(option.id) ? Qt::Checked : Qt::Unchecked);
    }
    applyFilter(m_search->text());
}

QList<IdLabelOption> IdCheckList::options() const
{
    return m_options;
}

QList<qint64> IdCheckList::selectedIds() const
{
    QList<qint64> ids;
    for (int row = 0; row < m_list->count(); ++row)
    {
        const QListWidgetItem *item = m_list->item(row);
        if (item->checkState() == Qt::Checked)
            ids.append(item->data(Qt::UserRole).toLongLong());
    }
    return ids;
}

void IdCheckList::setSelectedIds(const QList<qint64> &ids)
{
    const QSet<qint64> selected(ids.cbegin(), ids.cend());
    const QSignalBlocker blocker(m_list);
    for (int row = 0; row < m_list->count(); ++row)
    {
        QListWidgetItem *item = m_list->item(row);
        item->setCheckState(
            selected.contains(item->data(Qt::UserRole).toLongLong()) ? Qt::Checked : Qt::Unchecked);
    }
}

void IdCheckList::clearSelection()
{
    if (selectedIds().isEmpty())
        return;
    setSelectedIds({});
    emit selectionChanged();
}

void IdCheckList::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_list->count(); ++row)
    {
        QListWidgetItem *item = m_list->item(row);
        item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
    }
}

} // namespace darkeye
