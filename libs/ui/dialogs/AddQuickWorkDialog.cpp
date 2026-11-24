#include "ui/dialogs/AddQuickWorkDialog.h"

#include "crawler/CrawlerScheduler.h"
#include "domain/SerialNumber.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/theme/IconProvider.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "ui/components/CrawlerFieldSelector.h"
#include "utils/MediaUtils.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QPair>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace darkeye
{

namespace
{

struct WorkCrawlSnapshot final
{
    QString serial;
    QVariantMap values;
};

QList<WorkCrawlSnapshot> loadWorkCrawlSnapshots(const QSqlDatabase &database,
                                                 QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(R"(
SELECT w.serial_number, w.release_date, w.director, w.runtime, w.cn_title, w.jp_title,
       w.cn_story, w.jp_story, w.image_url, w.maker_id, w.label_id, w.series_id, w.fanart,
       (SELECT COUNT(1) FROM work_actress_relation war WHERE war.work_id=w.work_id),
       (SELECT COUNT(1) FROM work_actor_relation wor WHERE wor.work_id=w.work_id),
       (SELECT COUNT(1) FROM work_tag_relation wtr WHERE wtr.work_id=w.work_id)
FROM work w
WHERE IFNULL(w.is_deleted, 0)=0
)")))
    {
        if (errorMessage != nullptr) *errorMessage = query.lastError().text();
        return {};
    }

    QList<WorkCrawlSnapshot> snapshots;
    while (query.next())
    {
        WorkCrawlSnapshot snapshot;
        snapshot.serial = query.value(0).toString().trimmed().toUpper();
        if (snapshot.serial.isEmpty()) continue;
        const QStringList names = {QStringLiteral("release_date"), QStringLiteral("director"),
                                   QStringLiteral("runtime"), QStringLiteral("cn_title"),
                                   QStringLiteral("jp_title"), QStringLiteral("cn_story"),
                                   QStringLiteral("jp_story"), QStringLiteral("image_url"),
                                   QStringLiteral("maker_id"), QStringLiteral("label_id"),
                                   QStringLiteral("series_id"), QStringLiteral("fanart"),
                                   QStringLiteral("actress_count"), QStringLiteral("actor_count"),
                                   QStringLiteral("tag_count")};
        for (qsizetype index = 0; index < names.size(); ++index)
            snapshot.values.insert(names.at(index), query.value(index + 1));
        snapshots.append(std::move(snapshot));
    }
    return snapshots;
}

bool isEmptyForCrawlField(const QVariantMap &values, const QString &field)
{
    if (field == QStringLiteral("actress") || field == QStringLiteral("actor") || field == QStringLiteral("tag"))
        return values.value(field + QStringLiteral("_count")).toInt() <= 0;
    if (field == QStringLiteral("maker") || field == QStringLiteral("label") || field == QStringLiteral("series"))
        return values.value(field + QStringLiteral("_id")).isNull();
    if (field == QStringLiteral("cover"))
        return values.value(QStringLiteral("image_url")).toString().trimmed().isEmpty();
    if (field == QStringLiteral("runtime"))
    {
        const QVariant value = values.value(field);
        return value.isNull() || value.toInt() <= 0;
    }
    if (field == QStringLiteral("fanart"))
    {
        const QString raw = values.value(field).toString().trimmed();
        if (raw.isEmpty() || raw == QStringLiteral("[]")) return true;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().isEmpty()) return true;
        for (const QJsonValue &entry : document.array())
        {
            if (!entry.isObject()) continue;
            const QJsonObject object = entry.toObject();
            if (!object.value(QStringLiteral("url")).toString().trimmed().isEmpty()
                || !object.value(QStringLiteral("file")).toString().trimmed().isEmpty()) return false;
        }
        return true;
    }
    return values.value(field).toString().trimmed().isEmpty();
}

// Mirrors MessageBoxService._messagebox_qss_from_tokens in the Python app.
void showThemedInformation(QWidget *parent, ThemeService &themes, const QString &title,
                           const QString &message)
{
    const ThemeTokens tokens = themes.currentTokens();
    QMessageBox box(QMessageBox::Information, title, message, QMessageBox::Ok, parent);
    box.setStyleSheet(QStringLiteral(R"(
QMessageBox { background-color: %1; color: %2; font-family: "%3"; font-size: %4; }
QMessageBox QLabel { color: %2; }
QMessageBox QPushButton {
    background-color: %5; color: %6; border: none; border-radius: 4px;
    padding: 6px 16px; min-width: 70px;
}
QMessageBox QPushButton:hover { background-color: %7; color: %9; }
QMessageBox QPushButton:disabled { background-color: %8; color: %2; }
)")
                          .arg(tokens.background, tokens.text, tokens.fontFamilyBase,
                               tokens.fontSizeBase, tokens.primary, tokens.textInverse,
                               tokens.primaryHover, tokens.textDisabled,
                               tokens.textOnPrimaryHover.isEmpty() ? tokens.textInverse
                                                                   : tokens.textOnPrimaryHover));
    box.exec();
}

// Kept local to this dialog because the drop target's only responsibility is
// collecting paths.  The dialog owns the table update and user-facing report.
class QuickWorkVideoDropZone final : public QFrame
{
public:
    explicit QuickWorkVideoDropZone(ThemeService &themes,
                                    std::function<void(QStringList, QStringList)> dropped,
                                    QWidget *parent = nullptr)
        : QFrame(parent), m_themes(themes), m_dropped(std::move(dropped))
    {
        setAcceptDrops(true);
        setMinimumHeight(132);
        setObjectName(QStringLiteral("quickWorkVideoDropZone"));
        applyTokenStyles();
        connect(&m_themes, &ThemeService::themeChanged, this,
                [this] { applyTokenStyles(); });

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        auto *hint = new QLabel(QStringLiteral("拖入视频或文件夹，识别番号并加入列表"), this);
        hint->setObjectName(QStringLiteral("quickWorkVideoDropZoneHint"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        layout->addStretch();
        layout->addWidget(hint);
        layout->addStretch();
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasUrls()) return;

        QStringList serials;
        QStringList failedPaths;
        const QStringList extensions = utils::defaultVideoExtensions();
        for (const QUrl &url : event->mimeData()->urls()) {
            const QString localPath = url.toLocalFile();
            if (localPath.isEmpty()) continue;
            const QFileInfo path(localPath);
            if (path.isDir()) {
                const utils::VideoScanResult scan = utils::videoNamesFromPaths({path.absoluteFilePath()});
                serials.append(scan.serials);
                for (const auto &[name, filePath] : scan.filesWithoutSerial) {
                    Q_UNUSED(name);
                    failedPaths.append(filePath);
                }
            } else if (!path.isFile() || !extensions.contains(QStringLiteral(".") + path.suffix().toLower())) {
                failedPaths.append(path.absoluteFilePath());
            } else if (const auto serial = serial::extract(path.completeBaseName()); serial.has_value()) {
                serials.append(*serial);
            } else {
                failedPaths.append(path.absoluteFilePath());
            }
        }
        event->acceptProposedAction();
        m_dropped(std::move(serials), std::move(failedPaths));
    }

private:
    void applyTokenStyles()
    {
        const ThemeTokens tokens = m_themes.currentTokens();
        setStyleSheet(QStringLiteral(
            "#quickWorkVideoDropZone { border: 2px dashed %1; border-radius: 10px; "
            "background-color: %2; }"
            "#quickWorkVideoDropZoneHint { background-color: transparent; "
            "color: %3; font-family: \"%4\"; font-size: %5; }")
                          .arg(tokens.border, tokens.inputBackground, tokens.textPlaceholder,
                               tokens.fontFamilyBase, tokens.fontSizeBase));
    }

    ThemeService &m_themes;
    std::function<void(QStringList, QStringList)> m_dropped;
};

} // namespace

AddQuickWorkDialog::AddQuickWorkDialog(QSqlDatabase publicDatabase,
                                       CrawlerScheduler &crawlerScheduler, ThemeService &themes,
                                       QWidget *parent)
    : QDialog(parent), m_publicDatabase(std::move(publicDatabase)),
      m_crawlerScheduler(crawlerScheduler), m_themes(themes)
{
    setObjectName(QStringLiteral("AddQuickWorkDialog"));
    setWindowTitle(QStringLiteral("批量录入与补全作品"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("film")));
    setMinimumSize(780, 520);
    resize(800, 540);

    auto *add = new IconButton(QStringLiteral("list_plus"), QStringLiteral("添加番号行"), &m_themes, this);
    auto *remove = new IconButton(QStringLiteral("list_x"), QStringLiteral("删除选中行"), &m_themes, this);
    auto *removeAll = new IconButton(QStringLiteral("trash_2"), QStringLiteral("删除全部"), &m_themes, this);
    auto *clean = new IconButton(QStringLiteral("eraser"), QStringLiteral("去后缀"), &m_themes, this);
    auto *cleanPrefix = new DesignButton(QStringLiteral("按前缀删除行"), this);
    auto *sort = new DesignButton(QStringLiteral("排序"), this);
    sort->setToolTip(QStringLiteral("按番号升序排序"));
    auto *csv = new DesignButton(QStringLiteral("从 CSV 导入"), this);
    auto *toolbar = new QHBoxLayout;
    QPushButton *const buttons[] = {add, remove, removeAll, clean, cleanPrefix, sort, csv};
    for (auto *button : buttons)
        toolbar->addWidget(button);
    toolbar->addStretch();

    m_table = new TokenTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("选择"), QStringLiteral("番号")});
    auto *selectionHeader = m_table->horizontalHeaderItem(0);
    selectionHeader->setData(Qt::AccessibleTextRole, selectionHeader->text());
    selectionHeader->setToolTip(selectionHeader->text());
    selectionHeader->setText(QString());
    const auto updateHeaderIcon = [this, selectionHeader] {
        selectionHeader->setIcon(IconProvider::builtIn(QStringLiteral("square_check"), QSize(16, 16),
                                                        QColor(m_themes.currentTokens().icon)));
    };
    updateHeaderIcon();
    connect(&m_themes, &ThemeService::themeChanged, this, updateHeaderIcon);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_commitButton = new DesignButton(QString(), this);
    m_commitButton->setMinimumHeight(40);
    m_commitButton->setIconSize(QSize(24, 24));
    updateCommitButtonAppearance();
    connect(&m_themes, &ThemeService::themeChanged, this,
            &AddQuickWorkDialog::updateCommitButtonAppearance);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addLayout(toolbar);
    leftLayout->addWidget(m_table);
    leftLayout->addWidget(m_commitButton);

    m_fields = new CrawlerFieldSelector(this);
    auto *filter = new IconButton(QStringLiteral("funnel"), QStringLiteral("按空字段筛选"), &m_themes, this);
    filter->setToolTip(QStringLiteral("按右侧勾选字段查询全库，并用结果覆盖左侧列表。提交时仅补充各作品实际缺失的字段。"));
    auto *hint = new QLabel(QStringLiteral("未筛选时提交：对左侧番号全量爬取。\n"
                                           "勾选字段后点击筛选：全库结果覆盖左侧；提交时只补充每部作品实际缺失的勾选字段。"), this);
    hint->setWordWrap(true);
    auto *dropZone = new QuickWorkVideoDropZone(
        m_themes, [this](QStringList serials, QStringList failedPaths) {
            appendDroppedSerials(serials, failedPaths);
        }, this);
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(hint);
    rightLayout->addWidget(filter, 0, Qt::AlignLeft);
    rightLayout->addWidget(m_fields);
    rightLayout->addStretch();
    rightLayout->addWidget(dropZone);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(left, 3);
    layout->addWidget(right, 2);

    connect(add, &QPushButton::clicked, this, [this] { addRow(); });
    connect(remove, &QPushButton::clicked, this, &AddQuickWorkDialog::deleteSelectedRows);
    connect(removeAll, &QPushButton::clicked, m_table, [this] { m_table->setRowCount(0); });
    connect(clean, &QPushButton::clicked, this, &AddQuickWorkDialog::cleanSuffixes);
    connect(cleanPrefix, &QPushButton::clicked, this, &AddQuickWorkDialog::deletePrefixRows);
    connect(sort, &QPushButton::clicked, this, [this, sort] {
        sortRows();
        const QString label = m_sortAscending ? QStringLiteral("按番号升序排序")
                                              : QStringLiteral("按番号降序排序");
        sort->setToolTip(label);
        sort->setAccessibleName(label);
    });
    connect(csv, &QPushButton::clicked, this, &AddQuickWorkDialog::importCsv);
    connect(filter, &QPushButton::clicked, this, &AddQuickWorkDialog::applyEmptyFieldFilter);
    connect(m_commitButton, &QPushButton::clicked, this, &AddQuickWorkDialog::submit);
    addRow();
}

