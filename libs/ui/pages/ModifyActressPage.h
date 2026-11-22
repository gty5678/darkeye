#pragma once

#include "ui/pages/PersonEditorPage.h"

#include <QJsonObject>

namespace darkeye
{

class ActressSyncService;
class LlmTranslationService;
class ActressNavPage;
class ImageFetchService;

class ModifyActressPage final : public PersonEditorPage
{
    Q_OBJECT

public:
    explicit ModifyActressPage(QSqlDatabase database, ThemeService &themes,
                               QString imageDirectory, QWidget *parent = nullptr,
                               QString coverDirectory = {}, QString navConfigFile = {});

    bool loadActress(qint64 actressId);
    /// Applies a browser/Collector capture to the open form without saving it.
    bool applyCapture(const QJsonObject &payload, QString *errorMessage = nullptr);

private:
    void translateChineseNames(bool overwrite);
    void translateNextName();
    void downloadCapturedAvatar(const QJsonObject &capture);

    ActressSyncService *m_sync = nullptr;
    LlmTranslationService *m_translation = nullptr;
    ActressNavPage *m_navPage = nullptr;
    ImageFetchService *m_avatarFetch = nullptr;
    quint64 m_avatarFetchRequestId = 0;
    qint64 m_avatarFetchPersonId = 0;
    QList<QString> m_nameTranslations;
    QList<int> m_translationRows;
    int m_translationCursor = 0;
    int m_successfulNameTranslations = 0;
    quint64 m_translationRequestId = 0;
    bool m_translationOverwrite = false;
    ::QPushButton *m_translateMissingButton = nullptr;
    ::QPushButton *m_translateOverwriteButton = nullptr;
};

} // namespace darkeye
