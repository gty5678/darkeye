#include "ui/components/ImageDropWidget.h"

#include "ui/components/AsyncImageLabel.h"

#include <QBuffer>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QResizeEvent>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

namespace darkeye
{
namespace
{
QImage imageForJpeg(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return image;
    QImage flattened(image.size(), QImage::Format_RGB32);
    flattened.fill(Qt::white);
    QPainter painter(&flattened);
    painter.drawImage(0, 0, image);
    return flattened;
}

bool imagesHaveZeroMse(const QImage &first, const QImage &second)
{
    if (first.isNull() || second.isNull() || first.size() != second.size())
        return false;

    const QImage firstPixels = first.convertToFormat(QImage::Format_RGBA8888);
    const QImage secondPixels = second.convertToFormat(QImage::Format_RGBA8888);
    double squaredError = 0;
    for (int y = 0; y < firstPixels.height(); ++y)
    {
        const uchar *firstRow = firstPixels.constScanLine(y);
        const uchar *secondRow = secondPixels.constScanLine(y);
        for (qsizetype x = 0; x < qsizetype(firstPixels.width()) * 4; ++x)
        {
            const int difference = int(firstRow[x]) - int(secondRow[x]);
            squaredError += difference * difference;
        }
    }
    // Python's mse() uses the original resolution and requires exactly zero,
    // without a similarity threshold. Normalize Qt's decoded pixel storage.
    return squaredError / (double(firstPixels.width()) * firstPixels.height()) == 0.0;
}

bool imagesMatchSavedCover(const QString &savedPath, const QString &candidatePath)
{
    QImageReader savedReader(savedPath);
    const bool savedAsJpeg = savedReader.format() == QByteArrayLiteral("jpeg");
    savedReader.setAutoTransform(true);
    const QImage saved = savedReader.read();
    QImageReader candidateReader(candidatePath);
    candidateReader.setAutoTransform(true);
    const QImage candidate = candidateReader.read();
    if (saved.isNull() || candidate.isNull() || saved.size() != candidate.size())
        return false;
    if (imagesHaveZeroMse(saved, candidate))
        return true;
    if (!savedAsJpeg)
        return false;

    // Older C++ builds re-encoded every cover at quality 92. Non-JPEG sources
    // still use that conversion. Compare the exact decoded save result too,
    // so dropping the original after saving works without a fuzzy threshold.
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)
        || !imageForJpeg(candidate).save(&buffer, "JPEG", 92))
        return false;
    return imagesHaveZeroMse(saved, QImage::fromData(encoded, "JPEG"));
}
} // namespace

ImageDropWidget::ImageDropWidget(QString managedDirectory, QWidget *parent)
    : QWidget(parent), m_managedDirectory(QDir::cleanPath(std::move(managedDirectory)))
{
    setObjectName(QStringLiteral("ImageDropWidget"));
    setAcceptDrops(true);
    setMinimumSize(0, 0);
    m_preview = new AsyncImageLabel(this);
    m_preview->setObjectName(QStringLiteral("ImageDropPreview"));
    m_preview->setPlaceholderText(QStringLiteral("点击或拖入人物头像"));
    m_preview->setFitMode(ImageFitMode::Contain);
    m_preview->setMinimumSize(200, 240);
    // 预览图的像素尺寸不应参与 MyADS 工作区的尺寸协商；否则图片加载完成后会
    // 改变 sizeHint，触发布局、resizeEvent 和下一次异步加载，形成可见的闪烁循环。
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_preview->installEventFilter(this);
    m_qualityBadge = new QLabel(QStringLiteral("非高清图"), this);
    m_qualityBadge->setObjectName(QStringLiteral("coverQualityBadge"));
    m_qualityBadge->setAlignment(Qt::AlignCenter);
    m_qualityBadge->setCursor(Qt::PointingHandCursor);
    m_qualityBadge->setToolTip(QStringLiteral("点击从 Fanza 下载可能的大图封面"));
    m_qualityBadge->setStyleSheet(QStringLiteral(
        "QLabel#coverQualityBadge { background-color: rgba(0, 0, 0, 160);"
        " color: #FFD54F; border: 1px solid #FFD54F; border-radius: 8px;"
        " font-size: 12px; font-weight: 600; padding: 1px 6px; }"));
    m_qualityBadgeText = new QLabel(QStringLiteral("非高清图"), m_qualityBadge);
    m_qualityBadgeText->setObjectName(QStringLiteral("coverQualityBadgeText"));
    m_qualityBadgeText->setAlignment(Qt::AlignCenter);
    m_qualityBadgeText->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_qualityBadgeText->setStyleSheet(QStringLiteral(
        "QLabel#coverQualityBadgeText { background: transparent; color: #FFD54F;"
        " border: none; padding: 0; margin: 0; }"));
    m_qualityBadge->hide();
    m_qualityBadge->installEventFilter(this);
    refreshStyle();
}

