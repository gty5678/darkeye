#include "ui/pages/management/SummaryQueryWidget.h"

#include "database/CsvExport.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "ui/components/WorkCompletenessIndicators.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSqlError>
#include <QSqlQueryModel>
#include <QSqlRecord>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <functional>

namespace darkeye
{
namespace
{
class WorkSummaryQueryModel final : public QSqlQueryModel
{
public:
    explicit WorkSummaryQueryModel(QObject *parent = nullptr) : QSqlQueryModel(parent) {}

    int columnCount(const QModelIndex &parent = {}) const override
    {
        return QSqlQueryModel::columnCount(parent) + 1;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && section == 0)
            return role == Qt::DisplayRole ? QVariant(QStringLiteral("编辑")) : QVariant{};
        return orientation == Qt::Horizontal ? QSqlQueryModel::headerData(section - 1, orientation, role)
                                             : QSqlQueryModel::headerData(section, orientation, role);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() != 0)
        {
            const int sourceColumn = index.column() - 1;
            if (role == Qt::UserRole
                && sourceColumn == record().indexOf(QStringLiteral("completeness_bits")))
            {
                const int scoreColumn = record().indexOf(QStringLiteral("completeness_score"));
                return scoreColumn < 0 ? QVariant{}
                                       : QSqlQueryModel::data(this->index(index.row(), scoreColumn),
                                                               Qt::DisplayRole)
                                             .toInt();
            }
            return QSqlQueryModel::data(this->index(index.row(), sourceColumn), role);
        }
        if (role == Qt::UserRole)
        {
            const int workIdColumn = record().indexOf(QStringLiteral("work_id"));
            return workIdColumn < 0 ? QVariant{}
                                    : QSqlQueryModel::data(this->index(index.row(), workIdColumn), Qt::DisplayRole);
        }
        if (role == Qt::ToolTipRole)
        {
            const int serialColumn = record().indexOf(QStringLiteral("serial_number"));
            const QString serial = serialColumn < 0 ? QString{}
                : QSqlQueryModel::data(this->index(index.row(), serialColumn), Qt::DisplayRole).toString();
            return serial.isEmpty() ? QVariant{} : QVariant(QStringLiteral("编辑作品：%1").arg(serial));
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return index.column() == 0 ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                   : QSqlQueryModel::flags(this->index(index.row(), index.column() - 1));
    }
};

class SummaryProxyModel final : public QSortFilterProxyModel
{
public:
    explicit SummaryProxyModel(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setCompletenessColumns(int bitsColumn, int scoreColumn)
    {
        m_bitsColumn = bitsColumn;
        m_scoreColumn = scoreColumn;
    }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        if (left.column() == m_bitsColumn && m_scoreColumn >= 0)
        {
            const auto *model = sourceModel();
            const int leftScore = model->index(left.row(), m_scoreColumn).data().toInt();
            const int rightScore = model->index(right.row(), m_scoreColumn).data().toInt();
            if (leftScore != rightScore)
                return leftScore < rightScore;
            return left.data(Qt::DisplayRole).toString().toCaseFolded()
                < right.data(Qt::DisplayRole).toString().toCaseFolded();
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }

private:
    int m_bitsColumn = -1;
    int m_scoreColumn = -1;
};

class WorkSummaryEditDelegate final : public QStyledItemDelegate
{
public:
    explicit WorkSummaryEditDelegate(std::function<void(qint64)> openWork, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_openWork(std::move(openWork)) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &) const override
    {
        if (option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(option.rect, option.palette.highlight());
        QStyleOptionButton button;
        button.rect = option.rect.adjusted(4, 4, -4, -4);
        button.text = QStringLiteral("编辑");
        button.state = QStyle::State_Enabled;
        if (option.state.testFlag(QStyle::State_MouseOver))
            button.state |= QStyle::State_MouseOver;
        QApplication::style()->drawControl(QStyle::CE_PushButton, &button, painter);
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &,
                     const QModelIndex &index) override
    {
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
        {
            const qint64 workId = index.data(Qt::UserRole).toLongLong();
            if (workId > 0)
                m_openWork(workId);
            return true;
        }
        return event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove;
    }

private:
    std::function<void(qint64)> m_openWork;
};

QString sqlResourceForType(int type)
{
    if (type == 1)
        return QStringLiteral(":/sql/actress_all_info.sql");
    if (type == 2)
        return QStringLiteral(":/sql/actor_all_info.sql");
    return QStringLiteral(":/sql/work_all_info.sql");
}

QString readSqlResource(int type, QString *errorMessage)
{
    QFile file(sqlResourceForType(type));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        *errorMessage = file.errorString();
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
} // namespace

SummaryQueryWidget::SummaryQueryWidget(QSqlDatabase database, ThemeService &themes, QWidget *parent)
    : QWidget(parent), m_database(std::move(database)), m_themes(themes)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    m_model = new WorkSummaryQueryModel(this);
    m_filterModel = new SummaryProxyModel(this);
    m_filterModel->setSourceModel(m_model);
    m_table = new TokenTableView(this);
    m_table->setStyleSheet(QStringLiteral(
        "QTableView#DesignTableView {"
        " selection-background-color: transparent;"
        " selection-color: palette(text);"
        "}"));
    m_table->setModel(m_filterModel);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSectionsClickable(true);
    root->addWidget(m_table, 1);

