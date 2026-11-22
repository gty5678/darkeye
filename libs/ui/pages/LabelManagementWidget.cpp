#include "ui/pages/LabelManagementWidget.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSqlError>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace darkeye
{

namespace
{

class ReferenceRedirectDialog final : public QDialog
{
public:
    ReferenceRedirectDialog(ReferenceKind kind, const QList<ReferenceRecord> &records,
                            QWidget *parent)
        : QDialog(parent)
    {
        const QString kindName = kind == ReferenceKind::Series ? QStringLiteral("系列")
                                                               : QStringLiteral("厂牌");
        setWindowTitle(QStringLiteral("重定向%1").arg(kindName));
        setModal(true);
        setMinimumWidth(620);

        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        m_source = new DesignComboBox(this);
        m_target = new DesignComboBox(this);
        m_source->addItem(QStringLiteral("请选择被重定向%1").arg(kindName), QVariant());
        m_target->addItem(QStringLiteral("请选择保留%1").arg(kindName), QVariant());
        for (const ReferenceRecord &record : records)
        {
            const QString name = record.chineseName.trimmed().isEmpty()
                                     ? record.japaneseName.trimmed()
                                     : record.chineseName.trimmed();
            const QString displayName = QStringLiteral("%1 (#%2)").arg(name).arg(record.id);
            m_source->addItem(displayName, record.id);
            m_target->addItem(displayName, record.id);
        }
        form->addRow(QStringLiteral("被重定向%1").arg(kindName), m_source);
        form->addRow(QStringLiteral("保留%1").arg(kindName), m_target);
        layout->addLayout(form);

        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *confirmButton = new DesignButton(QStringLiteral("确定"), this);
        confirmButton->setVariant(QStringLiteral("primary"));
        auto *cancelButton = new DesignButton(QStringLiteral("取消"), this);
        buttons->addWidget(confirmButton);
        buttons->addWidget(cancelButton);
        layout->addLayout(buttons);

        connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    }

    [[nodiscard]] qint64 sourceId() const { return m_source->currentData().toLongLong(); }
    [[nodiscard]] qint64 targetId() const { return m_target->currentData().toLongLong(); }

private:
    QComboBox *m_source = nullptr;
    QComboBox *m_target = nullptr;
};

} // namespace

LabelManagementWidget::LabelManagementWidget(ReferenceKind kind, QSqlDatabase database, ThemeService &themes,
                                             QWidget *parent)
    : LazyWidget(parent), m_database(std::move(database)), m_kind(kind), m_repository(m_database),
      m_jsonService(m_database), m_themes(themes)
{
}

void LabelManagementWidget::lazyLoad()
{
    const bool series = m_kind == ReferenceKind::Series;
    const QString kindName = series ? QStringLiteral("系列") : QStringLiteral("厂牌");
    auto *root = new QVBoxLayout(addPageScrollArea(this));
    root->setContentsMargins(0, 0, 0, 0);
    m_table = new TokenTableWidget(0, series ? 6 : 5, this);
    m_table->setHorizontalHeaderLabels(
        series ? QStringList{QStringLiteral("中文名"), QStringLiteral("日文名"),
                             QStringLiteral("别名"), QStringLiteral("详情"),
                             QStringLiteral("相关系列"), QStringLiteral("ID")}
               : QStringList{QStringLiteral("中文名"), QStringLiteral("日文名"),
                             QStringLiteral("别名"), QStringLiteral("详情"), QStringLiteral("ID")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setColumnHidden(series ? 5 : 4, true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 180);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 180);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_table->setColumnWidth(2, 220);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_table->setColumnWidth(3, 100);
    if (series)
    {
        m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
        m_table->setColumnWidth(4, 100);
        m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    }
    else
        m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    auto *searchLayout = new QHBoxLayout;
    m_search = new DesignLineEdit(this);
    m_search->setAccessibleName(QStringLiteral("搜索表格"));
    m_search->setProperty("referenceControlId", QStringLiteral("search"));
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(200);
    m_search->installEventFilter(this);
    m_searchResult = new DesignLabel(QStringLiteral("无搜索结果"), this);
    m_searchResult->setFixedWidth(70);
    m_previousSearch = new IconButton(QStringLiteral("arrow_up"), &m_themes, this);
    m_nextSearch = new IconButton(QStringLiteral("arrow_down"), &m_themes, this);
    m_previousSearch->setToolTip(QStringLiteral("向前搜索(Shift+Enter)"));
    m_nextSearch->setToolTip(QStringLiteral("向后搜索(Enter)"));
    m_previousSearch->setAccessibleName(QStringLiteral("上一个搜索结果"));
    m_nextSearch->setAccessibleName(QStringLiteral("下一个搜索结果"));
    searchLayout->addWidget(m_search);
    searchLayout->addWidget(m_searchResult);
    searchLayout->addWidget(m_previousSearch);
    searchLayout->addWidget(m_nextSearch);
    searchLayout->addStretch();
    root->addLayout(searchLayout);

    auto *buttons = new QHBoxLayout;
    auto *newButton = new IconButton(QStringLiteral("list_plus"), QStringLiteral("新增行"),
                                     &m_themes, this);
    auto *deleteButton = new IconButton(QStringLiteral("list_x"), QStringLiteral("删除行"),
                                        &m_themes, this);
    auto *saveButton = new IconButton(QStringLiteral("save"), QStringLiteral("保存修改"),
                                      &m_themes, this);
    auto *revertButton = new IconButton(QStringLiteral("undo"), QStringLiteral("撤销修改"),
                                        &m_themes, this);
    auto *refreshButton = new IconButton(QStringLiteral("refresh"), QStringLiteral("读数据库数据"),
                                         &m_themes, this);
    auto *exportButton = new IconButton(QStringLiteral("arrow_up_to_line"),
                                        QStringLiteral("导出%1到 JSON 文件").arg(kindName),
                                        &m_themes, this);
    auto *importButton = new IconButton(QStringLiteral("arrow_down_to_line"),
                                        QStringLiteral("从 JSON 文件导入%1").arg(kindName),
                                        &m_themes, this);
    auto *redirectButton = new IconButton(QStringLiteral("share_2"),
                                          QStringLiteral("重定向%1").arg(kindName), &m_themes, this);
    for (QPushButton *button : {newButton, deleteButton, saveButton, revertButton, refreshButton,
                                exportButton, importButton})
        buttons->addWidget(button);
    buttons->addStretch();
    buttons->addWidget(redirectButton);
    root->addLayout(buttons);

    auto *form = new QFormLayout;
    m_chineseName = new DesignLineEdit(this);
    m_japaneseName = new DesignLineEdit(this);
    m_aliases = new DesignLineEdit(this);
    form->addRow(new DesignLabel(QStringLiteral("中文名"), this), m_chineseName);
    form->addRow(new DesignLabel(QStringLiteral("日文名"), this), m_japaneseName);
    form->addRow(new DesignLabel(QStringLiteral("别名"), this), m_aliases);
    root->addLayout(form);

    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { if (!m_loading) selectRow(row); });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_loading || item->row() < 0 || item->row() >= m_records.size()) return;
        auto &record = m_records[item->row()];
        switch (item->column())
        {
        case 0: record.chineseName = item->text(); break;
        case 1: record.japaneseName = item->text(); break;
        case 2: record.aliases = item->text(); break;
        case 3: record.detail = item->text(); break;
        case 4: if (m_kind == ReferenceKind::Series) record.extra = item->text(); else return; break;
        default: return;
        }
        m_dirtyRows.insert(item->row());
        if (item->row() == m_table->currentRow()) selectRow(item->row());
        performSearch(false);
    });
    for (auto *edit : {m_chineseName, m_japaneseName, m_aliases})
        connect(edit, &QLineEdit::textChanged, this, &LabelManagementWidget::storeEditorEdits);
    connect(m_search, &QLineEdit::textChanged, this, [this] { performSearch(); });
    connect(m_previousSearch, &QPushButton::clicked, this, [this] { navigateSearch(-1); });
    connect(m_nextSearch, &QPushButton::clicked, this, [this] { navigateSearch(1); });
    connect(newButton, &QPushButton::clicked, this, &LabelManagementWidget::beginNew);
    connect(deleteButton, &QPushButton::clicked, this, &LabelManagementWidget::removeCurrent);
    connect(saveButton, &QPushButton::clicked, this, &LabelManagementWidget::saveCurrent);
    connect(revertButton, &QPushButton::clicked, this, &LabelManagementWidget::refresh);
    connect(refreshButton, &QPushButton::clicked, this, &LabelManagementWidget::refresh);
    connect(redirectButton, &QPushButton::clicked, this, &LabelManagementWidget::redirectCurrent);
    connect(exportButton, &QPushButton::clicked, this, &LabelManagementWidget::exportJson);
    connect(importButton, &QPushButton::clicked, this, &LabelManagementWidget::importJson);
    refresh();
}