void ImageDropWidget::setPurpose(const QString &purpose, const QString &placeholder)
{
    m_purpose = purpose.trimmed().isEmpty() ? QStringLiteral("图片") : purpose.trimmed();
    m_preview->setPlaceholderText(placeholder.trimmed().isEmpty()
                                      ? QStringLiteral("点击或拖入%1").arg(m_purpose)
                                      : placeholder);
}

QString ImageDropWidget::purpose() const
{
    return m_purpose;
}

void ImageDropWidget::setFitMode(ImageFitMode mode)
{
    m_preview->setFitMode(mode);
}

void ImageDropWidget::setPreviewAspectRatio(qreal aspectRatio)
{
    m_previewAspectRatio = aspectRatio > 0.0 ? aspectRatio : 0.0;
    if (m_previewAspectRatio > 0.0)
        m_preview->setMinimumSize(0, 0);
    updatePreviewGeometry();
}

void ImageDropWidget::setQualityBadgeEnabled(bool enabled)
{
    m_qualityBadgeEnabled = enabled;
    updateQualityBadge();
}

void ImageDropWidget::setQualityBadgeStyleSheet(const QString &styleSheet,
                                                const QColor &textColor)
{
    m_qualityBadge->setStyleSheet(styleSheet);
    // QStyleSheetStyle may repaint the badge panel over QLabel's own content
    // on Windows.  Keep the Python-compatible panel on the parent and put the
    // caption on a transparent child layer that is painted afterwards.
    m_qualityBadgeText->setFont(m_qualityBadge->font());
    m_qualityBadgeText->setStyleSheet(
        QStringLiteral("QLabel#coverQualityBadgeText { background: transparent; color: %1;"
                       " border: none; padding: 0; margin: 0; }")
            .arg(textColor.name(QColor::HexRgb)));
    m_qualityBadge->setAccessibleName(QStringLiteral("非高清图"));
    updateQualityBadge();
}

void ImageDropWidget::setImagePath(const QString &path)
{
    const QString normalized = path.trimmed();
    m_originalImagePath = normalized;
    if (m_imagePath == normalized)
    {
        setDirty(false);
        return;
    }
    m_imagePath = normalized;
    if (m_imagePath.isEmpty())
        m_preview->clearSource();
    else
        m_preview->setSource(resolvedImagePath());
    updateQualityBadge();
    setDirty(false);
}

bool ImageDropWidget::setPendingImagePath(const QString &path)
{
    const QFileInfo info(path);
    return acceptImage(info.isAbsolute() ? path : QDir(m_managedDirectory).filePath(path));
}

QString ImageDropWidget::imagePath() const
{
    return m_imagePath;
}

QString ImageDropWidget::resolvedImagePath() const
{
    if (m_imagePath.isEmpty())
        return {};
    const QFileInfo info(m_imagePath);
    return info.isAbsolute() ? QDir::cleanPath(m_imagePath)
                             : QDir(m_managedDirectory).filePath(m_imagePath);
}

bool ImageDropWidget::isDirty() const noexcept
{
    return m_dirty;
}

void ImageDropWidget::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    refreshStyle();
    emit dirtyChanged(m_dirty);
}

void ImageDropWidget::clearImage()
{
    if (m_imagePath.isEmpty())
        return;
    m_imagePath.clear();
    m_preview->clearSource();
    updateQualityBadge();
    setDirty(!m_originalImagePath.isEmpty());
    emit imageChanged({});
}