void AddQuickWorkDialog::loadSerials(const QStringList &serials)
{
    m_selectiveCrawl = false;
    updateCommitButtonAppearance();
    m_table->setRowCount(0);
    QSet<QString> seen;
    for (const QString &value : serials)
    {
        const QString serial = value.trimmed().toUpper();
        if (serial.isEmpty() || seen.contains(serial)) continue;
        seen.insert(serial);
        addRow(serial);
    }
}

void AddQuickWorkDialog::updateCommitButtonAppearance()
{
    const QString icon = m_selectiveCrawl ? QStringLiteral("refresh") : QStringLiteral("check");
    m_commitButton->setIcon(IconProvider::builtIn(icon, m_commitButton->iconSize(),
                                                 QColor(m_themes.currentTokens().icon)));
    if (m_selectiveCrawl)
    {
        m_commitButton->setAccessibleName(QStringLiteral("补全缺失字段"));
        m_commitButton->setToolTip(QStringLiteral("补全缺失字段：仅采集每部作品实际缺失的勾选字段。"));
    }
    else
    {
        m_commitButton->setAccessibleName(QStringLiteral("提交采集"));
        m_commitButton->setToolTip(QStringLiteral("提交采集：采集左侧勾选番号的完整作品信息并写入资料库。"));
    }
}

