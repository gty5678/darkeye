#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"

#include <QSqlDatabase>

class QComboBox;
class QLabel;
class QTimer;

namespace darkeye
{

class CompleterLineEdit;
class LazyScrollArea;

class PersonPage : public LazyWidget
{
    Q_OBJECT

public:
    explicit PersonPage(PersonKind kind, QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                        ThemeService &themeService, QString imageDirectory = {},
                        QWidget *parent = nullptr);

    void refresh();
    [[nodiscard]] QWidget *captureContent();
    [[nodiscard]] PersonKind kind() const noexcept;
    [[nodiscard]] const QSet<qint64> &filteredPersonIds();

signals:
    void detailRequested(PersonKind kind, qint64 personId);
    void editRequested(PersonKind kind, qint64 personId);
    void filteredPeopleChanged();

private:
    void lazyLoad() override;
    void buildUi();
    void applyFilters();
    void clearFilters();
    void updateCount();
    [[nodiscard]] PersonSearch currentSearch() const;
    [[nodiscard]] QList<QWidget *> loadCardPage(int pageIndex, int pageSize);

    PersonKind m_kind;
    PersonRepository m_repository;
    PrivateRepository m_privateRepository;
    ThemeService &m_themeService;
    QString m_imageDirectory;
    CompleterLineEdit *m_nameInput = nullptr;
    QComboBox *m_cupSelector = nullptr;
    QComboBox *m_scopeSelector = nullptr;
    QComboBox *m_sortSelector = nullptr;
    QLabel *m_countLabel = nullptr;
    LazyScrollArea *m_lazyArea = nullptr;
    QTimer *m_filterTimer = nullptr;
    PersonSearch m_appliedSearch;
    std::optional<QSet<qint64>> m_filteredPersonIds;
    quint32 m_randomSeed = 1;
    quint32 m_randomSeed2 = 1;
};

} // namespace darkeye
