#include "ui/components/WorkMaintenanceActions.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "database/repositories/PersonRepository.h"
#include "services/TopActressSyncService.h"
#include "services/ActressSyncService.h"
#include "settings/Settings.h"
#include "darkeye_ui/components/ToastNotification.h"

#include <QAction>
#include <QMessageBox>
#include <QWidget>

namespace darkeye
{

WorkMaintenanceActions::WorkMaintenanceActions(QSqlDatabase database, ThemeService &themes,
                                               QString coverDirectory, QString actressImageDirectory,
                                               QWidget *window)
    : QObject(window), m_window(window), m_database(database),
      m_service(database, std::move(coverDirectory)), m_themes(themes),
      m_actressImageDirectory(std::move(actressImageDirectory))
{
    m_translations = new BatchTranslationService(database, this);

    const auto addAction = [this](const QString &id, const QString &title, const QString &description,
                                  auto callback, bool confirm = false)
    {
        auto *action = new QAction(title, this);
        action->setObjectName(id);
        action->setToolTip(description);
        connect(action, &QAction::triggered, this, [this, action, callback, confirm]
        {
            if (confirm && QMessageBox::question(m_window, action->text(), action->toolTip(),
                                                  QMessageBox::Yes | QMessageBox::No,
                                                  QMessageBox::No) != QMessageBox::Yes)
                return;
            (this->*callback)();
        });
        m_actions.append(action);
        return action;
    };
    m_popularAction = addAction(
        QStringLiteral("update_popular_actresses"), QStringLiteral("更新热门女优"),
        QStringLiteral("通过本地 Collector 与浏览器扩展解析前 50 名并写入女优库"),
        &WorkMaintenanceActions::updatePopularActresses);
    m_actressAction = addAction(
        QStringLiteral("update_needed_actresses"), QStringLiteral("更新标记需要更新的女优数据"),
        QStringLiteral("逐条采集 Minnano AV 并合并所有标记为待更新的女优"),
        &WorkMaintenanceActions::updateNeededActresses);
    addAction(QStringLiteral("assign_makers"), QStringLiteral("根据番号前缀判断片商"),
              QStringLiteral("按现有前缀规则更新匹配的未删除作品，可能替换已有片商。确定继续？"),
              &WorkMaintenanceActions::assignMakers, true);
    m_translateAction = addAction(
        QStringLiteral("translate_missing"), QStringLiteral("补译缺失的中文标题 / 简介"),
        QStringLiteral("只翻译缺少中文内容的标题或简介"),
        &WorkMaintenanceActions::translateMissingFields);
    m_forceTranslateAction = addAction(
        QStringLiteral("translate_overwrite"), QStringLiteral("覆盖已有中文标题 / 简介"),
        QStringLiteral("重新翻译未删除作品中有日文原文的标题和简介，并覆盖已有中文内容。确定继续？"),
        &WorkMaintenanceActions::forceTranslateFields, true);
    addAction(QStringLiteral("normalize_covers"), QStringLiteral("统一封面文件名为番号.jpg"),
              QStringLiteral("重命名符合条件的封面并更新数据库路径，跳过已存在的目标文件。确定继续？"),
              &WorkMaintenanceActions::normalizeCovers, true);

    connect(m_translations, &BatchTranslationService::progress, this,
            [this](int done, int total, qint64 elapsedMilliseconds)
            {
                auto *active = m_translationMode == BatchTranslationMode::ForceOverwrite
                    ? m_forceTranslateAction : m_translateAction;
                active->setStatusTip(QStringLiteral("正在翻译 %1/%2 · %3 秒")
                                         .arg(done)
                                         .arg(total)
                                         .arg(elapsedMilliseconds / 1000.0, 0, 'f', 1));
            });
    connect(m_translations, &BatchTranslationService::finished, this,
            [this](const BatchTranslationResult &result)
            {
                m_translateAction->setEnabled(true);
                m_forceTranslateAction->setEnabled(true);
                m_translateAction->setStatusTip({});
                m_forceTranslateAction->setStatusTip({});
                if (!result.succeeded)
                {
                    Toast::showError(m_window, QStringLiteral("批量翻译失败：%1").arg(result.errorMessage),
                                     &m_themes);
                    return;
                }
                emit worksChanged();
                Toast::showSuccess(m_window, result.summary(m_translationMode),
                                   &m_themes);
            });
}

void WorkMaintenanceActions::assignMakers()
{
    const MakerAssignmentResult result = m_service.assignMakersFromPrefixes();
    if (!result.succeeded)
    {
        Toast::showError(m_window, QStringLiteral("批量更新失败：%1").arg(result.errorMessage),
                         &m_themes);
        return;
    }
    emit worksChanged();
    Toast::showSuccess(m_window, result.summary(), &m_themes);
}

void WorkMaintenanceActions::normalizeCovers()
{
    const CoverNormalizationResult result = m_service.normalizeCoverFileNames();
    if (!result.succeeded)
    {
        Toast::showError(m_window, QStringLiteral("封面整理失败：%1").arg(result.errorMessage),
                         &m_themes);
        return;
    }
    emit worksChanged();
    Toast::showSuccess(m_window, result.summary(), &m_themes);
}

void WorkMaintenanceActions::updatePopularActresses()
{
    auto *sync = new TopActressSyncService(m_database, settings::crawler().topActressesApiUrl, this);
    connect(sync, &TopActressSyncService::finished, this,
            [this, sync](const TopActressSyncResult &result)
            {
                sync->deleteLater();
                m_popularAction->setEnabled(true);
                m_popularAction->setStatusTip({});
                if (!result.succeeded)
                {
                    Toast::showError(
                        m_window, QStringLiteral("热门女优更新失败：%1").arg(result.errorMessage),
                        &m_themes);
                    return;
                }
                emit actressesChanged();
                Toast::showSuccess(m_window, result.summary(), &m_themes);
            });
    sync->start();
    m_popularAction->setEnabled(false);
    m_popularAction->setStatusTip(QStringLiteral("正在更新热门女优…"));
    Toast::showMessage(m_window, QStringLiteral("已请求 Collector 解析热门女优"),
                       Toast::Level::Info, 2500, &m_themes);
}

void WorkMaintenanceActions::updateNeededActresses()
{
    if (m_actressSync)
        return;
    QString error;
    m_pendingActressIds = PersonRepository(m_database).actressIdsNeedingUpdate(&error);
    if (!error.isEmpty())
    {
        Toast::showError(m_window, QStringLiteral("读取待更新女优失败：%1").arg(error), &m_themes);
        return;
    }
    if (m_pendingActressIds.isEmpty())
    {
        Toast::showMessage(m_window, QStringLiteral("没有待更新的女优"), Toast::Level::Info, 2500,
                           &m_themes);
        return;
    }
    const auto crawler = settings::crawler();
    m_actressSync = new ActressSyncService(m_database, crawler.actressApiBaseUrl,
                                          m_actressImageDirectory, crawler.coverFetchApiUrl, this);
    connect(m_actressSync, &ActressSyncService::finished, this,
            [this](const ActressSyncResult &result)
            {
                if (result.succeeded)
                    ++m_syncedActresses;
                else
                    ++m_failedActresses;
                syncNextActress();
            });
    m_syncedActresses = 0;
    m_failedActresses = 0;
    m_actressAction->setEnabled(false);
    m_actressAction->setStatusTip(QStringLiteral("正在更新女优…"));
    syncNextActress();
}

void WorkMaintenanceActions::translateMissingFields()
{
    startTranslation(BatchTranslationMode::FillMissing);
}

void WorkMaintenanceActions::forceTranslateFields()
{
    startTranslation(BatchTranslationMode::ForceOverwrite);
}

void WorkMaintenanceActions::startTranslation(BatchTranslationMode mode)
{
    if (m_translations->isBusy())
        return;
    m_translationMode = mode;
    m_translateAction->setEnabled(false);
    m_forceTranslateAction->setEnabled(false);
    m_translateAction->setStatusTip(QStringLiteral("批量翻译执行中"));
    m_forceTranslateAction->setStatusTip(QStringLiteral("批量翻译执行中"));
    m_translations->start(mode);
    Toast::showMessage(m_window, QStringLiteral("正在后台翻译，请稍候…"),
                       Toast::Level::Info, 2500, &m_themes);
}

void WorkMaintenanceActions::syncNextActress()
{
    while (!m_pendingActressIds.isEmpty())
    {
        const qint64 actressId = m_pendingActressIds.takeFirst();
        if (m_actressSync->start(actressId))
            return;
        ++m_failedActresses;
    }
    m_actressSync->deleteLater();
    m_actressSync = nullptr;
    m_actressAction->setEnabled(true);
    m_actressAction->setStatusTip({});
    if (m_syncedActresses > 0)
        emit actressesChanged();
    Toast::showMessage(m_window, QStringLiteral("女优更新完成：成功 %1，失败 %2")
                           .arg(m_syncedActresses).arg(m_failedActresses),
                       m_failedActresses == 0 ? Toast::Level::Success : Toast::Level::Warning,
                       4000, &m_themes);
}

} // namespace darkeye