void AddQuickWorkDialog::applyEmptyFieldFilter()
{
    const QSet<QString> selectedFields = m_fields->selectedFields();
    if (selectedFields.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先勾选至少一个字段"));
        return;
    }

    QString error;
    const QList<WorkCrawlSnapshot> snapshots = loadWorkCrawlSnapshots(m_publicDatabase, &error);
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("筛选失败"), error);
        return;
    }

    QStringList serials;
    for (const WorkCrawlSnapshot &snapshot : snapshots)
    {
        for (const QString &field : selectedFields)
        {
            if (isEmptyForCrawlField(snapshot.values, field))
            {
                serials.append(snapshot.serial);
                break;
            }
        }
    }

    loadSerials(serials);
    m_selectiveCrawl = !serials.isEmpty();
    updateCommitButtonAppearance();
    if (m_selectiveCrawl)
    {
        QMessageBox::information(this, QStringLiteral("筛选完成"),
                                 QStringLiteral("已从全库筛出 %1 条番号填入左侧。点击列表下方的图标即可补全缺失字段。").arg(serials.size()));
    }
    else
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("没有匹配到需要更新的作品，左侧已清空。"));
    }
}

void AddQuickWorkDialog::addRow(const QString &serial)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *checked = new QTableWidgetItem;
    checked->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checked->setCheckState(Qt::Checked);
    m_table->setItem(row, 0, checked);
    auto *text = new QTableWidgetItem(serial);
    m_table->setItem(row, 1, text);
    if (serial.isEmpty())
    {
        m_table->setCurrentItem(text);
        m_table->editItem(text);
    }
}

