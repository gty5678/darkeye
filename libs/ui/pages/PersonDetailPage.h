#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"

#include <optional>

namespace darkeye
{

class PersonInfoPanel;
class ActressWorkTimeline;
class ForceDirectPage;
namespace graph { class GraphManager; }

class PersonDetailPage : public LazyWidget
{
    Q_OBJECT

public:
    explicit PersonDetailPage(PersonKind kind, QSqlDatabase publicDatabase,
                              QSqlDatabase privateDatabase, ThemeService &themes,
                              QString imageDirectory = {}, QWidget *parent = nullptr,
                              QString coverDirectory = {}, graph::GraphManager *graphManager = nullptr);

    bool showPerson(qint64 personId);
    [[nodiscard]] QWidget *captureContent();
    [[nodiscard]] qint64 currentPersonId() const noexcept;
    [[nodiscard]] PersonKind kind() const noexcept;

signals:
    void editRequested(PersonKind kind, qint64 personId);
    void workRequested(qint64 workId);
    void favoriteChanged(qint64 personId, bool favorite);

private:
    void lazyLoad() override;
    void toggleFavorite(bool favorite);

    PersonKind m_kind;
    PersonRepository m_repository;
    PrivateRepository m_privateRepository;
    ThemeService &m_themes;
    QString m_imageDirectory;
    QString m_coverDirectory;
    std::optional<PersonDetails> m_details;
    PersonInfoPanel *m_panel = nullptr;
    ActressWorkTimeline *m_timeline = nullptr;
    ForceDirectPage *m_relationGraph = nullptr;
};

} // namespace darkeye
