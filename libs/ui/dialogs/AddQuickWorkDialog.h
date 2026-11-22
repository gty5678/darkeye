#pragma once

#include <QDialog>
#include <QSqlDatabase>
#include <QStringList>

class QTableWidget;
class QPushButton;

namespace darkeye
{

class CrawlerFieldSelector;
class CrawlerScheduler;
class ThemeService;

// Batch-oriented counterpart of Python's AddQuickWork dialog.  Keeping this
// separate from the work editor makes the global shortcut useful without
// unexpectedly navigating away from the current page.
class AddQuickWorkDialog final : public QDialog
{
public:
    explicit AddQuickWorkDialog(QSqlDatabase publicDatabase, CrawlerScheduler &crawlerScheduler, ThemeService &themes,
                                QWidget *parent = nullptr);

    /// Replaces the editable list with serials discovered elsewhere in the app.
    /// Entries are normalized and selected so callers can immediately submit them.
    void loadSerials(const QStringList &serials);

private:
    void addRow(const QString &serial = {});
    void deleteSelectedRows();
    void cleanSuffixes();
    void deletePrefixRows();
    void sortRows();
    void importCsv();
    void appendDroppedSerials(const QStringList &serials, const QStringList &failedPaths);
    void applyEmptyFieldFilter();
    void updateCommitButtonAppearance();
    void submit();
    [[nodiscard]] QStringList checkedSerials() const;

    QSqlDatabase m_publicDatabase;
    CrawlerScheduler &m_crawlerScheduler;
    ThemeService &m_themes;
    QTableWidget *m_table = nullptr;
    QPushButton *m_commitButton = nullptr;
    CrawlerFieldSelector *m_fields = nullptr;
    bool m_sortAscending = true;
    bool m_selectiveCrawl = false;
};

} // namespace darkeye
