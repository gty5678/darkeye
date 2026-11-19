#pragma once

#include "services/BatchTranslationService.h"
#include "services/WorkMaintenanceService.h"

#include <QObject>
#include <QList>

class QAction;
class QWidget;

namespace darkeye
{

class ThemeService;
class ActressSyncService;

class WorkMaintenanceActions final : public QObject
{
    Q_OBJECT

public:
    explicit WorkMaintenanceActions(QSqlDatabase database, ThemeService &themes,
                                    QString coverDirectory, QString actressImageDirectory,
                                    QWidget *window);

    const QList<QAction *> &actions() const { return m_actions; }

signals:
    void worksChanged();
    void actressesChanged();

private:
    void assignMakers();
    void normalizeCovers();
    void updatePopularActresses();
    void updateNeededActresses();
    void translateMissingFields();
    void forceTranslateFields();
    void startTranslation(BatchTranslationMode mode);
    void syncNextActress();

    QWidget *m_window;
    QSqlDatabase m_database;
    WorkMaintenanceService m_service;
    ThemeService &m_themes;
    QString m_actressImageDirectory;
    ActressSyncService *m_actressSync = nullptr;
    BatchTranslationService *m_translations = nullptr;
    QAction *m_popularAction = nullptr;
    QAction *m_actressAction = nullptr;
    QAction *m_translateAction = nullptr;
    QAction *m_forceTranslateAction = nullptr;
    QList<QAction *> m_actions;
    QList<qint64> m_pendingActressIds;
    int m_syncedActresses = 0;
    int m_failedActresses = 0;
    BatchTranslationMode m_translationMode = BatchTranslationMode::FillMissing;
};

} // namespace darkeye
