#include "ui/pages/WorkBatchStateWidget.h"
#include "ui/pages/PageScrollArea.h"
#include "ui/components/ModelSearchBar.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenViews.h"

#include <QDir>
#include <QAbstractTableModel>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableView>
#include <QVBoxLayout>

namespace darkeye
{
namespace
{
const QStringList workHeaders = {
    QStringLiteral("work_id"), QStringLiteral("serial_number"), QStringLiteral("director"),
    QStringLiteral("runtime"), QStringLiteral("notes"), QStringLiteral("release_date"),
    QStringLiteral("image_url"), QStringLiteral("video_url"), QStringLiteral("cn_title"),
    QStringLiteral("jp_title"), QStringLiteral("cn_story"), QStringLiteral("jp_story"),
    QStringLiteral("maker_id"), QStringLiteral("label_id"), QStringLiteral("series_id"),
    QStringLiteral("fanart"), QStringLiteral("create_time"), QStringLiteral("update_time"),
    QStringLiteral("is_deleted"), QStringLiteral("javtxt_id"), QStringLiteral("fcover_url"),
    QStringLiteral("on_dan")};
}

class WorkStateTableModel final : public QAbstractTableModel
{
public:
    explicit WorkStateTableModel(QList<WorkStateRecord> &records, bool hasCheckbox,
                                 QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_records(records), m_hasCheckbox(hasCheckbox)
    {
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_records.size();
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : workHeaders.size() + (m_hasCheckbox ? 1 : 0);
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_records.size()) return {};
        if (m_hasCheckbox && index.column() == 0) {
            if (role == Qt::CheckStateRole)
                return m_checkedIds.contains(m_records.at(index.row()).id) ? Qt::Checked
                                                                            : Qt::Unchecked;
            if (role == Qt::BackgroundRole && m_checkedIds.contains(m_records.at(index.row()).id))
                return QColor(255, 215, 215);
            return {};
        }
        if (role != Qt::DisplayRole) return {};
        const int valueColumn = index.column() - (m_hasCheckbox ? 1 : 0);
        const QStringList &values = m_records.at(index.row()).tableValues;
        return valueColumn >= 0 && valueColumn < values.size() ? values.at(valueColumn) : QVariant{};
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return section + 1;
        if (m_hasCheckbox && section == 0) return QStringLiteral("选");
        const int valueColumn = section - (m_hasCheckbox ? 1 : 0);
        return valueColumn >= 0 && valueColumn < workHeaders.size()
            ? workHeaders.at(valueColumn) : QVariant{};
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (m_hasCheckbox && index.column() == 0) result |= Qt::ItemIsUserCheckable;
        return result;
    }

    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override
    {
        if (!m_hasCheckbox || !index.isValid() || index.column() != 0 ||
            role != Qt::CheckStateRole) return false;
        const qint64 id = m_records.at(index.row()).id;
        if (value == Qt::Checked) {
            m_checkedIds.insert(id);
        } else {
            m_checkedIds.remove(id);
        }
        emit dataChanged(index, index, {Qt::CheckStateRole, Qt::BackgroundRole});
        return true;
    }

    void reset()
    {
        beginResetModel();
        m_checkedIds.clear();
        endResetModel();
    }

    void setAllChecked(bool checked)
    {
        if (m_records.isEmpty()) return;
        if (checked) {
            for (const WorkStateRecord &record : m_records) m_checkedIds.insert(record.id);
        } else {
            m_checkedIds.clear();
        }
        emit dataChanged(index(0, 0), index(m_records.size() - 1, 0),
                         {Qt::CheckStateRole, Qt::BackgroundRole});
    }

