#include "ui/pages/MakerManagementWidget.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QKeyEvent>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QSqlError>
#include <QStyledItemDelegate>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace darkeye
{

namespace
{

class PrefixMakerDelegate final : public QStyledItemDelegate
{
public:
    PrefixMakerDelegate(const QList<ReferenceRecord> &makers, QObject *parent)
        : QStyledItemDelegate(parent), m_makers(makers) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new DesignComboBox(parent);
        combo->addItem(QStringLiteral("请选择制作商"), 0);
        for (const auto &maker : m_makers)
            if (maker.id > 0)
                combo->addItem(maker.chineseName.isEmpty() ? maker.japaneseName : maker.chineseName,
                               maker.id);
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        combo->setCurrentIndex(qMax(0, combo->findData(index.data(Qt::UserRole))));
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        model->setData(index, combo->currentData(), Qt::UserRole);
        model->setData(index, combo->currentData().toLongLong() > 0 ? combo->currentText() : QString(),
                       Qt::EditRole);
    }

private:
    const QList<ReferenceRecord> &m_makers;
};

class MakerRedirectDialog final : public QDialog
{
public:
    MakerRedirectDialog(const QList<ReferenceRecord> &makers, qint64 selectedSourceId,
                        QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("重定向片商"));
        setModal(true);
        setMinimumWidth(620);

        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        m_source = new DesignComboBox(this);
        m_target = new DesignComboBox(this);
        m_source->addItem(QStringLiteral("请选择被重定向片商"), QVariant());
        m_target->addItem(QStringLiteral("请选择保留片商"), QVariant());
        for (const ReferenceRecord &maker : makers)
        {
            const QString name = maker.chineseName.trimmed().isEmpty()
                                     ? maker.japaneseName.trimmed()
                                     : maker.chineseName.trimmed();
            const QString displayName = QStringLiteral("%1 (#%2)").arg(name).arg(maker.id);
            m_source->addItem(displayName, maker.id);
            m_target->addItem(displayName, maker.id);
        }
        m_source->setCurrentIndex(qMax(0, m_source->findData(selectedSourceId)));
        form->addRow(QStringLiteral("被重定向片商"), m_source);
        form->addRow(QStringLiteral("保留片商"), m_target);
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

MakerManagementWidget::MakerManagementWidget(QSqlDatabase database, ThemeService &themes,
                                             QWidget *parent)
    : LazyWidget(parent), m_database(std::move(database)), m_repository(m_database),
      m_jsonService(m_database), m_themes(themes)
{
}

void MakerManagementWidget::lazyLoad()
{
    auto *root = new QVBoxLayout(addPageScrollArea(this));
    root->setContentsMargins(0, 0, 0, 0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_prefixTable = new TokenTableWidget(0, 3, splitter);
    m_prefixTable->setHorizontalHeaderLabels(
        {QStringLiteral("番号前缀"), QStringLiteral("制作商"), QStringLiteral("ID")});
    m_makerTable = new TokenTableWidget(0, 4, splitter);
    m_makerTable->setHorizontalHeaderLabels(
        {QStringLiteral("中文名"), QStringLiteral("日文名"), QStringLiteral("别名"), QStringLiteral("ID")});
    constexpr int minimumVisibleRows = 5;
    for (QTableWidget *table : {m_prefixTable, m_makerTable})
    {
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->ensurePolished();
        table->setMinimumHeight(table->horizontalHeader()->sizeHint().height()
                                + minimumVisibleRows * table->verticalHeader()->defaultSectionSize()
                                + 2 * table->frameWidth()
                                + table->horizontalScrollBar()->sizeHint().height());
    }
    m_prefixTable->setColumnHidden(2, true);
    m_makerTable->setColumnHidden(3, true);
    m_prefixTable->setItemDelegateForColumn(1, new PrefixMakerDelegate(m_makers, m_prefixTable));
    splitter->addWidget(m_prefixTable);
    splitter->addWidget(m_makerTable);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto *searchLayout = new QHBoxLayout;
    searchLayout->setContentsMargins(9, 9, 9, 9);
    m_search = new DesignLineEdit(this);
    m_search->setProperty("makerControlId", QStringLiteral("MakerManagementSearch"));
    m_search->setAccessibleName(QStringLiteral("搜索当前表格"));
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(200);
    m_search->installEventFilter(this);
    m_searchResult = new DesignLabel(QStringLiteral("无搜索结果"), this);
    m_searchResult->setProperty("makerControlId", QStringLiteral("MakerManagementSearchResult"));
    m_searchResult->setFixedWidth(70);
    m_previousSearch = new IconButton(QStringLiteral("arrow_up"), &m_themes, this);
    m_nextSearch = new IconButton(QStringLiteral("arrow_down"), &m_themes, this);
    m_previousSearch->setAccessibleName(QStringLiteral("上一个搜索结果"));
    m_nextSearch->setAccessibleName(QStringLiteral("下一个搜索结果"));
    m_previousSearch->setToolTip(QStringLiteral("向前搜索(Shift+Enter)"));
    m_nextSearch->setToolTip(QStringLiteral("向后搜索(Enter)"));
    m_previousSearch->setEnabled(false);
    m_nextSearch->setEnabled(false);
    searchLayout->addWidget(m_search);
    searchLayout->addWidget(m_searchResult);
    searchLayout->addWidget(m_previousSearch);
    searchLayout->addWidget(m_nextSearch);
    searchLayout->addStretch();
    root->addLayout(searchLayout);
    connect(m_search, &QLineEdit::textChanged, this, [this] { performSearch(); });
    connect(m_previousSearch, &QPushButton::clicked, this, [this] { navigateSearch(-1); });
    connect(m_nextSearch, &QPushButton::clicked, this, [this] { navigateSearch(1); });
    for (QTableWidget *table : {m_prefixTable, m_makerTable})
    {
        connect(table, &QTableWidget::itemChanged, this, [this] { scheduleSearchUpdate(); });
        connect(table->model(), &QAbstractItemModel::rowsInserted,
                this, [this] { scheduleSearchUpdate(); });
        connect(table->model(), &QAbstractItemModel::rowsRemoved,
                this, [this] { scheduleSearchUpdate(); });
    }

    auto *buttons = new QHBoxLayout;
    const auto iconButton = [this](const QString &icon, const QString &toolTip) {
        auto *button = new IconButton(icon, &m_themes, this);
        button->setToolTip(toolTip);
        return button;
    };
    auto *newButton = iconButton(QStringLiteral("list_plus"), QStringLiteral("新增行"));
    auto *removeButton = iconButton(QStringLiteral("list_x"), QStringLiteral("删除行"));
    auto *saveButton = iconButton(QStringLiteral("save"), QStringLiteral("保存修改"));
    auto *revertButton = iconButton(QStringLiteral("eraser"), QStringLiteral("撤销修改"));
    auto *refreshButton = iconButton(QStringLiteral("refresh"), QStringLiteral("读数据库数据"));
    auto *exportButton = iconButton(QStringLiteral("arrow_up_to_line"),
                                    QStringLiteral("导出片商前缀到 JSON 文件"));
    auto *importButton = iconButton(QStringLiteral("arrow_down_to_line"),
                                    QStringLiteral("从 JSON 文件导入片商前缀"));
    auto *redirectButton = iconButton(QStringLiteral("share_2"), QStringLiteral("重定向片商"));
    for (QPushButton *button : {newButton, removeButton, saveButton, revertButton, refreshButton,
                                exportButton, importButton})
        buttons->addWidget(button);
    buttons->addStretch();
    buttons->addWidget(redirectButton);
    root->addLayout(buttons);

    auto *forms = new QHBoxLayout;
    auto *prefixForm = new QFormLayout;
    m_prefix = new DesignLineEdit(this);
    m_prefixMaker = new DesignComboBox(this);
    prefixForm->addRow(new DesignLabel(QStringLiteral("番号前缀"), this), m_prefix);
    prefixForm->addRow(new DesignLabel(QStringLiteral("制作商"), this), m_prefixMaker);
    auto *makerForm = new QFormLayout;
    m_chineseName = new DesignLineEdit(this);
    m_japaneseName = new DesignLineEdit(this);
    m_aliases = new DesignLineEdit(this);
    m_detail = new DesignPlainTextEdit(this);
    m_detail->setMaximumHeight(80);
    makerForm->addRow(new DesignLabel(QStringLiteral("中文名"), this), m_chineseName);
    makerForm->addRow(new DesignLabel(QStringLiteral("日文名"), this), m_japaneseName);
    makerForm->addRow(new DesignLabel(QStringLiteral("别名"), this), m_aliases);
    makerForm->addRow(new DesignLabel(QStringLiteral("说明"), this), m_detail);
    forms->addLayout(prefixForm, 1);
    forms->addLayout(makerForm, 1);
    root->addLayout(forms);

    connect(m_prefixTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { if (!m_loading) selectPrefix(row); });
    connect(m_makerTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { if (!m_loading) selectMaker(row); });
    connect(m_prefixTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const int row = item->row();
        if (m_loading || row < 0 || row >= m_prefixes.size()) return;
        auto &record = m_prefixes[row];
        if (item->column() == 0) record.prefix = item->text();
        else if (item->column() == 1)
        {
            record.makerId = item->data(Qt::UserRole).toLongLong();
            record.makerName = item->text();
        }
        else return;
        m_dirtyPrefixes.insert(row);
        if (row == m_prefixTable->currentRow()) selectPrefix(row);
    });
    connect(m_makerTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const int row = item->row();
        if (m_loading || row < 0 || row >= m_makers.size()) return;
        auto &record = m_makers[row];
        if (item->column() == 0) record.chineseName = item->text();
        else if (item->column() == 1) record.japaneseName = item->text();
        else if (item->column() == 2) record.aliases = item->text();
        else return;
        m_dirtyMakers.insert(row);
        if (row == m_makerTable->currentRow()) selectMaker(row);
    });
    m_prefixTable->installEventFilter(this);
    m_makerTable->installEventFilter(this);
    connect(newButton, &QPushButton::clicked, this, &MakerManagementWidget::beginNew);
    connect(saveButton, &QPushButton::clicked, this, &MakerManagementWidget::saveCurrent);
    connect(removeButton, &QPushButton::clicked, this, &MakerManagementWidget::removeCurrent);
    connect(revertButton, &QPushButton::clicked, this, [this] {
        reloadTables(m_activeTable == ActiveTable::Prefixes, m_activeTable == ActiveTable::Makers);
    });
    connect(refreshButton, &QPushButton::clicked, this, &MakerManagementWidget::refresh);
    connect(redirectButton, &QPushButton::clicked, this, &MakerManagementWidget::redirectMaker);
    connect(exportButton, &QPushButton::clicked, this, &MakerManagementWidget::exportJson);
    connect(importButton, &QPushButton::clicked, this, &MakerManagementWidget::importJson);
    connect(&m_themes, &ThemeService::themeChanged, this,
            [this](ThemeId) { updateActiveTableIndicator(); });
    connect(m_prefix, &QLineEdit::textChanged, this, &MakerManagementWidget::storePrefixEdits);
    connect(m_prefixMaker, &QComboBox::currentIndexChanged, this, &MakerManagementWidget::storePrefixEdits);
    for (QLineEdit *edit : {m_chineseName, m_japaneseName, m_aliases})
        connect(edit, &QLineEdit::textChanged, this, &MakerManagementWidget::storeMakerEdits);
    connect(m_detail, &QPlainTextEdit::textChanged, this, &MakerManagementWidget::storeMakerEdits);
    refresh();
}

void MakerManagementWidget::refresh()
{
    if (!m_prefixTable) return;
    reloadTables(true, true);
}

void MakerManagementWidget::reloadTables(bool prefixes, bool makers)
{
    QString error;
    const auto prefixRecords = prefixes ? m_repository.listMakerPrefixes(&error) : m_prefixes;
    const auto makerRecords = error.isEmpty() && makers
                                  ? m_repository.list(ReferenceKind::Maker, &error) : m_makers;
    if (!error.isEmpty())
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    QScopedValueRollback<bool> loading(m_loading, true);
    if (prefixes)
    {
        m_prefixes = prefixRecords;
        m_dirtyPrefixes.clear();
        m_removedPrefixes.clear();
        m_currentPrefixId = 0;
        m_prefixTable->setCurrentCell(-1, -1);
        m_prefixTable->setRowCount(m_prefixes.size());
        for (qsizetype row = 0; row < m_prefixes.size(); ++row)
        {
            const auto &record = m_prefixes.at(row);
            m_prefixTable->setItem(row, 0, new QTableWidgetItem(record.prefix));
            auto *maker = new QTableWidgetItem(record.makerName);
            maker->setData(Qt::UserRole, record.makerId);
            m_prefixTable->setItem(row, 1, maker);
            auto *id = new QTableWidgetItem(QString::number(record.id));
            id->setFlags(id->flags() & ~Qt::ItemIsEditable);
            m_prefixTable->setItem(row, 2, id);
        }
        m_prefix->clear();
        m_prefixMaker->setCurrentIndex(0);
    }
    if (makers)
    {
        m_makers = makerRecords;
        m_dirtyMakers.clear();
        m_removedMakers.clear();
        m_currentMakerId = 0;
        m_makerTable->setCurrentCell(-1, -1);
        m_makerTable->setRowCount(m_makers.size());
        for (qsizetype row = 0; row < m_makers.size(); ++row)
        {
            const auto &record = m_makers.at(row);
            m_makerTable->setItem(row, 0, new QTableWidgetItem(record.chineseName));
            m_makerTable->setItem(row, 1, new QTableWidgetItem(record.japaneseName));
            m_makerTable->setItem(row, 2, new QTableWidgetItem(record.aliases));
            auto *id = new QTableWidgetItem(QString::number(record.id));
            id->setFlags(id->flags() & ~Qt::ItemIsEditable);
            m_makerTable->setItem(row, 3, id);
        }
        refreshMakerChoices();
        m_chineseName->clear();
        m_japaneseName->clear();
        m_aliases->clear();
        m_detail->clear();
    }
    updateActiveTableIndicator();
    scheduleSearchUpdate();
}

void MakerManagementWidget::refreshMakerChoices()
{
    const qint64 prefixMakerId = m_prefixMaker->currentData().toLongLong();
    for (QComboBox *combo : {m_prefixMaker})
    {
        combo->clear();
        combo->addItem(QStringLiteral("请选择制作商"), QVariant());
        for (const ReferenceRecord &maker : m_makers)
        {
            if (maker.id <= 0) continue;
            const QString name = maker.chineseName.trimmed().isEmpty() ? maker.japaneseName
                                                                        : maker.chineseName;
            combo->addItem(QStringLiteral("%1 (#%2)").arg(name).arg(maker.id), maker.id);
        }
    }
    m_prefixMaker->setCurrentIndex(qMax(0, m_prefixMaker->findData(prefixMakerId)));
}

void MakerManagementWidget::updateActiveTableIndicator()
{
    const QString activeStyle = QStringLiteral("QTableWidget { border: 2px solid %1; }")
                                    .arg(m_themes.currentTokens().primary);
    const auto applyBorder = [&activeStyle](QTableWidget *table, bool active) {
        table->setStyleSheet(active ? activeStyle : QString());
    };
    applyBorder(m_prefixTable, m_activeTable == ActiveTable::Prefixes);
    applyBorder(m_makerTable, m_activeTable == ActiveTable::Makers);
    if (m_searchTable != m_activeTable)
        performSearch(false);
}

void MakerManagementWidget::scheduleSearchUpdate()
{
    if (m_searchUpdatePending) return;
    m_searchUpdatePending = true;
    QTimer::singleShot(0, this, [this] {
        m_searchUpdatePending = false;
        performSearch(false);
    });
}

void MakerManagementWidget::performSearch(bool selectFirst)
{
    QTableWidget *table = m_activeTable == ActiveTable::Prefixes ? m_prefixTable : m_makerTable;
    m_searchTable = m_activeTable;
    m_searchRows.clear();
    m_searchIndex = -1;
    const QString query = m_search->text().trimmed();
    if (!query.isEmpty())
    {
        for (int row = 0; row < table->rowCount(); ++row)
        {
            bool matches = false;
            for (int column = 0; column < table->columnCount(); ++column)
            {
                const auto *item = table->item(row, column);
                if (item && item->text().contains(query, Qt::CaseInsensitive))
                {
                    matches = true;
                    break;
                }
            }
            if (!matches && m_activeTable == ActiveTable::Makers && row < m_makers.size())
                matches = m_makers.at(row).detail.contains(query, Qt::CaseInsensitive);
            if (matches) m_searchRows.append(row);
        }
    }
    const bool hasResults = !m_searchRows.isEmpty();
    m_previousSearch->setEnabled(hasResults);
    m_nextSearch->setEnabled(hasResults);
    if (!hasResults)
    {
        m_searchResult->setText(query.isEmpty() ? QStringLiteral("无搜索结果")
                                               : QStringLiteral("无匹配结果"));
        return;
    }
    if (selectFirst)
    {
        navigateSearch(1);
        return;
    }
    m_searchIndex = m_searchRows.indexOf(table->currentRow());
    m_searchResult->setText(m_searchIndex >= 0
        ? QStringLiteral("结果 %1/%2").arg(m_searchIndex + 1).arg(m_searchRows.size())
        : QStringLiteral("找到 %1 个结果").arg(m_searchRows.size()));
}

void MakerManagementWidget::navigateSearch(int direction)
{
    const int previousRow = m_searchIndex >= 0 && m_searchIndex < m_searchRows.size()
                                ? m_searchRows.at(m_searchIndex) : -1;
    // Edits and row removal may have changed the matches since the last navigation.
    performSearch(false);
    if (m_searchRows.isEmpty()) return;
    const int previousIndex = m_searchRows.indexOf(previousRow);
    m_searchIndex = previousIndex < 0
                        ? (direction > 0 ? 0 : m_searchRows.size() - 1)
                        : (previousIndex + direction + m_searchRows.size()) % m_searchRows.size();
    auto *table = m_activeTable == ActiveTable::Prefixes ? m_prefixTable : m_makerTable;
    const int row = m_searchRows.at(m_searchIndex);
    table->setCurrentCell(row, 0);
    table->selectRow(row);
    table->scrollToItem(table->item(row, 0), QAbstractItemView::PositionAtCenter);
    m_searchResult->setText(QStringLiteral("结果 %1/%2")
                                .arg(m_searchIndex + 1).arg(m_searchRows.size()));
}

bool MakerManagementWidget::eventFilter(QObject *watched, QEvent *event)
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
    if (event->type() == QEvent::FocusIn)
    {
        if (watched == m_prefixTable)
        {
            m_activeTable = ActiveTable::Prefixes;
            updateActiveTableIndicator();
        }
        else if (watched == m_makerTable)
        {
            m_activeTable = ActiveTable::Makers;
            updateActiveTableIndicator();
        }
    }
    return LazyWidget::eventFilter(watched, event);
}