void LabelManagementWidget::refresh()
{
    if (!m_table) return;
    QString error;
    m_records = m_repository.list(m_kind, &error);
    if (!error.isEmpty())
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    std::sort(m_records.begin(), m_records.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    m_removedIds.clear();
    m_dirtyRows.clear();
    populateTable();
    if (!m_records.isEmpty()) m_table->setCurrentCell(0, 0);
}

void LabelManagementWidget::populateTable()
{
    {
        QScopedValueRollback<bool> loading(m_loading, true);
        m_table->setRowCount(0);
        m_table->setRowCount(m_records.size());
        for (int row = 0; row < m_records.size(); ++row)
        {
            const auto &record = m_records.at(row);
            const QStringList values{record.chineseName, record.japaneseName, record.aliases,
                                     record.detail, m_kind == ReferenceKind::Series ? record.extra
                                                                                  : QString::number(record.id),
                                     QString::number(record.id)};
            for (int column = 0; column < m_table->columnCount(); ++column)
                m_table->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
    }
    selectRow(-1);
    performSearch(false);
}

void LabelManagementWidget::performSearch(bool selectFirst)
{
    m_searchRows.clear();
    m_searchIndex = -1;
    const QString query = m_search->text().trimmed();
    if (!query.isEmpty())
        for (int row = 0; row < m_table->rowCount(); ++row)
            for (int column = 0; column < m_table->columnCount(); ++column)
                if (m_table->item(row, column)->text().contains(query, Qt::CaseInsensitive))
                {
                    m_searchRows.append(row);
                    break;
                }
    m_previousSearch->setEnabled(!m_searchRows.isEmpty());
    m_nextSearch->setEnabled(!m_searchRows.isEmpty());
    if (m_searchRows.isEmpty())
    {
        m_searchResult->setText(query.isEmpty() ? QStringLiteral("无搜索结果") : QStringLiteral("无匹配结果"));
        return;
    }
    if (selectFirst) { navigateSearch(1); return; }
    m_searchIndex = m_searchRows.indexOf(m_table->currentRow());
    m_searchResult->setText(m_searchIndex >= 0
        ? QStringLiteral("结果 %1/%2").arg(m_searchIndex + 1).arg(m_searchRows.size())
        : QStringLiteral("找到 %1 个结果").arg(m_searchRows.size()));
}

void LabelManagementWidget::navigateSearch(int direction)
{
    if (m_searchRows.isEmpty()) return;
    m_searchIndex = (m_searchIndex + direction + m_searchRows.size()) % m_searchRows.size();
    const int row = m_searchRows.at(m_searchIndex);
    m_table->setCurrentCell(row, 0);
    m_table->selectRow(row);
    m_table->scrollToItem(m_table->item(row, 0), QAbstractItemView::PositionAtCenter);
    m_searchResult->setText(QStringLiteral("结果 %1/%2").arg(m_searchIndex + 1).arg(m_searchRows.size()));
}

bool LabelManagementWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier))
        {
            navigateSearch(key->modifiers() == Qt::ShiftModifier ? -1 : 1);
            return true;
        }
    }
    return LazyWidget::eventFilter(watched, event);
}

