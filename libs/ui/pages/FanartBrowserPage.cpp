#include "ui/pages/FanartBrowserPage.h"
#include "ui/pages/PageScrollArea.h"

#include "ui/components/AsyncImageLabel.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "services/ImageFetchService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace darkeye {

FanartBrowserPage::FanartBrowserPage(QSqlDatabase publicDatabase, ThemeService &themes,
                                     QString fanartDirectory, QString legacyCoverDirectory,
                                     QUrl imageFetchEndpoint, QWidget *parent)
    : LazyWidget(parent), m_themes(themes), m_repository(std::move(publicDatabase)),
      m_fanartDirectory(QDir::cleanPath(std::move(fanartDirectory))),
      m_legacyCoverDirectory(QDir::cleanPath(std::move(legacyCoverDirectory))),
      m_imageFetchEndpoint(std::move(imageFetchEndpoint))
{
    setObjectName(QStringLiteral("FanartBrowserPage"));
}

bool FanartBrowserPage::showWork(qint64 workId)
{
    initialize();
    QString errorMessage;
    const std::optional<Work> work = m_repository.findById(workId, &errorMessage);
    if (!work.has_value()) {
        ToastNotification::showMessage(window(), errorMessage.isEmpty() ? QStringLiteral("作品不存在") : errorMessage,
                                       ToastNotification::Level::Error, 3500, &m_themes);
        return false;
    }
    QList<FanartEntry> entries;
    if (!FanartStripWidget::parseJson(work->fanartJson, &entries, &errorMessage)) {
        ToastNotification::showMessage(window(), errorMessage, ToastNotification::Level::Error,
                                       3500, &m_themes);
        return false;
    }
    m_work = *work;
    m_entries = entries;
    m_currentIndex = m_entries.isEmpty() ? -1 : 0;
    updateView();
    return true;
}

qint64 FanartBrowserPage::currentWorkId() const noexcept { return m_work.id; }
int FanartBrowserPage::currentIndex() const noexcept { return m_currentIndex; }
int FanartBrowserPage::entryCount() const noexcept { return m_entries.size(); }

void FanartBrowserPage::lazyLoad()
{
    auto *root = new QVBoxLayout(addPageScrollArea(this));
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("返回作品"), this);
    back->setObjectName(QStringLiteral("FanartBrowserBackButton"));
    m_caption = new QLabel(this);
    m_caption->setObjectName(QStringLiteral("FanartBrowserCaption"));
    m_caption->setAlignment(Qt::AlignCenter);
    header->addWidget(back);
    header->addWidget(m_caption, 1);
    root->addLayout(header);

    m_image = new AsyncImageLabel(this);
    m_image->setObjectName(QStringLiteral("FanartBrowserImage"));
    m_image->setFitMode(ImageFitMode::Contain);
    m_image->setPlaceholderText(QStringLiteral("没有可显示的剧照"));
    m_image->setMinimumSize(480, 320);
    root->addWidget(m_image, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("FanartBrowserStatus"));
    m_status->setAlignment(Qt::AlignCenter);
    root->addWidget(m_status);

    auto *actions = new QHBoxLayout;
    m_previous = new QPushButton(QStringLiteral("上一张"), this);
    m_previous->setObjectName(QStringLiteral("FanartBrowserPreviousButton"));
    m_next = new QPushButton(QStringLiteral("下一张"), this);
    m_next->setObjectName(QStringLiteral("FanartBrowserNextButton"));
    m_download = new QPushButton(QStringLiteral("下载"), this);
    m_download->setObjectName(QStringLiteral("FanartBrowserDownloadButton"));
    m_delete = new QPushButton(QStringLiteral("删除"), this);
    m_delete->setObjectName(QStringLiteral("FanartBrowserDeleteButton"));
    actions->addStretch();
    actions->addWidget(m_previous);
    actions->addWidget(m_next);
    actions->addWidget(m_download);
    actions->addWidget(m_delete);
    actions->addStretch();
    root->addLayout(actions);

    m_imageFetch = new ImageFetchService(m_imageFetchEndpoint, this);
    connect(back, &QPushButton::clicked, this, &FanartBrowserPage::closeRequested);
    connect(m_previous, &QPushButton::clicked, this, &FanartBrowserPage::showPrevious);
    connect(m_next, &QPushButton::clicked, this, &FanartBrowserPage::showNext);
    connect(m_download, &QPushButton::clicked, this, &FanartBrowserPage::downloadCurrent);
    connect(m_delete, &QPushButton::clicked, this, &FanartBrowserPage::deleteCurrent);
    connect(m_imageFetch, &ImageFetchService::requestFinished, this,
            [this](quint64 requestId, bool succeeded, const QString &destination, const QString &errorMessage) {
                if (requestId != m_downloadRequestId)
                    return;
                m_downloadRequestId = 0;
                if (!succeeded || m_currentIndex < 0 || m_currentIndex >= m_entries.size()) {
                    m_status->setText(errorMessage.isEmpty() ? QStringLiteral("剧照下载失败") : errorMessage);
                    updateView();
                    return;
                }
                m_entries[m_currentIndex].file = QFileInfo(destination).fileName();
                QString saveError;
                if (!saveEntries(&saveError)) {
                    QFile::remove(destination);
                    m_status->setText(saveError);
                } else {
                    m_status->setText(QStringLiteral("剧照已下载"));
                    emit fanartChanged(m_work.id);
                }
                updateView();
            });
    updateView();
}