void MakerManagementWidget::selectPrefix(int row)
{
    if (row < 0 || row >= m_prefixes.size())
        return;
    m_activeTable = ActiveTable::Prefixes;
    updateActiveTableIndicator();
    QScopedValueRollback<bool> loading(m_loading, true);
    const MakerPrefixRecord &record = m_prefixes.at(row);
    m_currentPrefixId = record.id;
    m_prefix->setText(record.prefix);
    m_prefixMaker->setCurrentIndex(qMax(0, m_prefixMaker->findData(record.makerId)));
}

void MakerManagementWidget::selectMaker(int row)
{
    if (row < 0 || row >= m_makers.size())
        return;
    m_activeTable = ActiveTable::Makers;
    updateActiveTableIndicator();
    QScopedValueRollback<bool> loading(m_loading, true);
    const ReferenceRecord &record = m_makers.at(row);
    m_currentMakerId = record.id;
    m_chineseName->setText(record.chineseName);
    m_japaneseName->setText(record.japaneseName);
    m_aliases->setText(record.aliases);
    m_detail->setPlainText(record.detail);
}

void MakerManagementWidget::beginNew()
{
    QTableWidget *table = m_activeTable == ActiveTable::Prefixes ? m_prefixTable : m_makerTable;
    const int row = table->rowCount();
    table->insertRow(row);
    {
        QScopedValueRollback<bool> loading(m_loading, true);
        for (int column = 0; column < table->columnCount(); ++column)
            table->setItem(row, column, new QTableWidgetItem);
        auto *id = table->item(row, table->columnCount() - 1);
        id->setFlags(id->flags() & ~Qt::ItemIsEditable);
    }
    if (m_activeTable == ActiveTable::Prefixes)
    {
        QString makerName;
        for (const auto &maker : m_makers)
            if (maker.id == 1)
                makerName = maker.chineseName.isEmpty() ? maker.japaneseName : maker.chineseName;
        m_prefixes.append({0, QStringLiteral("番号前缀"), 1, makerName});
        {
            QScopedValueRollback<bool> loading(m_loading, true);
            table->item(row, 0)->setText(QStringLiteral("番号前缀"));
            table->item(row, 1)->setText(makerName);
            table->item(row, 1)->setData(Qt::UserRole, 1);
        }
        m_dirtyPrefixes.insert(row);
        table->setCurrentCell(row, 0);
        selectPrefix(row);
        m_prefix->setFocus();
    }
    else
    {
        m_makers.append(ReferenceRecord{});
        m_dirtyMakers.insert(row);
        table->setCurrentCell(row, 0);
        selectMaker(row);
        m_chineseName->setFocus();
    }
    table->scrollToItem(table->item(row, 0));
}

