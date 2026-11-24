#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "domain/Work.h"

#include <QSqlDatabase>

class QComboBox;
class QLabel;
class QTimer;

namespace darkeye
{
class CompleterLineEdit;
class DesignLineEdit;
class MakerSelector;
class ThemeService;
class TokenCollapsibleSection;
class WorkTagSelector;

class WorkFilter final : public LazyWidget
{
    Q_OBJECT

public:
    enum class Reference { Maker, Label, Series };
    enum class Scope { Public, Favorites, UnwatchedFavorites, Watched, LocalVideo };

    struct SearchRequest
    {
        WorkSearch filters;
        Scope scope;
        QString privateDatabasePath;

        [[nodiscard]] WorkSearch resolve(QString *error = nullptr) const;
    };

    WorkFilter(QSqlDatabase database, QSqlDatabase privateDatabase,
               ThemeService &themeService, QWidget *parent = nullptr, QString settingsFile = {});

    [[nodiscard]] WorkSearch search() const;
    [[nodiscard]] SearchRequest searchRequest() const;
    void focusNotes();
    bool flush();
    void updateCount(int total);
    void refreshOptions(bool tagsChanged, bool referencesChanged);
    void filterByReference(Reference reference, qint64 id);
    void filterByDirector(const QString &director);
    void filterByActor(qint64 id, const QString &name);
    void filterByTag(qint64 id);
    void resetToPublicLibrary();

signals:
    void changed();

protected:

private:
    void lazyLoad() override;
    void buildUi();
    void applyFilters();
    void clearFilters();
    void clearInputs();
    void selectRouteTag();
    void applyInitialDefaultTagFilter(const QList<TagOption> &tags);
    void loadReferenceOptionsAsync();
    void reloadReferenceOptions();
    [[nodiscard]] WorkSortOrder selectedSortOrder() const;

    ThemeService &m_themeService;
    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    QString m_settingsFile;
    std::optional<qint64> m_actorId;
    qint64 m_routeTagId = 0;
    DesignLineEdit *m_searchInput = nullptr;
    CompleterLineEdit *m_serialFilter = nullptr;
    DesignLineEdit *m_titleFilter = nullptr;
    DesignLineEdit *m_storyFilter = nullptr;
    CompleterLineEdit *m_directorFilter = nullptr;
    CompleterLineEdit *m_actressFilter = nullptr;
    CompleterLineEdit *m_actorFilter = nullptr;
    MakerSelector *m_makerFilter = nullptr;
    MakerSelector *m_labelFilter = nullptr;
    MakerSelector *m_seriesFilter = nullptr;
    TokenCollapsibleSection *m_moreFilters = nullptr;
    WorkTagSelector *m_tagSelector = nullptr;
    QComboBox *m_sortSelector = nullptr;
    QComboBox *m_scopeSelector = nullptr;
    QTimer *m_filterTimer = nullptr;
    quint32 m_randomSeed = 1;
    quint32 m_randomSeed2 = 1;
    QLabel *m_countLabel = nullptr;
};

} // namespace darkeye
