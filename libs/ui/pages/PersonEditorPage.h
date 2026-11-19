#pragma once

#include "database/repositories/PersonRepository.h"
#include "database/repositories/WorkRepository.h"

#include <QWidget>
#include <optional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QVBoxLayout;

namespace darkeye
{

class ThemeService;
class ImageDropWidget;
class WikiTextEdit;
namespace myads { class PaneWidget; class WorkspaceWidget; }

class PersonEditorPage : public QWidget
{
    Q_OBJECT

public:
    explicit PersonEditorPage(QSqlDatabase database, ThemeService &themes, QString imageDirectory,
                              QWidget *parent = nullptr, QString coverDirectory = {});

    bool loadPerson(PersonKind kind, qint64 personId);
    bool savePerson();
    [[nodiscard]] qint64 personId() const noexcept;

signals:
    void personSaved(PersonKind kind, qint64 personId);
    void workLinkRequested(qint64 workId);
    void actressLinkRequested(qint64 actressId);
    void namesChanged();
    void personViewRequested(PersonKind kind, qint64 personId);
    void personDeleted(PersonKind kind, qint64 personId);
    void closeRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void addActressExternalLinksPanel(QWidget *panel);
    void configureAvatar(const QString &purpose, const QString &placeholder);
    void setAvatarImagePath(const QString &path);
    void applyActressCaptureFields(const QJsonObject &capture);
    void addActionButton(QWidget *widget);
    void setCancelButtonVisible(bool visible);
    bool deleteCurrentPerson(QString *errorMessage = nullptr);
    [[nodiscard]] QString primaryJapaneseName() const;
    [[nodiscard]] QString primaryChineseName() const;
    [[nodiscard]] QList<QString> japaneseNames() const;
    [[nodiscard]] QList<QString> chineseNames() const;
    void replaceChineseNames(const QList<QString> &translations, bool overwrite);

private:
    void buildUi();
    void populate(const PersonDetails &details);
    [[nodiscard]] PersonDetails editorDetails() const;
    void updateDirtyState();
    void resetDirtyState();
    void setDirtyStyle(QWidget *widget, bool dirty, const QString &selector);
    void addNameRow(const PersonName &name = {});
    void removeSelectedNameRows();
    void moveSelectedNameRow(int offset);
    static std::optional<int> optionalSpinValue(const QSpinBox *spinBox);
    static bool sameNames(const QList<PersonName> &left, const QList<PersonName> &right);

    PersonRepository m_repository;
    WorkRepository m_works;
    ThemeService &m_themes;
    QString m_imageDirectory;
    QString m_coverDirectory;
    std::optional<PersonDetails> m_original;
    bool m_populating = false;
    QTableWidget *m_names = nullptr;
    ImageDropWidget *m_imageDrop = nullptr;
    QLineEdit *m_birthday = nullptr;
    QSpinBox *m_height = nullptr;
    QSpinBox *m_bust = nullptr;
    QSpinBox *m_waist = nullptr;
    QSpinBox *m_hip = nullptr;
    QComboBox *m_cup = nullptr;
    QLineEdit *m_debutDate = nullptr;
    QComboBox *m_handsome = nullptr;
    QComboBox *m_fat = nullptr;
    QCheckBox *m_needUpdate = nullptr;
    QLineEdit *m_minnanoUrl = nullptr;
    WikiTextEdit *m_notes = nullptr;
    QWidget *m_actressFields = nullptr;
    QWidget *m_actorFields = nullptr;
    myads::WorkspaceWidget *m_workspace = nullptr;
    myads::PaneWidget *m_notesPane = nullptr;
    QVBoxLayout *m_actionsLayout = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace darkeye
