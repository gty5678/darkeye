#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "ui/components/FanartStripWidget.h"

#include "database/repositories/WorkRepository.h"

#include <QUrl>

class QLabel;
class QPushButton;

namespace darkeye {

class AsyncImageLabel;
class ImageFetchService;
class ThemeService;

class FanartBrowserPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit FanartBrowserPage(QSqlDatabase publicDatabase, ThemeService &themes,
                              QString fanartDirectory, QString legacyCoverDirectory = {},
                              QUrl imageFetchEndpoint = {}, QWidget *parent = nullptr);

    bool showWork(qint64 workId);
    [[nodiscard]] qint64 currentWorkId() const noexcept;
    [[nodiscard]] int currentIndex() const noexcept;
    [[nodiscard]] int entryCount() const noexcept;

signals:
    void closeRequested();
    void fanartChanged(qint64 workId);

private:
    void lazyLoad() override;
    void updateView();
    void showPrevious();
    void showNext();
    void downloadCurrent();
    void deleteCurrent();
    bool saveEntries(QString *errorMessage = nullptr);
    [[nodiscard]] QString resolvedPath(const FanartEntry &entry) const;
    [[nodiscard]] QString downloadFileName(const FanartEntry &entry) const;

    ThemeService &m_themes;
    WorkRepository m_repository;
    QString m_fanartDirectory;
    QString m_legacyCoverDirectory;
    QUrl m_imageFetchEndpoint;
    Work m_work;
    QList<FanartEntry> m_entries;
    int m_currentIndex = -1;
    quint64 m_downloadRequestId = 0;
    AsyncImageLabel *m_image = nullptr;
    QLabel *m_caption = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_delete = nullptr;
    ImageFetchService *m_imageFetch = nullptr;
};

} // namespace darkeye