void FanartBrowserPage::updateView()
{
    if (m_image == nullptr)
        return;
    const bool hasEntry = m_currentIndex >= 0 && m_currentIndex < m_entries.size();
    if (!hasEntry) {
        m_image->clearSource();
        m_caption->setText(QStringLiteral("剧照 0 / 0"));
        if (m_status->text().isEmpty())
            m_status->setText(QStringLiteral("该作品还没有剧照"));
    } else {
        const FanartEntry &entry = m_entries.at(m_currentIndex);
        const QString path = resolvedPath(entry);
        if (path.isEmpty())
            m_image->clearSource();
        else
            m_image->setSource(path);
        m_caption->setText(QStringLiteral("%1 · 剧照 %2 / %3").arg(m_work.serialNumber).arg(m_currentIndex + 1).arg(m_entries.size()));
        if (m_downloadRequestId == 0)
            m_status->setText(path.isEmpty() && !entry.url.isEmpty() ? QStringLiteral("远程剧照尚未下载") : QString());
    }
    m_previous->setEnabled(m_entries.size() > 1 && m_downloadRequestId == 0);
    m_next->setEnabled(m_entries.size() > 1 && m_downloadRequestId == 0);
    m_download->setEnabled(hasEntry && !m_entries.at(m_currentIndex).url.isEmpty() &&
                           resolvedPath(m_entries.at(m_currentIndex)).isEmpty() && m_downloadRequestId == 0);
    m_delete->setEnabled(hasEntry && m_downloadRequestId == 0);
}

void FanartBrowserPage::showPrevious()
{
    if (m_entries.isEmpty()) return;
    m_currentIndex = (m_currentIndex + m_entries.size() - 1) % m_entries.size();
    updateView();
}

void FanartBrowserPage::showNext()
{
    if (m_entries.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_entries.size();
    updateView();
}

void FanartBrowserPage::downloadCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size() || m_downloadRequestId != 0)
        return;
    const FanartEntry &entry = m_entries.at(m_currentIndex);
    const QUrl source(entry.url.trimmed());
    const QString fileName = downloadFileName(entry);
    if (fileName.isEmpty()) {
        m_status->setText(QStringLiteral("剧照网址没有可用的文件名"));
        return;
    }
    m_downloadRequestId = m_imageFetch->fetchToJpeg(source, QDir(m_fanartDirectory).filePath(fileName));
    m_status->setText(QStringLiteral("正在下载剧照…"));
    updateView();
}

void FanartBrowserPage::deleteCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size()) return;
    if (QMessageBox::question(this, QStringLiteral("确认删除"), QStringLiteral("确定删除当前剧照吗？")) != QMessageBox::Yes)
        return;
    const FanartEntry entry = m_entries.at(m_currentIndex);
    m_entries.removeAt(m_currentIndex);
    QString errorMessage;
    if (!saveEntries(&errorMessage)) {
        m_entries.insert(m_currentIndex, entry);
        m_status->setText(errorMessage);
        return;
    }
    const QString fileName = QFileInfo(entry.file).fileName();
    const bool stillReferenced = std::any_of(m_entries.cbegin(), m_entries.cend(),
                                             [&entry](const FanartEntry &remaining) {
                                                 return remaining.file == entry.file;
                                             });
    if (!stillReferenced && !fileName.isEmpty() && fileName == entry.file)
        QFile::remove(QDir(m_fanartDirectory).filePath(fileName));
    if (m_currentIndex >= m_entries.size())
        m_currentIndex = m_entries.size() - 1;
    m_status->setText(QStringLiteral("剧照已删除"));
    emit fanartChanged(m_work.id);
    updateView();
}

bool FanartBrowserPage::saveEntries(QString *errorMessage)
{
    m_work.fanartJson = FanartStripWidget::toJson(m_entries);
    return m_repository.updateDetails(m_work, errorMessage);
}

QString FanartBrowserPage::resolvedPath(const FanartEntry &entry) const
{
    if (!entry.localPath.isEmpty() && QFileInfo::exists(entry.localPath)) return entry.localPath;
    const QString fileName = QFileInfo(entry.file).fileName();
    if (fileName.isEmpty() || fileName != entry.file) return {};
    const QString fanartPath = QDir(m_fanartDirectory).filePath(fileName);
    if (QFileInfo::exists(fanartPath)) return fanartPath;
    const QString legacyPath = QDir(m_legacyCoverDirectory).filePath(fileName);
    return QFileInfo::exists(legacyPath) ? legacyPath : QString();
}

QString FanartBrowserPage::downloadFileName(const FanartEntry &entry) const
{
    const QString suggested = ImageFetchService::suggestedJpegFileName(QUrl(entry.url.trimmed()));
    if (suggested.isEmpty()) return {};
    QString serial = m_work.serialNumber.toLower();
    serial.remove(QChar('-'));
    return QStringLiteral("%1_fa_%2_%3").arg(serial.isEmpty() ? QStringLiteral("fanart") : serial)
        .arg(m_currentIndex + 1).arg(suggested);
}

} // namespace darkeye
