#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"

#include <optional>

namespace darkeye
{

class PersonInfoPanel;

class PersonDetailPage : public LazyWidget
{
    Q_OBJECT

public:
    explicit PersonDetailPage(QSqlDatabase publicDatabase,
                              QSqlDatabase privateDatabase, ThemeService &themes,
                              QString imageDirectory = {}, QWidget *parent = nullptr);

    bool showPerson(qint64 personId, bool reportError = true);
    [[nodiscard]] QWidget *captureContent();
    [[nodiscard]] qint64 currentPersonId() const noexcept;
    [[nodiscard]] QString currentPersonName() const;

signals:
    void editRequested(PersonKind kind, qint64 personId);
    void workRequested(qint64 workId);
    void favoriteChanged(qint64 personId, bool favorite);

private:
    void lazyLoad() override;
    void toggleFavorite(bool favorite);

    PersonRepository m_repository;
    PrivateRepository m_privateRepository;
    ThemeService &m_themes;
    QString m_imageDirectory;
    std::optional<PersonDetails> m_details;
    PersonInfoPanel *m_panel = nullptr;
};

} // namespace darkeye
