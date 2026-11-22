#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include "database/repositories/PersonRepository.h"
#include "database/repositories/WorkRepository.h"

#include <QWidget>
#include <optional>
#include <utility>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace darkeye
{

class ThemeService;
class ImageDropWidget;
class WikiTextEdit;
namespace myads { class WorkspaceWidget; }

class PersonEditorPage : public LazyWidget
{
    Q_OBJECT

public:
    explicit PersonEditorPage(QSqlDatabase database, ThemeService &themes, QString imageDirectory,
                              QWidget *parent = nullptr, QString coverDirectory = {});
    ~PersonEditorPage() override;

    void enableLayoutPersistence(const QString &path);
    void setInitialPerson(PersonKind kind, qint64 personId);
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
    virtual void buildEditorActions() {}
    ThemeService &themeService() const noexcept { return m_themes; }
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
    void lazyLoad() override;
    void buildUi();
    void initializeLayoutPersistence();
    void restoreWorkspaceLayout();
    void scheduleWorkspaceSave();
    void saveWorkspaceLayout();
    void ensureKindFields(PersonKind kind);
    void populate(const PersonDetails &details);
    [[nodiscard]] PersonDetails editorDetails() const;
    void updateDirtyState();
    void resetDirtyState();
    void setDirtyStyle(QWidget *widget, bool dirty);
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
    std::optional<std::pair<PersonKind, qint64>> m_initialPerson;
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
    QVBoxLayout *m_baseLayout = nullptr;
    QWidget *m_kindFields = nullptr;
    std::optional<PersonKind> m_fieldsKind;
    myads::WorkspaceWidget *m_workspace = nullptr;
    QString m_workspaceLayoutPath;
    QTimer *m_workspaceSaveTimer = nullptr;
    QVBoxLayout *m_actionsLayout = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace darkeye