void MakerManagementWidget::storePrefixEdits()
{
    const int row = m_prefixTable->currentRow();
    if (m_loading || row < 0 || row >= m_prefixes.size()) return;
    QScopedValueRollback<bool> loading(m_loading, true);
    auto &record = m_prefixes[row];
    record.prefix = m_prefix->text();
    record.makerId = m_prefixMaker->currentData().toLongLong();
    record.makerName.clear();
    for (const auto &maker : m_makers)
        if (maker.id == record.makerId && maker.id > 0)
            record.makerName = maker.chineseName.isEmpty() ? maker.japaneseName : maker.chineseName;
    m_prefixTable->item(row, 0)->setText(record.prefix);
    m_prefixTable->item(row, 1)->setText(record.makerName);
    m_prefixTable->item(row, 1)->setData(Qt::UserRole, record.makerId);
    m_dirtyPrefixes.insert(row);
}

void MakerManagementWidget::storeMakerEdits()
{
    const int row = m_makerTable->currentRow();
    if (m_loading || row < 0 || row >= m_makers.size()) return;
    QScopedValueRollback<bool> loading(m_loading, true);
    auto &record = m_makers[row];
    record = makerRecord();
    m_makerTable->item(row, 0)->setText(record.chineseName);
    m_makerTable->item(row, 1)->setText(record.japaneseName);
    m_makerTable->item(row, 2)->setText(record.aliases);
    m_dirtyMakers.insert(row);
    scheduleSearchUpdate();
}

