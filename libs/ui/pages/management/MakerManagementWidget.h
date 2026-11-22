#pragma once

#include "database/repositories/ReferenceRepository.h"
#include "services/ReferenceJsonService.h"

#include <QWidget>
#include <QSet>

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;

namespace darkeye
{

class ThemeService;

class MakerManagementWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit MakerManagementWidget(QSqlDatabase database, ThemeService &themes,
                                  QWidget *parent = nullptr);

    void refresh();

signals:
    void referencesChanged(ReferenceKind kind);
    void prefixesChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class ActiveTable { Prefixes, Makers };

    void reloadTables(bool prefixes, bool makers);
    void refreshMakerChoices();
    void updateActiveTableIndicator();
    void performSearch(bool selectFirst = true);
    void navigateSearch(int direction);
    void scheduleSearchUpdate();
    void selectPrefix(int row);
    void selectMaker(int row);
    void beginNew();
    void storePrefixEdits();
    void storeMakerEdits();
    void saveCurrent();
    void removeCurrent();
    void redirectMaker();
    void importJson();
    void exportJson();
    [[nodiscard]] ReferenceRecord makerRecord() const;

    QSqlDatabase m_database;
    ReferenceRepository m_repository;
    ReferenceJsonService m_jsonService;
    ThemeService &m_themes;
    QList<MakerPrefixRecord> m_prefixes;
    QList<ReferenceRecord> m_makers;
    qint64 m_currentPrefixId = 0;
    qint64 m_currentMakerId = 0;
    ActiveTable m_activeTable = ActiveTable::Prefixes;
    QSet<qint64> m_removedPrefixes;
    QSet<qint64> m_removedMakers;
    QSet<int> m_dirtyPrefixes;
    QSet<int> m_dirtyMakers;
    bool m_loading = false;
    QTableWidget *m_prefixTable = nullptr;
    QTableWidget *m_makerTable = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_searchResult = nullptr;
    QPushButton *m_previousSearch = nullptr;
    QPushButton *m_nextSearch = nullptr;
    QList<int> m_searchRows;
    int m_searchIndex = -1;
    ActiveTable m_searchTable = ActiveTable::Prefixes;
    bool m_searchUpdatePending = false;
    QLineEdit *m_prefix = nullptr;
    QComboBox *m_prefixMaker = nullptr;
    QLineEdit *m_chineseName = nullptr;
    QLineEdit *m_japaneseName = nullptr;
    QLineEdit *m_aliases = nullptr;
    QPlainTextEdit *m_detail = nullptr;
};

} // namespace darkeye