    auto *searchLayout = new QHBoxLayout;
    m_search = new DesignLineEdit(this);
    m_search->installEventFilter(this);
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(200);
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    m_searchResult = new DesignLabel(QStringLiteral("无搜索结果"), this);
    m_searchResult->setFixedWidth(70);
    m_previousSearch = new IconButton(QStringLiteral("arrow_up"), &m_themes, this);
    m_nextSearch = new IconButton(QStringLiteral("arrow_down"), &m_themes, this);
    m_previousSearch->setAccessibleName(QStringLiteral("上一个搜索结果"));
    m_nextSearch->setAccessibleName(QStringLiteral("下一个搜索结果"));
    m_previousSearch->setWhatsThis(QStringLiteral("向前搜索"));
    m_nextSearch->setWhatsThis(QStringLiteral("向后搜索"));
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

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new DesignLabel(QStringLiteral("查询类型:"), this));
    m_queryType = new DesignComboBox(this);
    m_queryType->addItems({QStringLiteral("作品汇总"), QStringLiteral("女演员汇总"), QStringLiteral("男演员汇总")});
    toolbar->addWidget(m_queryType);
    toolbar->addStretch();
    auto *refreshButton = new DesignButton(QStringLiteral("刷新数据"), this);
    auto *exportButton = new DesignButton(QStringLiteral("导出为 CSV"), this);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(exportButton);
    root->addLayout(toolbar);