void MakerManagementWidget::saveCurrent()
{
    const bool makersChanged = m_activeTable == ActiveTable::Makers
                               && (!m_dirtyMakers.isEmpty() || !m_removedMakers.isEmpty());
    const bool prefixesModified = m_activeTable == ActiveTable::Prefixes
                                  && (!m_dirtyPrefixes.isEmpty() || !m_removedPrefixes.isEmpty());
    if (!makersChanged && !prefixesModified) return;
    QString error;
    if (!m_database.transaction())
    {
        Toast::showError(window(), m_database.lastError().text(), &m_themes);
        return;
    }
    bool success = true;
    const auto &removed = prefixesModified ? m_removedPrefixes : m_removedMakers;
    for (qint64 id : removed)
    {
        success = prefixesModified ? m_repository.removeMakerPrefix(id, &error)
                                  : m_repository.remove(ReferenceKind::Maker, id, &error);
        if (!success) break;
    }
    for (int row = 0; makersChanged && row < m_makers.size() && success; ++row)
    {
        if (!m_dirtyMakers.contains(row)) continue;
        const auto &record = m_makers.at(row);
        success = record.id == 0 ? m_repository.create(record, &error).has_value()
                                 : m_repository.update(record, &error);
    }
    for (int row = 0; prefixesModified && row < m_prefixes.size() && success; ++row)
    {
        if (!m_dirtyPrefixes.contains(row)) continue;
        const auto &record = m_prefixes.at(row);
        success = record.id == 0
                      ? m_repository.createMakerPrefix(record.prefix, record.makerId, &error).has_value()
                      : m_repository.updateMakerPrefix(record, &error);
    }
    if (!success || !m_database.commit())
    {
        if (error.isEmpty()) error = m_database.lastError().text();
        m_database.rollback();
        Toast::showError(window(), error, &m_themes);
        return;
    }
    reloadTables(prefixesModified, makersChanged);
    if (makersChanged) emit referencesChanged(ReferenceKind::Maker);
    if (prefixesModified) emit prefixesChanged();
}

