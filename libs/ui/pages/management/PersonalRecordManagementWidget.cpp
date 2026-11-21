#include "ui/pages/management/PersonalRecordManagementWidget.h"

#include "database/CsvExport.h"
#include "ui/components/ModelSearchBar.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QModelIndexList>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace darkeye
{
PersonalRecordManagementWidget::PersonalRecordManagementWidget(QSqlDatabase privateDatabase,
                                                               ThemeService &themes, QWidget *parent)
    : QWidget(parent), m_privateDatabase(std::move(privateDatabase)), m_themes(themes)
{
    setObjectName(QStringLiteral("PersonalRecordManagementWidget"));
    auto *root = new QVBoxLayout(this);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new DesignLabel(QStringLiteral("选择表:"), this));
    m_tableChoice = new DesignComboBox(this);
    m_tableChoice->setProperty("managementControlId", QStringLiteral("personalRecordTableChoice"));
    m_tableChoice->addItems({QStringLiteral("love_making"), QStringLiteral("masturbation"),
                             QStringLiteral("sexual_arousal")});
    toolbar->addWidget(m_tableChoice);
    auto *addButton = new IconButton(QStringLiteral("list_plus"), QStringLiteral("新增行"),
                                     &m_themes, this);
    auto *deleteButton = new IconButton(QStringLiteral("list_x"), QStringLiteral("删除行"),
                                        &m_themes, this);
    auto *saveButton = new IconButton(QStringLiteral("save"), QStringLiteral("保存修改"),
                                      &m_themes, this);
    auto *revertButton = new IconButton(QStringLiteral("undo"), QStringLiteral("撤销修改"),
                                        &m_themes, this);
    auto *refreshButton = new IconButton(QStringLiteral("refresh"), QStringLiteral("刷新数据"),
                                         &m_themes, this);
    auto *exportButton = new IconButton(QStringLiteral("arrow_up_to_line"),
                                        QStringLiteral("导出为 CSV"), &m_themes, this);
    for (QPushButton *button : {addButton, deleteButton, saveButton, revertButton, refreshButton,
                                exportButton})
        toolbar->addWidget(button);
    toolbar->addStretch();
    root->addLayout(toolbar);

    m_model = new QSqlTableModel(this, m_privateDatabase);
    m_model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_table = new TokenTableView(this);
    m_table->setObjectName(QStringLiteral("personalRecordTable"));
    m_table->setModel(m_model);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    auto *searchBar = new ModelSearchBar(m_table, m_themes, this);
    m_search = searchBar->input();
    m_search->setProperty("managementControlId", QStringLiteral("personalRecordSearch"));
    root->addWidget(searchBar);

    connect(m_tableChoice, &QComboBox::currentTextChanged, this,
            &PersonalRecordManagementWidget::loadTable);
    connect(addButton, &QPushButton::clicked, this, &PersonalRecordManagementWidget::addRow);
    connect(deleteButton, &QPushButton::clicked, this,
            &PersonalRecordManagementWidget::deleteSelectedRows);
    connect(saveButton, &QPushButton::clicked, this, &PersonalRecordManagementWidget::saveChanges);
    connect(revertButton, &QPushButton::clicked, this,
            &PersonalRecordManagementWidget::revertChanges);
    connect(refreshButton, &QPushButton::clicked, this, &PersonalRecordManagementWidget::refresh);
    connect(exportButton, &QPushButton::clicked, this, &PersonalRecordManagementWidget::exportCsv);

    loadTable(m_tableChoice->currentText());
}

void PersonalRecordManagementWidget::refresh()
{
    if (!m_model->select())
        Toast::showError(window(), QStringLiteral("刷新数据失败: %1").arg(m_model->lastError().text()),
                         &m_themes);
}

void PersonalRecordManagementWidget::loadTable(const QString &tableName)
{
    m_model->revertAll();
    m_model->setTable(tableName);
    if (!m_model->select())
    {
        Toast::showError(window(), QStringLiteral("加载表 %1 失败: %2")
                                      .arg(tableName, m_model->lastError().text()),
                         &m_themes);
        return;
    }
    const QSqlRecord record = m_model->record();
    const int primaryKey = record.indexOf(tableName + QStringLiteral("_id"));
    if (primaryKey >= 0)
        m_table->setColumnHidden(primaryKey, true);
}

void PersonalRecordManagementWidget::addRow()
{
    m_search->clear();
    const int row = m_model->rowCount();
    if (!m_model->insertRow(row))
    {
        Toast::showError(window(), QStringLiteral("新增行失败: %1").arg(m_model->lastError().text()),
                         &m_themes);
        return;
    }
    const QModelIndex index = m_model->index(row, 0);
    m_table->selectRow(index.row());
    m_table->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void PersonalRecordManagementWidget::deleteSelectedRows()
{
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty())
    {
        Toast::showWarning(window(), QStringLiteral("请先选择要删除的行"), &m_themes);
        return;
    }
    QList<int> rows;
    for (const QModelIndex &index : selected)
        rows.append(index.row());
    std::sort(rows.begin(), rows.end(), [](int left, int right) { return left > right; });
    for (const int row : rows)
        if (!m_model->removeRow(row))
        {
            Toast::showError(window(), QStringLiteral("删除行失败: %1").arg(m_model->lastError().text()),
                             &m_themes);
            return;
        }
}

void PersonalRecordManagementWidget::saveChanges()
{
    if (!m_model->submitAll())
    {
        Toast::showError(window(), QStringLiteral("保存失败: %1").arg(m_model->lastError().text()),
                         &m_themes);
        return;
    }
    refresh();
    Toast::showSuccess(window(), QStringLiteral("保存成功"), &m_themes);
}

void PersonalRecordManagementWidget::revertChanges()
{
    m_model->revertAll();
    Toast::showSuccess(window(), QStringLiteral("已撤销修改"), &m_themes);
}

void PersonalRecordManagementWidget::exportCsv()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存为 CSV 文件"), {},
                                                QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");
    QString errorMessage;
    if (!exportModelToCsv(m_model, path, &errorMessage))
    {
        Toast::showError(window(), QStringLiteral("导出失败: %1").arg(errorMessage), &m_themes);
        return;
    }
    Toast::showSuccess(window(), QStringLiteral("已导出 CSV 文件"), &m_themes);
}

} // namespace darkeye