void LabelManagementWidget::selectRow(int row)
{
    QScopedValueRollback<bool> loading(m_loading, true);
    const ReferenceRecord record = row >= 0 && row < m_records.size() ? m_records.at(row) : ReferenceRecord{};
    m_chineseName->setText(record.chineseName);
    m_japaneseName->setText(record.japaneseName);
    m_aliases->setText(record.aliases);
}

void LabelManagementWidget::storeEditorEdits()
{
    const int row = m_table->currentRow();
    if (m_loading || row < 0 || row >= m_records.size()) return;
    {
        QScopedValueRollback<bool> loading(m_loading, true);
        auto &record = m_records[row];
        record.chineseName = m_chineseName->text();
        record.japaneseName = m_japaneseName->text();
        record.aliases = m_aliases->text();
        m_table->item(row, 0)->setText(record.chineseName);
        m_table->item(row, 1)->setText(record.japaneseName);
        m_table->item(row, 2)->setText(record.aliases);
        m_dirtyRows.insert(row);
    }
    performSearch(false);
}

void LabelManagementWidget::beginNew()
{
    m_records.append({m_kind, 0, {}, {}, {}, {}, {}});
    m_dirtyRows.insert(m_records.size() - 1);
    populateTable();
    m_table->setCurrentCell(m_records.size() - 1, 0);
    m_table->selectRow(m_records.size() - 1);
    m_table->scrollToBottom();
}