void MakerManagementWidget::removeCurrent()
{
    const bool prefixes = m_activeTable == ActiveTable::Prefixes;
    const qint64 id = prefixes ? m_currentPrefixId : m_currentMakerId;
    QTableWidget *table = prefixes ? m_prefixTable : m_makerTable;
    const int row = table->currentRow();
    if (row < 0)
    {
        Toast::showWarning(window(), QStringLiteral("请先选择要删除的行"), &m_themes);
        return;
    }
    QScopedValueRollback<bool> loading(m_loading, true);
    table->removeRow(row);
    if (prefixes)
    {
        if (id > 0) m_removedPrefixes.insert(id);
        m_prefixes.removeAt(row);
    }
    else
    {
        if (id > 0) m_removedMakers.insert(id);
        m_makers.removeAt(row);
    }
    QSet<int> &dirty = prefixes ? m_dirtyPrefixes : m_dirtyMakers;
    QSet<int> remaining;
    for (int dirtyRow : dirty)
        if (dirtyRow != row) remaining.insert(dirtyRow > row ? dirtyRow - 1 : dirtyRow);
    dirty = remaining;
    if (prefixes)
    {
        m_currentPrefixId = 0;
        m_prefix->clear();
        m_prefixMaker->setCurrentIndex(0);
    }
    else
    {
        m_currentMakerId = 0;
        m_chineseName->clear();
        m_japaneseName->clear();
        m_aliases->clear();
        m_detail->clear();
    }
    table->setCurrentCell(-1, -1);
    table->clearSelection();
}