    QList<qint64> checkedIds() const { return m_checkedIds.values(); }
    qint64 workIdAt(int row) const
    {
        return row >= 0 && row < m_records.size() ? m_records.at(row).id : 0;
    }

private:
    QList<WorkStateRecord> &m_records;
    bool m_hasCheckbox;
    QSet<qint64> m_checkedIds;
};

WorkBatchStateWidget::WorkBatchStateWidget(WorkStateMode mode, QSqlDatabase database,
                                           ThemeService &themes, QString coverDirectory,
                                           QString fanartDirectory, QWidget *parent)
    : LazyWidget(parent), m_mode(mode), m_repository(std::move(database)), m_themes(themes),
      m_coverDirectory(std::move(coverDirectory)), m_fanartDirectory(std::move(fanartDirectory))
{
}

void WorkBatchStateWidget::lazyLoad()
{
    auto *root = new QVBoxLayout(addPageScrollArea(this));
    root->setContentsMargins(0, 0, 0, 0);
    const int checkboxColumns = m_mode == WorkStateMode::Active ? 1 : 0;
    m_table = new TokenTableView(this);
    m_model = new WorkStateTableModel(m_records, checkboxColumns != 0, m_table);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto *searchBar = new ModelSearchBar(m_table, m_themes, this);
    m_search = searchBar->input();
    root->addWidget(searchBar);

    auto *buttons = new QHBoxLayout;
    auto *refreshButton = new IconButton(QStringLiteral("refresh"), QStringLiteral("刷新数据"),
                                         &m_themes, this);
    if (m_mode == WorkStateMode::Active)
    {
        auto *checkAllButton = new IconButton(QStringLiteral("square_check"),
                                              QStringLiteral("全选"), &m_themes, this);
        auto *uncheckAllButton = new IconButton(QStringLiteral("square"),
                                                QStringLiteral("取消全选"), &m_themes, this);
        auto *removeButton = new IconButton(QStringLiteral("trash_2"),
                                            QStringLiteral("将勾选项移入回收站"), &m_themes, this);
        buttons->addWidget(refreshButton);
        buttons->addWidget(removeButton);
        buttons->addStretch();
        buttons->addWidget(checkAllButton);
        buttons->addWidget(uncheckAllButton);
        connect(removeButton, &QPushButton::clicked, this,
                &WorkBatchStateWidget::moveCheckedToRecycleBin);
        connect(checkAllButton, &QPushButton::clicked, this, [this] { setAllChecked(true); });
        connect(uncheckAllButton, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    }
    else
    {
        buttons->addWidget(refreshButton);
        auto *removeButton = new IconButton(QStringLiteral("trash_2"),
                                            QStringLiteral("彻底删除选中项"), &m_themes, this);
        auto *restoreButton = new IconButton(QStringLiteral("undo"), QStringLiteral("恢复选中项"),
                                             &m_themes, this);
        auto *removeAllButton = new DesignButton(QStringLiteral("删除全部"), this);
        removeAllButton->setVariant(QStringLiteral("danger"));
        auto *restoreAllButton = new DesignButton(QStringLiteral("恢复全部"), this);
        buttons->addWidget(removeButton);
        buttons->addWidget(restoreButton);
        buttons->addStretch();
        buttons->addWidget(removeAllButton);
        buttons->addWidget(restoreAllButton);
        connect(restoreButton, &QPushButton::clicked, this, [this] { restore(false); });
        connect(restoreAllButton, &QPushButton::clicked, this, [this] { restore(true); });
        connect(removeButton, &QPushButton::clicked, this, [this] { removePermanently(false); });
        connect(removeAllButton, &QPushButton::clicked, this, [this] { removePermanently(true); });
    }
    root->addLayout(buttons);
    connect(refreshButton, &QPushButton::clicked, this, &WorkBatchStateWidget::refresh);
    connect(m_table, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index)
            {
                if (m_mode != WorkStateMode::Active)
                    return;
                const QModelIndex checkbox = m_model->index(index.row(), 0);
                const auto state = static_cast<Qt::CheckState>(
                    m_model->data(checkbox, Qt::CheckStateRole).toInt());
                m_model->setData(checkbox, state == Qt::Checked ? Qt::Unchecked : Qt::Checked,
                                 Qt::CheckStateRole);
    });
    refresh();
}

void WorkBatchStateWidget::refresh()
{
    if (!m_model) return;
    QString errorMessage;
    m_records = m_repository.listByDeletedState(m_mode == WorkStateMode::RecycleBin,
                                                {}, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    const int checkboxColumns = m_mode == WorkStateMode::Active ? 1 : 0;
    m_model->reset();
    m_table->setColumnHidden(checkboxColumns, true);
}

QList<qint64> WorkBatchStateWidget::checkedIds() const
{
    return m_model->checkedIds();
}

QList<qint64> WorkBatchStateWidget::selectedIds() const
{
    QList<qint64> ids;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows())
    {
        const qint64 id = m_model->workIdAt(index.row());
        if (id > 0) ids.append(id);
    }
    return ids;
}

QList<qint64> WorkBatchStateWidget::allIds() const
{
    QList<qint64> ids;
    ids.reserve(m_records.size());
    for (const WorkStateRecord &record : m_records)
        ids.append(record.id);
    return ids;
}

void WorkBatchStateWidget::setAllChecked(bool checked)
{
    m_model->setAllChecked(checked);
}

void WorkBatchStateWidget::moveCheckedToRecycleBin()
{
    const QList<qint64> ids = checkedIds();
    if (ids.isEmpty())
    {
        Toast::showWarning(window(), QStringLiteral("请先勾选要移入回收站的作品"), &m_themes);
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("确认移入回收站"),
            QStringLiteral("确定将选中的 %1 部作品移入回收站？").arg(ids.size())) !=
        QMessageBox::Yes)
        return;
    QString errorMessage;
    if (!m_repository.setDeletedMany(ids, true, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    refresh();
    emit worksChanged();
    Toast::showSuccess(window(), QStringLiteral("作品已移入回收站"), &m_themes);
}

void WorkBatchStateWidget::restore(bool all)
{
    const QList<qint64> ids = all ? allIds() : selectedIds();
    if (ids.isEmpty())
    {
        Toast::showWarning(window(), all ? QStringLiteral("回收站暂无数据")
                                         : QStringLiteral("请先选择要恢复的行"), &m_themes);
        return;
    }
    const QString title = all ? QStringLiteral("确认恢复全部") : QStringLiteral("确认恢复");
    const QString message = all ? QStringLiteral("确定要恢复回收站内全部 %1 条记录吗？").arg(ids.size())
                                : QStringLiteral("确定要恢复选中的 %1 行吗？").arg(ids.size());
    if (QMessageBox::question(this, title, message) !=
        QMessageBox::Yes)
        return;
    QString errorMessage;
    if (!m_repository.setDeletedMany(ids, false, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    refresh();
    emit worksChanged();
    Toast::showSuccess(window(), QStringLiteral("已恢复 %1 行数据").arg(ids.size()), &m_themes);
}

void WorkBatchStateWidget::removePermanently(bool all)
{
    const QList<qint64> ids = all ? allIds() : selectedIds();
    if (ids.isEmpty())
    {
        Toast::showWarning(window(), all ? QStringLiteral("回收站暂无数据")
                                         : QStringLiteral("请先选择要删除的行"), &m_themes);
        return;
    }
    const QString title = all ? QStringLiteral("确认删除全部") : QStringLiteral("确认删除");
    const QString message = all
        ? QStringLiteral("确定要彻底删除回收站内全部 %1 条记录吗？此操作不可撤销。").arg(ids.size())
        : QStringLiteral("确定要彻底删除选中的 %1 行吗？此操作不可撤销。").arg(ids.size());
    if (QMessageBox::question(this, title, message) !=
        QMessageBox::Yes)
        return;
    QStringList imageUrls;
    QStringList fanartFiles;
    QString errorMessage;
    if (!m_repository.permanentlyRemoveDeletedMany(ids, &imageUrls, &errorMessage, &fanartFiles))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    removeManagedFiles(m_coverDirectory, imageUrls);
    removeManagedFiles(m_fanartDirectory, fanartFiles);
    refresh();
    emit worksChanged();
    Toast::showSuccess(window(), QStringLiteral("已删除 %1 行数据").arg(ids.size()), &m_themes);
}

void WorkBatchStateWidget::removeManagedFiles(const QString &directory,
                                              const QStringList &relativeFiles)
{
    const QString root = QDir(directory).canonicalPath();
    if (root.isEmpty())
        return;
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(root));
    const QString rootPrefix = normalizedRoot + QChar('/');
    for (const QString &relativeFile : relativeFiles)
    {
        if (QFileInfo(relativeFile).isAbsolute())
            continue;
        const QFileInfo candidate(QDir(root).filePath(relativeFile));
        const QString canonical = QDir::fromNativeSeparators(candidate.canonicalFilePath());
        if (!canonical.isEmpty() && canonical.startsWith(rootPrefix, Qt::CaseInsensitive))
            QFile::remove(canonical);
    }
}

} // namespace darkeye