QStringList AddQuickWorkDialog::checkedSerials() const
{
    QStringList serials;
    QSet<QString> seen;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const auto *checked = m_table->item(row, 0);
        const auto *text = m_table->item(row, 1);
        const QString serial = text == nullptr ? QString() : text->text().trimmed().toUpper();
        if (checked != nullptr && checked->checkState() == Qt::Checked && !serial.isEmpty() && !seen.contains(serial))
        {
            seen.insert(serial);
            serials.append(serial);
        }
    }
    return serials;
}

void AddQuickWorkDialog::deleteSelectedRows()
{
    QList<int> rows;
    for (const QTableWidgetSelectionRange &range : m_table->selectedRanges())
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) rows.append(row);
    std::sort(rows.begin(), rows.end(), std::greater<>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int row : rows) m_table->removeRow(row);
}

void AddQuickWorkDialog::cleanSuffixes()
{
    const QRegularExpression suffix(QStringLiteral("(-C|-h|_uncensored|ch|pl)$"),
                                    QRegularExpression::CaseInsensitiveOption);
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (auto *checked = m_table->item(row, 0); checked != nullptr && checked->checkState() == Qt::Checked)
            if (auto *text = m_table->item(row, 1); text != nullptr) text->setText(text->text().trimmed().remove(suffix));
}

void AddQuickWorkDialog::deletePrefixRows()
{
    bool accepted = false;
    const QString prefix = QInputDialog::getText(this, QStringLiteral("删前缀行"),
        QStringLiteral("输入前缀（删除已勾选且以此前缀开头的番号行）："), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || prefix.isEmpty()) return;
    for (int row = m_table->rowCount() - 1; row >= 0; --row)
    {
        const auto *checked = m_table->item(row, 0);
        const auto *text = m_table->item(row, 1);
        if (checked != nullptr && text != nullptr && checked->checkState() == Qt::Checked && text->text().startsWith(prefix, Qt::CaseInsensitive))
            m_table->removeRow(row);
    }
}

