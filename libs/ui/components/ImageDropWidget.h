#pragma once

#include <QWidget>
#include <QTemporaryDir>

class QContextMenuEvent;
class QColor;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QLabel;
class QResizeEvent;

namespace darkeye
{

class AsyncImageLabel;
enum class ImageFitMode;

class ImageDropWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ImageDropWidget(QString managedDirectory, QWidget *parent = nullptr);

    void setPurpose(const QString &purpose, const QString &placeholder = {});
    [[nodiscard]] QString purpose() const;
    void setFitMode(ImageFitMode mode);
    void setPreviewAspectRatio(qreal aspectRatio);
    void setQualityBadgeEnabled(bool enabled);
    /// Overrides the low-quality badge appearance.  The editor supplies its
    /// theme tokens so this reusable widget stays independent of ThemeService.
    void setQualityBadgeStyleSheet(const QString &styleSheet, const QColor &textColor);
    void setImagePath(const QString &path);
    /// Selects a replacement and compares its pixels with the loaded image.
    bool setPendingImagePath(const QString &path);
    [[nodiscard]] QString imagePath() const;
    [[nodiscard]] QString resolvedImagePath() const;
    [[nodiscard]] bool isDirty() const noexcept;
    void setDirty(bool dirty);
    void clearImage();

    bool persistAsJpeg(const QString &fileName, QString *relativePath,
                       QString *errorMessage = nullptr) const;

signals:
    void imageChanged(const QString &path);
    void imageRejected(const QString &message);
    /// Emitted whenever the pending-image state changes, including
    /// programmatic replacements such as a downloaded high-quality cover.
    void dirtyChanged(bool dirty);
    /// Emitted when the visible low-quality-cover badge is clicked.  Consumers
    /// can offer a source-specific replacement without coupling this reusable
    /// drop widget to a downloader.
    void qualityBadgeClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    bool chooseImage();
    bool acceptImage(const QString &path, QString *errorMessage = nullptr);
    void showAcceptedImage(const QString &path);
    void updatePreviewGeometry();
    void updateQualityBadge();
    void refreshStyle();

    QString m_managedDirectory;
    QString m_purpose = QStringLiteral("人物头像");
    QString m_imagePath;
    QString m_originalImagePath;
    // Browser drops can refer to files that only exist during the drag.
    QTemporaryDir m_pendingImages;
    AsyncImageLabel *m_preview = nullptr;
    QLabel *m_qualityBadge = nullptr;
    QLabel *m_qualityBadgeText = nullptr;
    qreal m_previewAspectRatio = 0.0;
    bool m_qualityBadgeEnabled = false;
    bool m_dirty = false;
};

} // namespace darkeye
