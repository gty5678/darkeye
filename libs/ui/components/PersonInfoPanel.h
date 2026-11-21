#pragma once

#include "domain/Person.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QEvent;

namespace darkeye
{

class HeartLabel;
class IconButton;
class OctImage;
class ThemeService;
class RadarChartWidget;
class TokenCollapsibleSection;

class PersonInfoPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit PersonInfoPanel(ThemeService &themes, QString imageDirectory = {},
                             QWidget *parent = nullptr);

    void setDetails(const PersonDetails &details, bool favorite = false);
    [[nodiscard]] qint64 personId() const noexcept;
    [[nodiscard]] PersonKind kind() const noexcept;

signals:
    void aliasesToggled(bool expanded);
    void favoriteChanged(bool favorite);
    void editRequested(qint64 personId);
    void workRequested(qint64 workId);
    void actressExternalSearchRequested(const QString &name);

private:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void rebuildNames(const PersonDetails &details);
    void rebuildActressAliases(const QList<PersonName> &names);
    void rebuildFacts(const PersonDetails &details);
    void rebuildWorks(const PersonDetails &details);
    void rebuildRadar(const PersonDetails &details);

    ThemeService &m_themes;
    QString m_imageDirectory;
    PersonKind m_kind = PersonKind::Actress;
    qint64 m_personId = 0;
    OctImage *m_avatar = nullptr;
    QWidget *m_names = nullptr;
    QWidget *m_facts = nullptr;
    HeartLabel *m_heart = nullptr;
    IconButton *m_edit = nullptr;
    QLabel *m_notes = nullptr;
    TokenCollapsibleSection *m_actressAliases = nullptr;
    QWidget *m_aliasesGroup = nullptr;
    QWidget *m_worksGroup = nullptr;
    QTableWidget *m_aliases = nullptr;
    QTableWidget *m_works = nullptr;
    RadarChartWidget *m_radar = nullptr;
};

} // namespace darkeye