void AddQuickWorkDialog::sortRows()
{
    struct Row { Qt::CheckState checked; QString serial; };
    QList<Row> rows;
    for (int row = 0; row < m_table->rowCount(); ++row)
        rows.append({m_table->item(row, 0)->checkState(), m_table->item(row, 1)->text()});
    std::sort(rows.begin(), rows.end(), [this](const Row &a, const Row &b) {
        return m_sortAscending ? a.serial.compare(b.serial, Qt::CaseInsensitive) < 0 : a.serial.compare(b.serial, Qt::CaseInsensitive) > 0;
    });
    m_sortAscending = !m_sortAscending;
    m_table->setRowCount(0);
    for (const Row &row : rows) { addRow(row.serial); m_table->item(m_table->rowCount() - 1, 0)->setCheckState(row.checked); }
}

void AddQuickWorkDialog::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 CSV 文件"), {}, QStringLiteral("CSV (*.csv);;All Files (*.*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString()); return; }
    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QString serial = stream.readLine().section(',', 0, 0).trimmed();
        if (!serial.isEmpty()) addRow(serial);
    }
}

void AddQuickWorkDialog::appendDroppedSerials(const QStringList &serials,
                                              const QStringList &failedPaths)
{
    QSet<QString> existing;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (const auto *item = m_table->item(row, 1); item != nullptr)
            existing.insert(item->text().trimmed().toUpper());
    }

    int added = 0;
    int skipped = 0;
    for (const QString &value : serials) {
        const QString serial = value.trimmed().toUpper();
        if (serial.isEmpty()) continue;
        if (existing.contains(serial)) {
            ++skipped;
            continue;
        }
        existing.insert(serial);
        addRow(serial);
        ++added;
    }

    if (added == 0 && skipped == 0 && failedPaths.isEmpty()) {
        showThemedInformation(this, m_themes, QStringLiteral("提示"),
                              QStringLiteral("未检测到可处理的拖入项目"));
        return;
    }
    QStringList parts;
    if (added > 0) parts.append(QStringLiteral("已添加 %1 条番号").arg(added));
    if (skipped > 0) parts.append(QStringLiteral("跳过 %1 条重复番号").arg(skipped));
    if (!failedPaths.isEmpty()) parts.append(QStringLiteral("%1 个文件未能识别番号").arg(failedPaths.size()));
    if (parts.isEmpty()) parts.append(QStringLiteral("没有新增番号"));

    QString message = parts.join(QStringLiteral("；"));
    if (!failedPaths.isEmpty()) {
        message += QStringLiteral("\n\n未识别文件：\n") + failedPaths.mid(0, 5).join(QLatin1Char('\n'));
        if (failedPaths.size() > 5) message += QStringLiteral("\n...");
    }
    showThemedInformation(this, m_themes, QStringLiteral("拖入完成"), message);
}

void AddQuickWorkDialog::submit()
{
    const QStringList serials = checkedSerials();
    if (serials.isEmpty()) { QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("没有选中任何有效的番号")); return; }
    const QSet<QString> fields = m_selectiveCrawl ? m_fields->selectedFields() : QSet<QString>{};
    if (m_selectiveCrawl && fields.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先勾选至少一个要爬取的字段")); return; }
    if (!m_selectiveCrawl)
    {
        m_crawlerScheduler.enqueue(serials);
    }
    else
    {
        QString error;
        const QList<WorkCrawlSnapshot> snapshots = loadWorkCrawlSnapshots(m_publicDatabase, &error);
        if (!error.isEmpty()) { QMessageBox::warning(this, QStringLiteral("提交失败"), error); return; }
        QHash<QString, QVariantMap> works;
        for (const WorkCrawlSnapshot &snapshot : snapshots) works.insert(snapshot.serial, snapshot.values);
        QList<QPair<QSet<QString>, QStringList>> batches;
        for (const QString &serial : serials)
        {
            QSet<QString> missing;
            const QVariantMap values = works.value(serial);
            for (const QString &field : fields)
                if (values.isEmpty() || isEmptyForCrawlField(values, field)) missing.insert(field);
            if (missing.isEmpty()) continue;
            auto batch = std::find_if(batches.begin(), batches.end(), [&missing](const auto &candidate) {
                return candidate.first == missing;
            });
            if (batch == batches.end()) batches.append({missing, {serial}});
            else batch->second.append(serial);
        }
        for (const auto &batch : batches)
            m_crawlerScheduler.enqueue(batch.second, false, batch.first);
    }
    showThemedInformation(this, m_themes, QStringLiteral("提示"),
                          QStringLiteral("已转入后台处理，您可以继续其他操作。"));
    accept();
}

} // namespace darkeye