void MakerManagementWidget::redirectMaker()
{
    MakerRedirectDialog dialog(m_makers, m_currentMakerId, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const qint64 source = dialog.sourceId();
    const qint64 target = dialog.targetId();
    if (source <= 0 || target <= 0 || source == target)
    {
        Toast::showWarning(window(), QStringLiteral("请选择两个不同的制作商"), &m_themes);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("确认重定向"),
                              QStringLiteral("作品和前缀映射将转移到保留的制作商。")) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_repository.redirect(ReferenceKind::Maker, source, target, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    refresh();
    emit referencesChanged(ReferenceKind::Maker);
    emit prefixesChanged();
}

void MakerManagementWidget::importJson()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择片商前缀 JSON 文件"),
                                                       {}, QStringLiteral("JSON Files (*.json)"));
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("确认导入"),
                              QStringLiteral("导入会覆盖当前片商和前缀映射。是否继续？")) != QMessageBox::Yes)
        return;
    QString error;
    if (!m_jsonService.importFromFile(ReferenceKind::Maker, path, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    refresh();
    emit referencesChanged(ReferenceKind::Maker);
    emit prefixesChanged();
}

void MakerManagementWidget::exportJson()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("选择导出 JSON 文件"),
                                                QStringLiteral("maker_prefix.json"),
                                                QStringLiteral("JSON Files (*.json)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) path += QStringLiteral(".json");
    QString error;
    if (!m_jsonService.exportToFile(ReferenceKind::Maker, path, &error))
    {
        Toast::showError(window(), error, &m_themes);
        return;
    }
    Toast::showSuccess(window(), QStringLiteral("片商前缀已导出"), &m_themes);
}

ReferenceRecord MakerManagementWidget::makerRecord() const
{
    return {ReferenceKind::Maker, m_currentMakerId, m_chineseName->text(), m_japaneseName->text(),
            m_aliases->text(), m_detail->toPlainText(), {}};
}

} // namespace darkeye
