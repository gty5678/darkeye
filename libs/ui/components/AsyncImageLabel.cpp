#include "ui/components/AsyncImageLabel.h"

#include <QCache>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QImageReader>
#include <QResizeEvent>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

namespace darkeye {
namespace {

constexpr int imageCacheLimitKiB = 96 * 1024;

QCache<QString, QPixmap> &imageCache()
{
    static QCache<QString, QPixmap> cache(imageCacheLimitKiB);
    return cache;
}

QString imageCacheKey(const QString &path, const QSize &target, ImageFitMode fitMode)
{
    return QStringLiteral("%1|%2x%3|%4")
        .arg(path)
        .arg(target.width())
        .arg(target.height())
        .arg(static_cast<int>(fitMode));
}

int imageCostKiB(const QPixmap &pixmap)
{
    return qMax(1, pixmap.width() * pixmap.height() * 4 / 1024);
}

QThreadPool &imageThreadPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        // Python's cover loader uses Qt's global pool, whose default is the
        // ideal thread count. Keep the dedicated pool (so other jobs are not
        // starved), but match that concurrency for first-page cover loading.
        pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount()));
        return true;
    }();
    Q_UNUSED(configured);
    return pool;
}

class ImageLoadTask final : public QObject, public QRunnable
{
    Q_OBJECT

public:
    ImageLoadTask(QString path, QSize target, ImageFitMode fitMode, quint64 requestId,
                  std::shared_ptr<std::atomic_bool> cancelled)
        : m_path(std::move(path)), m_target(target), m_fitMode(fitMode),
          m_requestId(requestId), m_cancelled(std::move(cancelled))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_cancelled->load()) {
            emit completed(m_requestId, m_path, {});
            return;
        }
        QImageReader reader(m_path);
        const QSize sourceSize = reader.size();
        if (sourceSize.isValid() && m_target.isValid() && !m_target.isEmpty()) {
            QSize decodedSize;
            if (m_fitMode == ImageFitMode::RightCover
                && qreal(sourceSize.width()) / sourceSize.height()
                    >= qreal(m_target.width()) / m_target.height()) {
                decodedSize = QSize(qRound(qreal(sourceSize.width()) * m_target.height()
                                           / sourceSize.height()),
                                    m_target.height());
            } else {
                const Qt::AspectRatioMode aspect =
                    m_fitMode == ImageFitMode::Cover ? Qt::KeepAspectRatioByExpanding
                                                     : Qt::KeepAspectRatio;
                decodedSize = sourceSize.scaled(m_target, aspect);
            }
            if (decodedSize.isValid() && !decodedSize.isEmpty())
                reader.setScaledSize(decodedSize);
        }
        QImage image = reader.read();
        if (!image.isNull() && m_target.isValid() && !m_target.isEmpty()) {
            if (m_fitMode == ImageFitMode::RightCover) {
                if (image.height() != m_target.height())
                    image = image.scaledToHeight(m_target.height(), Qt::SmoothTransformation);
                const int cropWidth = qMin(image.width(), m_target.width());
                image = image.copy(image.width() - cropWidth, 0, cropWidth, image.height());
            } else if (m_fitMode == ImageFitMode::Cover) {
                image = image.scaled(m_target, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
                const int x = qMax(0, (image.width() - m_target.width()) / 2);
                const int y = qMax(0, (image.height() - m_target.height()) / 2);
                image = image.copy(x, y, m_target.width(), m_target.height());
            } else if (m_fitMode == ImageFitMode::Contain) {
                image = image.scaled(m_target, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
            }
        }
        emit completed(m_requestId, m_path, m_cancelled->load() ? QImage() : image);
    }

signals:
    void completed(quint64 requestId, const QString &path, const QImage &image);

private:
    QString m_path;
    QSize m_target;
    ImageFitMode m_fitMode;
    quint64 m_requestId;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace

AsyncImageLabel::AsyncImageLabel(QWidget *parent) : QLabel(parent)
{
    setObjectName(QStringLiteral("DesignAsyncImage"));
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QStringLiteral("background-color: transparent;"));
    setAttribute(Qt::WA_TranslucentBackground);
    setText(m_placeholder);
}

AsyncImageLabel::~AsyncImageLabel()
{
    if (m_cancelLoad) m_cancelLoad->store(true);
}

QString AsyncImageLabel::source() const { return m_source; }