    m_completenessDelegate = new WorkCompletenessBitsDelegate(this);
    m_editDelegate = new WorkSummaryEditDelegate([this](qint64 workId) { emit workRequested(workId); }, this);
    connect(m_queryType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { loadQuery(); });
    connect(m_search, &QLineEdit::textChanged, this, &SummaryQueryWidget::performSearch);
    connect(m_previousSearch, &QPushButton::clicked, this, [this] { navigateSearch(-1); });
    connect(m_nextSearch, &QPushButton::clicked, this, [this] { navigateSearch(1); });
    connect(refreshButton, &QPushButton::clicked, this, &SummaryQueryWidget::refresh);
    connect(exportButton, &QPushButton::clicked, this, &SummaryQueryWidget::exportCsv);
    loadQuery();
}

void SummaryQueryWidget::refresh() { loadQuery(); }

bool SummaryQueryWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            if (keyEvent->modifiers() == Qt::ShiftModifier
                || keyEvent->modifiers() == Qt::NoModifier)
            {
                navigateSearch(keyEvent->modifiers() == Qt::ShiftModifier ? -1 : 1);
                keyEvent->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SummaryQueryWidget::clearCompletenessPresentation()
{
    m_table->setItemDelegateForColumn(0, nullptr);
    if (m_completenessBitsColumn >= 0)
        m_table->setItemDelegateForColumn(m_completenessBitsColumn, nullptr);
    if (m_completenessScoreColumn >= 0)
        m_table->showColumn(m_completenessScoreColumn);
    m_completenessBitsColumn = -1;
    m_completenessScoreColumn = -1;
}

void SummaryQueryWidget::loadQuery()
{
    QString errorMessage;
    const QString sql = readSqlResource(m_queryType->currentIndex(), &errorMessage);
    if (sql.isEmpty())
    {
        Toast::showError(window(), QStringLiteral("读取资料汇总失败：%1").arg(errorMessage), &m_themes);
        return;
    }
    clearCompletenessPresentation();
    m_model->setQuery(sql, m_database);
    if (m_model->lastError().isValid())
    {
        Toast::showError(window(), QStringLiteral("加载汇总数据失败: %1").arg(m_model->lastError().text()), &m_themes);
        return;
    }
    while (m_model->canFetchMore())
        m_model->fetchMore();
    m_table->setItemDelegateForColumn(0,
                                      m_queryType->currentIndex() == 0 ? m_editDelegate : nullptr);
    const int bitsColumn = m_model->record().indexOf(QStringLiteral("completeness_bits")) + 1;
    const int scoreColumn = m_model->record().indexOf(QStringLiteral("completeness_score")) + 1;
    static_cast<SummaryProxyModel *>(m_filterModel)->setCompletenessColumns(bitsColumn, scoreColumn);
    if (m_queryType->currentIndex() == 0 && bitsColumn > 0 && scoreColumn > 0)
    {
        m_completenessBitsColumn = bitsColumn;
        m_completenessScoreColumn = scoreColumn;
        m_table->setItemDelegateForColumn(bitsColumn, m_completenessDelegate);
        m_table->hideColumn(scoreColumn);
        auto *header = m_table->horizontalHeader();
        header->moveSection(header->visualIndex(bitsColumn), 3);
        m_table->resizeColumnToContents(bitsColumn);
    }
    m_table->sortByColumn(m_queryType->currentIndex() == 0 ? 2 : 1, Qt::AscendingOrder);
    performSearch(m_search->text());
}

void SummaryQueryWidget::performSearch(const QString &text)
{
    m_searchResults.clear();
    m_currentSearchResult = -1;
    const QString needle = text.trimmed();
    if (!needle.isEmpty())
    {
        for (int row = 0; row < m_filterModel->rowCount(); ++row)
            for (int column = 0; column < m_filterModel->columnCount(); ++column)
                if (m_filterModel->index(row, column).data().toString().contains(needle, Qt::CaseInsensitive))
                {
                    m_searchResults.append(m_filterModel->index(row, column));
                    break;
                }
    }
    const bool found = !m_searchResults.isEmpty();
    m_previousSearch->setEnabled(found);
    m_nextSearch->setEnabled(found);
    m_searchResult->setText(found ? QStringLiteral("找到 %1 个结果").arg(m_searchResults.size()) : QStringLiteral("无搜索结果"));
    if (found)
        navigateSearch(1);
}

void SummaryQueryWidget::navigateSearch(int offset)
{
    if (m_searchResults.isEmpty())
        return;
    m_currentSearchResult = (m_currentSearchResult + offset + m_searchResults.size()) % m_searchResults.size();
    const QModelIndex index = m_searchResults.at(m_currentSearchResult);
    m_table->selectRow(index.row());
    m_table->scrollTo(index, QAbstractItemView::PositionAtCenter);
    m_searchResult->setText(QStringLiteral("结果 %1/%2").arg(m_currentSearchResult + 1).arg(m_searchResults.size()));
}

void SummaryQueryWidget::exportCsv()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存为 CSV 文件"), {}, QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) path += QStringLiteral(".csv");
    QString errorMessage;
    if (!exportModelToCsv(m_filterModel, path, &errorMessage))
    {
        Toast::showError(window(), QStringLiteral("导出失败: %1").arg(errorMessage), &m_themes);
        return;
    }
    Toast::showSuccess(window(), QStringLiteral("已导出 CSV 文件"), &m_themes);
}
} // namespace darkeye