void LabelManagementWidget::saveCurrent()
{
    // Close an active cell editor before collecting all staged changes.
    m_table->setFocus();
    if (!m_database.transaction())
    {
        Toast::showError(window(), m_database.lastError().text(), &m_themes);
        return;
    }
    QString error;
    bool success = true;
    for (qint64 id : m_removedIds)
        if (!m_repository.remove(m_kind, id, &error)) { success = false; break; }
    for (int row = 0; success && row < m_records.size(); ++row)
    {
        if (!m_dirtyRows.contains(row)) continue;
        const auto &record = m_records.at(row);
        success = record.id == 0 ? m_repository.create(record, &error).has_value()
                                 : m_repository.update(record, &error);
    }
    if (success) success = m_database.commit();
    if (!success)
    {
        if (error.isEmpty()) error = m_database.lastError().text();
        m_database.rollback();
        Toast::showError(window(), error, &m_themes);
        return;
    }
    refresh();
    emit referencesChanged(m_kind);
    Toast::showSuccess(window(), QStringLiteral("保存成功"), &m_themes);
}

void LabelManagementWidget::removeCurrent()
{
    const int row = m_table->currentRow();
    if (row < 0 || m_table->selectionModel()->selectedRows().isEmpty())
    {
        Toast::showWarning(window(), QStringLiteral("请先选择要删除的行"), &m_themes);
        return;
    }
    if (m_records.at(row).id > 0) m_removedIds.insert(m_records.at(row).id);
    m_records.removeAt(row);
    QSet<int> dirtyRows;
    for (int dirty : m_dirtyRows)
        if (dirty != row) dirtyRows.insert(dirty > row ? dirty - 1 : dirty);
    m_dirtyRows = dirtyRows;
    populateTable();
    if (!m_records.isEmpty()) m_table->setCurrentCell(qMin(row, int(m_records.size()) - 1), 0);
}

void LabelManagementWidget::redirectCurrent()
{
    ReferenceRedirectDialog dialog(m_kind, m_records, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const qint64 source = dialog.sourceId();
    const qint64 target = dialog.targetId();
    if (source <= 0 || target <= 0 || source == target)
    {
        Toast::showWarning(window(), QStringLiteral("请选择两个不同的%1").arg(
                               m_kind == ReferenceKind::Series ? QStringLiteral("系列") : QStringLiteral("厂牌")),
                           &m_themes);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("确认重定向"),
                              QStringLiteral("作品关联将转移到保留的%1。").arg(
                                  m_kind == ReferenceKind::Series ? QStringLiteral("系列") : QStringLiteral("厂牌"))) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_repository.redirect(m_kind, source, target, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    refresh();
    emit referencesChanged(m_kind);
}

void LabelManagementWidget::importJson()
{
    const QString kindName = m_kind == ReferenceKind::Series ? QStringLiteral("系列") : QStringLiteral("厂牌");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择%1 JSON 文件").arg(kindName), {},
                                                       QStringLiteral("JSON Files (*.json)"));
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("确认导入"),
                              QStringLiteral("导入会覆盖当前%1数据。是否继续？").arg(kindName)) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_jsonService.importFromFile(m_kind, path, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    refresh();
    emit referencesChanged(m_kind);
}

void LabelManagementWidget::exportJson()
{
    const QString kindName = m_kind == ReferenceKind::Series ? QStringLiteral("系列") : QStringLiteral("厂牌");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("选择导出 JSON 文件"),
                                                m_kind == ReferenceKind::Series ? QStringLiteral("series.json") : QStringLiteral("label.json"),
                                                QStringLiteral("JSON Files (*.json)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
    QString error;
    if (!m_jsonService.exportToFile(m_kind, path, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    Toast::showSuccess(window(), QStringLiteral("%1已导出").arg(kindName), &m_themes);
}

} // namespace darkeye