bool ImageDropWidget::persistAsJpeg(const QString &fileName, QString *relativePath,
                                    QString *errorMessage) const
{
    if (relativePath == nullptr)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("未提供%1保存结果").arg(m_purpose);
        return false;
    }
    relativePath->clear();
    if (m_imagePath.isEmpty())
        return true;

    QString safeName = fileName.trimmed();
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        safeName.replace(character, QChar('_'));
    if (!safeName.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive))
        safeName += QStringLiteral(".jpg");
    if (safeName == QStringLiteral(".jpg"))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("%1文件名不能为空").arg(m_purpose);
        return false;
    }

    const QString targetPath = QDir(m_managedDirectory).filePath(safeName);
    const QFileInfo sourceInfo(resolvedImagePath());
    const QFileInfo targetInfo(targetPath);
    // Match Python's rename_save_image(): submitting metadata or name edits must not
    // rewrite an avatar that is already stored under the requested managed path.  Apart
    // from doing unnecessary lossy JPEG recompression, replacing the file can fail with
    // "Access is denied" on Windows while the preview still has the image open.
    if (sourceInfo.exists() && targetInfo.exists() &&
        sourceInfo.canonicalFilePath() == targetInfo.canonicalFilePath())
    {
        *relativePath = safeName;
        return true;
    }

    QImageReader reader(sourceInfo.absoluteFilePath());
    const bool sourceIsJpeg = reader.format() == QByteArrayLiteral("jpeg");
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法读取%1：%2").arg(m_purpose, reader.errorString());
        return false;
    }
    if (!QDir().mkpath(m_managedDirectory))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建%1目录：%2").arg(m_purpose, m_managedDirectory);
        return false;
    }

    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
            *errorMessage = target.errorString();
        return false;
    }
    bool written = false;
    if (sourceIsJpeg)
    {
        // Match Python's rename_save_image(): retain JPEG bytes rather than
        // introducing another lossy generation on every import.
        QFile source(sourceInfo.absoluteFilePath());
        if (!source.open(QIODevice::ReadOnly))
        {
            if (errorMessage != nullptr)
                *errorMessage = source.errorString();
            return false;
        }
        const QByteArray bytes = source.readAll();
        written = source.error() == QFileDevice::NoError
            && target.write(bytes) == bytes.size();
    }
    else
        written = imageForJpeg(image).save(&target, "JPEG", 92);
    if (!written || !target.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = target.errorString().isEmpty()
                                ? QStringLiteral("%1写入失败").arg(m_purpose)
                                : target.errorString();
        return false;
    }
    *relativePath = safeName;
    return true;
}

bool ImageDropWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_qualityBadge)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
                emit qualityBadgeClicked();
        }
        // Do not let a badge click fall through to the preview's image picker.
        return true;
    }
    if (watched == m_preview && event->type() == QEvent::MouseButtonPress)
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
            return chooseImage();
    }
    return QWidget::eventFilter(watched, event);
}

void ImageDropWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePreviewGeometry();
}

void ImageDropWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage())
        event->acceptProposedAction();
}

void ImageDropWidget::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && acceptImage(url.toLocalFile()))
        {
            event->acceptProposedAction();
            return;
        }
    }
    // Some browsers supply bitmap data rather than a usable local file URL.
    if (event->mimeData()->hasImage())
    {
        const QImage image = qvariant_cast<QImage>(event->mimeData()->imageData());
        const QString path = m_pendingImages.filePath(
            QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
        if (!image.isNull() && m_pendingImages.isValid() && image.save(path, "PNG"))
        {
            showAcceptedImage(path);
            event->acceptProposedAction();
            return;
        }
        emit imageRejected(QStringLiteral("无法保存拖入的%1图片数据").arg(m_purpose));
    }
}

void ImageDropWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const QFileInfo imageInfo(resolvedImagePath());
    QAction *openFolder = menu.addAction(QStringLiteral("打开图片所在位置"));
    openFolder->setEnabled(imageInfo.exists());
    QAction *select = menu.addAction(QStringLiteral("选择%1").arg(m_purpose));
    QAction *clear = menu.addAction(QStringLiteral("清除%1").arg(m_purpose));
    clear->setEnabled(!m_imagePath.isEmpty());
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == openFolder)
    {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer"),
                                {QStringLiteral("/select,"),
                                 QDir::toNativeSeparators(imageInfo.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
        QProcess::startDetached(QStringLiteral("open"),
                                {QStringLiteral("-R"), imageInfo.absoluteFilePath()});
#endif
    }
    else if (chosen == select)
        chooseImage();
    else if (chosen == clear)
        clearImage();
}

bool ImageDropWidget::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择%1").arg(m_purpose), QString(),
        QStringLiteral("图片文件 (*.jpg *.jpeg *.png *.bmp *.gif *.webp)"));
    return path.isEmpty() ? false : acceptImage(path);
}

bool ImageDropWidget::acceptImage(const QString &path, QString *errorMessage)
{
    QImageReader reader(path);
    if (!reader.canRead())
    {
        const QString message = QStringLiteral("不是可读取的图片文件：%1").arg(path);
        if (errorMessage != nullptr)
            *errorMessage = message;
        emit imageRejected(message);
        return false;
    }
    // Own the original bytes before returning from the drop event. Resizing
    // reloads the preview from disk, and saving also needs the full-size image.
    const QString pendingPath = m_pendingImages.filePath(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!m_pendingImages.isValid() || !QFile::copy(path, pendingPath))
    {
        const QString message = QStringLiteral("无法保留拖入的%1图片：%2").arg(m_purpose, path);
        if (errorMessage != nullptr)
            *errorMessage = message;
        emit imageRejected(message);
        return false;
    }
    showAcceptedImage(pendingPath);
    return true;
}

void ImageDropWidget::showAcceptedImage(const QString &path)
{
    const QFileInfo originalInfo(m_originalImagePath);
    const QString originalPath = originalInfo.isAbsolute()
        ? m_originalImagePath : QDir(m_managedDirectory).filePath(m_originalImagePath);
    const bool unchanged = !m_originalImagePath.isEmpty()
        && imagesMatchSavedCover(originalPath, path);
    // Keep the saved path for identical pixels so editor path comparisons and
    // later metadata-only saves do not treat a temporary copy as a new cover.
    m_imagePath = unchanged ? m_originalImagePath : path;
    m_preview->setSource(resolvedImagePath());
    updateQualityBadge();
    setDirty(!unchanged);
    emit imageChanged(m_imagePath);
}

void ImageDropWidget::updatePreviewGeometry()
{
    QRect available = rect();
    if (available.isEmpty())
        return;
    if (m_previewAspectRatio > 0.0)
    {
        QSize previewSize = available.size();
        if (qreal(previewSize.width()) / previewSize.height() > m_previewAspectRatio)
            previewSize.setWidth(qRound(previewSize.height() * m_previewAspectRatio));
        else
            previewSize.setHeight(qRound(previewSize.width() / m_previewAspectRatio));
        const int x = available.x() + (available.width() - previewSize.width()) / 2;
        const int y = available.y() + (available.height() - previewSize.height()) / 2;
        m_preview->setGeometry(x, y, previewSize.width(), previewSize.height());
    }
    else
    {
        m_preview->setGeometry(available);
    }
    updateQualityBadge();
}

void ImageDropWidget::updateQualityBadge()
{
    const QFileInfo imageInfo(resolvedImagePath());
    const bool showBadge = m_qualityBadgeEnabled && imageInfo.exists() &&
                           imageInfo.size() < 500 * 1024;
    m_qualityBadge->setVisible(showBadge);
    if (!showBadge)
        return;
    m_qualityBadge->adjustSize();
    m_qualityBadgeText->setGeometry(m_qualityBadge->contentsRect());
    m_qualityBadgeText->raise();
    constexpr int margin = 6;
    m_qualityBadge->move(m_preview->geometry().right() - m_qualityBadge->width() - margin + 1,
                         m_preview->geometry().top() + margin);
    m_qualityBadge->raise();
}

void ImageDropWidget::refreshStyle()
{
    m_preview->setProperty("imageDropBorder", true);
    m_preview->setProperty("imageDropDirty", m_dirty);
    m_preview->update();
}

} // namespace darkeye