void AsyncImageLabel::setSource(const QString &path)
{
    const QString normalized = path.trimmed();
    if (m_source == normalized) return;
    m_source = normalized;
    startLoad();
}

void AsyncImageLabel::clearSource()
{
    m_source.clear();
    m_loadPending = false;
    if (m_cancelLoad) m_cancelLoad->store(true);
    ++m_requestId;
    clear();
    setText(m_placeholder);
}

void AsyncImageLabel::reload() { startLoad(); }

ImageFitMode AsyncImageLabel::fitMode() const { return m_fitMode; }

void AsyncImageLabel::setFitMode(ImageFitMode mode)
{
    if (m_fitMode == mode) return;
    m_fitMode = mode;
    startLoad();
}

QString AsyncImageLabel::placeholderText() const { return m_placeholder; }

void AsyncImageLabel::setPlaceholderText(const QString &text)
{
    m_placeholder = text;
    if (pixmap().isNull()) setText(m_placeholder);
}

void AsyncImageLabel::setDeferredLoading(bool deferred)
{
    if (m_deferredLoading == deferred) return;
    m_deferredLoading = deferred;
    if (deferred) {
        ++m_requestId;
        if (m_cancelLoad) m_cancelLoad->store(true);
        m_loadPending = !m_source.isEmpty();
        clear();
        setText(m_placeholder);
    } else if (m_loadPending) {
        startLoad();
    }
}

void AsyncImageLabel::startDeferredLoad(int priority)
{
    m_loadPriority = priority;
    m_deferredLoading = false;
    if (m_loadPending || (!m_source.isEmpty() && pixmap().isNull()))
        startLoad();
}

void AsyncImageLabel::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);
    if (!property("imageDropBorder").toBool())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dirty = property("imageDropDirty").toBool();
    // Keep this frame identical to Python's CoverDropWidget: square, 2px and dashed.
    painter.setPen(QPen(dirty ? QColor(QStringLiteral("#FFA500"))
                             : QColor(QStringLiteral("#8a8a8a")),
                        2.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
}

void AsyncImageLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    if (!m_source.isEmpty() && event->size() != event->oldSize()) startLoad();
}

void AsyncImageLabel::startLoad()
{
    if (m_deferredLoading) {
        m_loadPending = true;
        return;
    }
    m_loadPending = false;
    const quint64 requestId = ++m_requestId;
    if (m_cancelLoad) m_cancelLoad->store(true);
    if (m_source.isEmpty() || size().isEmpty()) {
        clear();
        setText(m_placeholder);
        return;
    }
    const QString cacheKey = imageCacheKey(m_source, size(), m_fitMode);
    if (const QPixmap *cached = imageCache().object(cacheKey)) {
        setText({});
        setPixmap(*cached);
        emit imageLoaded(m_source);
        return;
    }
    // 尺寸调整和 Dock 布局重排会连续触发加载。保留已经显示的图像直到新结果
    // 准备好，避免封面栏在每次重排时退回“加载中”。
    if (pixmap().isNull()) setText(QStringLiteral("加载中…"));
    if (m_cancelLoad) {
        m_loadPending = true;
        return;
    }
    m_cancelLoad = std::make_shared<std::atomic_bool>(false);
    auto *task = new ImageLoadTask(m_source, size(), m_fitMode, requestId, m_cancelLoad);
    connect(task, &ImageLoadTask::completed, this,
            [this](quint64 id, const QString &path, const QImage &image) {
                applyResult(id, path, image);
            });
    imageThreadPool().start(task, m_loadPriority);
}

void AsyncImageLabel::applyResult(quint64 requestId, const QString &path,
                                  const QImage &image)
{
    m_cancelLoad.reset();
    if (requestId != m_requestId || path != m_source) {
        if (!m_deferredLoading && m_loadPending)
            startLoad();
        return;
    }
    if (image.isNull()) {
        clear();
        setText(m_placeholder);
        emit imageLoadFailed(path);
        return;
    }
    const QPixmap displayPixmap = QPixmap::fromImage(image);
    imageCache().insert(imageCacheKey(path, size(), m_fitMode),
                        new QPixmap(displayPixmap), imageCostKiB(displayPixmap));
    setText({});
    setPixmap(displayPixmap);
    emit imageLoaded(path);
}

} // namespace darkeye

#include "AsyncImageLabel.moc"
