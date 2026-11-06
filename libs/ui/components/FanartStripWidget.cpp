#include "ui/components/FanartStripWidget.h"

#include "services/ImageFetchService.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QDir>
#include <QDialog>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QUrl>
#include <QToolButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>
#include <utility>

namespace darkeye
{
namespace
{
constexpr int fanartThumbSide = 100;
constexpr int fanartThumbMargin = 4;
constexpr int fanartThumbCell = fanartThumbSide + 2 * fanartThumbMargin;

class FanartPreviewImage final : public QLabel
{
public:
    using QLabel::QLabel;

    void showImage(const QPixmap &source, const QString &placeholder)
    {
        m_source = source;
        if (m_source.isNull())
        {
            clear();
            setText(placeholder);
        }
        else
            scaleImage();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        if (!m_source.isNull())
            scaleImage();
    }

private:
    void scaleImage()
    {
        setPixmap(m_source.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QPixmap m_source;
};

class FanartPreviewDialog final : public QDialog
{
public:
    using QDialog::QDialog;
    std::function<void(int)> navigate;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // QPushButton consumes arrow keys for focus traversal before they can
        // reach the dialog. Handle navigation first, except in the URL editor.
        if (event->type() == QEvent::KeyPress && !qobject_cast<QLineEdit *>(watched))
        {
            auto *key = static_cast<QKeyEvent *>(event);
            if (navigate && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right))
            {
                navigate(key->key() == Qt::Key_Left ? -1 : 1);
                key->accept();
                return true;
            }
        }
        return QDialog::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (navigate && !qobject_cast<QLineEdit *>(focusWidget()) &&
            (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right))
        {
            navigate(event->key() == Qt::Key_Left ? -1 : 1);
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }
};

class FanartHorizontalScrollArea final : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        QScrollBar *bar = horizontalScrollBar();
        const QPoint pixelDelta = event->pixelDelta();
        if (!pixelDelta.isNull())
        {
            const int delta = pixelDelta.x() != 0 ? pixelDelta.x() : pixelDelta.y();
            bar->setValue(bar->value() - delta);
            event->accept();
            return;
        }
        const QPoint angleDelta = event->angleDelta();
        const int delta = angleDelta.y() != 0 ? angleDelta.y() : angleDelta.x();
        if (delta != 0)
        {
            bar->setValue(bar->value() - delta);
            event->accept();
            return;
        }
        QScrollArea::wheelEvent(event);
    }
};

class FanartRemoveButton final : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink(255, 255, 255, underMouse() || hasFocus() ? 255 : 225);
        painter.setPen(QPen(ink, 1.25, Qt::SolidLine, Qt::RoundCap));
        const QPointF center = QRectF(rect()).center();
        const QPointF down(3.0, 3.0);
        const QPointF up(3.0, -3.0);
        painter.drawLine(center - down, center + down);
        painter.drawLine(center - up, center + up);
    }
};

class FanartThumbCell final : public QFrame
{
public:
    FanartThumbCell(std::function<void()> clicked, std::function<void()> removeRequested,
                    QWidget *parent = nullptr)
        : QFrame(parent), m_clicked(std::move(clicked)),
          m_removeRequested(std::move(removeRequested))
    {
        setObjectName(QStringLiteral("FanartCell"));
        setStyleSheet(QStringLiteral("QFrame#FanartCell, QFrame#FanartAddPlaceholder { background: transparent; border: none; padding: 0; margin: 0; }"));
        setFrameShape(QFrame::NoFrame);
        setFixedSize(fanartThumbCell, fanartThumbCell);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(fanartThumbMargin, fanartThumbMargin,
                                   fanartThumbMargin, fanartThumbMargin);
        m_label = new QLabel(this);
        m_label->setStyleSheet(QStringLiteral("border: none; padding: 0; margin: 0; background: transparent;"));
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setFixedSize(fanartThumbSide, fanartThumbSide);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(m_label, 0, Qt::AlignCenter);
        m_close = new FanartRemoveButton(this);
        m_close->setFixedSize(20, 20);
        m_close->setAutoRaise(true);
        m_close->setToolTip(QStringLiteral("删除"));
        m_close->setAccessibleName(QStringLiteral("删除剧照"));
        m_close->setStyleSheet(QStringLiteral(
            "QToolButton { background: rgba(0,0,0,0.34); border: none;"
            " border-radius: 10px; }"
            "QToolButton:hover, QToolButton:focus { background: rgba(0,0,0,0.65); }"
            "QToolButton:pressed { background: rgba(0,0,0,0.8); }"));
        connect(m_close, &QToolButton::clicked, this, [this] {
            if (m_removeRequested) m_removeRequested();
        });
        m_close->hide();
        applyStyle();
    }

    void setThumbnail(const QString &path, bool urlButNoImage)
    {
        m_label->clear();
        const QPixmap pixmap(path);
        if (!pixmap.isNull())
        {
            m_label->setPixmap(pixmap.scaled(fanartThumbSide, fanartThumbSide,
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
        }
        else
        {
            m_label->setText(urlButNoImage ? QStringLiteral("图片未下载") : QStringLiteral("-"));
            if (urlButNoImage) m_label->setStyleSheet(QStringLiteral("font-size: 8pt;"));
        }
    }

    void setAddPlaceholder()
    {
        m_isAddPlaceholder = true;
        m_label->setText(QStringLiteral("+"));
        m_label->setStyleSheet(QStringLiteral("color: #8b909a; font-size: 28px; font-weight: 300;"));
        m_close->hide();
        applyStyle();
    }

    void setEditMode(bool enabled)
    {
        m_close->setVisible(enabled && !m_isAddPlaceholder);
        if (enabled) m_close->raise();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        if (!m_isAddPlaceholder)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#9aa0a6"), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        const QRectF border = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(border, 4, 4);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        m_close->move(width() - m_close->width() - 2, 2);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        m_pressPosition = event->position();
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->position() - m_pressPosition).manhattanLength() > QApplication::startDragDistance())
            m_pressed = false;
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool clicked = m_pressed && event->button() == Qt::LeftButton &&
                             rect().contains(event->position().toPoint());
        m_pressed = false;
        QFrame::mouseReleaseEvent(event);
        if (clicked && m_clicked)
            m_clicked();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        event->accept();
    }

private:
    void applyStyle()
    {
        // Paint the outline without changing the frame's contents rect: a QSS
        // border consumes layout space and displaces the fixed-size thumbnail.
        update();
    }

    QLabel *m_label = nullptr;
    QToolButton *m_close = nullptr;
    QPointF m_pressPosition;
    std::function<void()> m_clicked;
    std::function<void()> m_removeRequested;
    bool m_pressed = false;
    bool m_isAddPlaceholder = false;
};
} // namespace

FanartStripWidget::FanartStripWidget(QString fanartDirectory, QString legacyCoverDirectory,
                                     QUrl imageFetchEndpoint, QWidget *parent)
    : QWidget(parent), m_fanartDirectory(QDir::cleanPath(std::move(fanartDirectory))),
      m_legacyCoverDirectory(QDir::cleanPath(std::move(legacyCoverDirectory)))
{
    qRegisterMetaType<QList<FanartEntry>>();
    setObjectName(QStringLiteral("FanartStripWidget"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::ClickFocus);
    auto *root = new QVBoxLayout(this);
    // Reserve the warning frame so the scroll area's viewport cannot cover it.
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(4);

    auto *scroll = new FanartHorizontalScrollArea(this);
    scroll->setObjectName(QStringLiteral("FanartScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(fanartThumbCell + 8);
    auto *strip = new QWidget(scroll);
    strip->setObjectName(QStringLiteral("FanartStripContent"));
    m_stripLayout = new QHBoxLayout(strip);
    m_stripLayout->setContentsMargins(4, 4, 4, 4);
    m_stripLayout->setSpacing(8);
    m_stripLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    scroll->setWidget(strip);
    root->addWidget(scroll, 1);

    m_imageFetch = new ImageFetchService(std::move(imageFetchEndpoint), this);
    connect(m_imageFetch, &ImageFetchService::requestFinished, this,
            [this](quint64 requestId, bool succeeded, const QString &destinationPath,
                   const QString &errorMessage)
            {
                if (requestId != m_activeRequestId)
                    return;
                const int index = m_activeDownloadIndex;
                m_activeRequestId = 0;
                m_activeDownloadIndex = -1;
                if (succeeded && index >= 0 && index < m_entries.size())
                {
                    m_entries[index].file = QFileInfo(destinationPath).fileName();
                    m_entries[index].localPath.clear();
                    rebuild();
                    emit fanartChanged(m_entries);
                }
                else
                {
                    rebuild();
                    if (!errorMessage.isEmpty())
                        emit imageRejected(errorMessage);
                }
                // A preview may start the newly selected image from fanartChanged.
                emit downloadStateChanged(downloadInProgress());
            });
    rebuild();
}

void FanartStripWidget::setEntries(const QList<FanartEntry> &entries)
{
    if (downloadInProgress())
    {
        const quint64 requestId = m_activeRequestId;
        m_activeRequestId = 0;
        m_activeDownloadIndex = -1;
        m_imageFetch->cancel(requestId);
        emit downloadStateChanged(false);
    }
    m_entries = entries;
    rebuild();
}

QList<FanartEntry> FanartStripWidget::entries() const
{
    return m_entries;
}

void FanartStripWidget::setUrlList(const QStringList &urls)
{
    if (downloadInProgress())
    {
        const quint64 requestId = m_activeRequestId;
        m_activeRequestId = 0;
        m_activeDownloadIndex = -1;
        m_imageFetch->cancel(requestId);
        emit downloadStateChanged(false);
    }
    QList<FanartEntry> replacement;
    for (const QString &value : urls)
    {
        const QString url = value.trimmed();
        if (!url.isEmpty())
            replacement.append({url, {}, {}});
    }
    QList<QString> oldUrls;
    for (const FanartEntry &entry : std::as_const(m_entries))
        oldUrls.append(entry.url.trimmed());
    QList<QString> newUrls;
    for (const FanartEntry &entry : std::as_const(replacement))
        newUrls.append(entry.url);
    if (oldUrls == newUrls)
        return;
    m_entries = replacement;
    rebuild();
    emit fanartChanged(m_entries);
}

void FanartStripWidget::setCanAdd(bool canAdd)
{
    m_canAdd = canAdd;
    if (m_mode == Mode::Edit)
        rebuild();
}

bool FanartStripWidget::canAdd() const noexcept
{
    return m_canAdd;
}

void FanartStripWidget::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    rebuild();
}

bool FanartStripWidget::addLocalImage(const QString &path, QString *errorMessage)
{
    if (downloadInProgress())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请等待剧照下载完成");
        return false;
    }
    if (!m_canAdd)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请先填写番号后再添加剧照");
        return false;
    }
    QImageReader reader(path);
    if (!reader.canRead())
    {
        const QString message = QStringLiteral("不是可读取的图片文件：%1").arg(path);
        if (errorMessage != nullptr)
            *errorMessage = message;
        emit imageRejected(message);
        return false;
    }
    m_entries.append({{}, {}, QDir::cleanPath(path)});
    rebuild();
    emit fanartChanged(m_entries);
    return true;
}

bool FanartStripWidget::addImageUrl(const QString &url, QString *errorMessage)
{
    const QUrl source(url.trimmed());
    QString error;
    if (m_mode != Mode::Edit || !m_canAdd)
        error = QStringLiteral("当前无法添加剧照");
    else if (downloadInProgress())
        error = QStringLiteral("请等待剧照下载完成");
    else if (!source.isValid() || source.host().isEmpty() ||
             (source.scheme() != QStringLiteral("http") && source.scheme() != QStringLiteral("https")))
        error = QStringLiteral("请输入有效的 HTTP 或 HTTPS 图片网址");
    if (!error.isEmpty())
    {
        if (errorMessage) *errorMessage = error;
        return false;
    }
    m_entries.append({url.trimmed(), {}, {}});
    rebuild();
    emit fanartChanged(m_entries);
    return true;
}

bool FanartStripWidget::removeEntry(int index)
{
    if (downloadInProgress() || index < 0 || index >= m_entries.size())
        return false;
    m_entries.removeAt(index);
    rebuild();
    emit fanartChanged(m_entries);
    return true;
}

bool FanartStripWidget::moveEntry(int from, int to)
{
    if (downloadInProgress() || from < 0 || from >= m_entries.size() || to < 0 ||
        to >= m_entries.size() || from == to)
        return false;
    m_entries.move(from, to);
    rebuild();
    emit fanartChanged(m_entries);
    return true;
}

bool FanartStripWidget::updateEntry(int index, const QString &url, const QString &file)
{
    if (downloadInProgress() || index < 0 || index >= m_entries.size())
        return false;
    m_entries[index].url = url.trimmed();
    m_entries[index].file = file.trimmed();
    rebuild();
    emit fanartChanged(m_entries);
    return true;
}

bool FanartStripWidget::downloadEntry(int index)
{
    if (downloadInProgress() || index < 0 || index >= m_entries.size())
        return false;
    const QUrl sourceUrl(m_entries.at(index).url.trimmed());
    const QString fileName = ImageFetchService::suggestedJpegFileName(sourceUrl);
    if (fileName.isEmpty())
    {
        emit imageRejected(QStringLiteral("剧照网址没有可用的文件名"));
        return false;
    }
    m_activeDownloadIndex = index;
    m_activeRequestId =
        m_imageFetch->fetchToJpeg(sourceUrl, QDir(m_fanartDirectory).filePath(fileName));
    rebuild();
    emit downloadStateChanged(true);
    return true;
}

bool FanartStripWidget::downloadInProgress() const noexcept
{
    return m_activeRequestId != 0;
}

bool FanartStripWidget::finalizedEntries(const QString &serialNumber,
                                         QList<FanartEntry> *finalEntries,
                                         QStringList *createdFiles, QString *errorMessage) const
{
    if (downloadInProgress())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请等待剧照下载完成后再保存作品");
        return false;
    }
    if (finalEntries == nullptr || createdFiles == nullptr)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("未提供剧照保存结果");
        return false;
    }
    finalEntries->clear();
    createdFiles->clear();
    QString serialBase = serialNumber.trimmed().toLower();
    serialBase.remove(QChar('-'));
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        serialBase.replace(character, QChar('_'));
    if (serialBase.isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("番号不能为空");
        return false;
    }
    if (!QDir().mkpath(m_fanartDirectory))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建剧照目录：%1").arg(m_fanartDirectory);
        return false;
    }

    for (FanartEntry entry : m_entries)
    {
        entry.url = entry.url.trimmed();
        entry.file = entry.file.trimmed();
        entry.localPath = entry.localPath.trimmed();
        if (!entry.localPath.isEmpty())
        {
            const QString name =
                QStringLiteral("%1_fa_%2.jpg")
                    .arg(serialBase, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
            const QString targetPath = QDir(m_fanartDirectory).filePath(name);
            if (!saveAsJpeg(entry.localPath, targetPath, errorMessage))
            {
                for (const QString &created : std::as_const(*createdFiles))
                    QFile::remove(created);
                finalEntries->clear();
                createdFiles->clear();
                return false;
            }
            createdFiles->append(targetPath);
            entry.file = name;
            entry.localPath.clear();
        }
        if (!entry.url.isEmpty() || !entry.file.isEmpty())
            finalEntries->append(entry);
    }
    return true;
}

bool FanartStripWidget::parseJson(const QString &json, QList<FanartEntry> *entries,
                                  QString *errorMessage)
{
    if (entries == nullptr)
        return false;
    entries->clear();
    if (json.trimmed().isEmpty())
        return true;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        if (errorMessage != nullptr)
            *errorMessage =
                parseError.error == QJsonParseError::NoError
                    ? QStringLiteral("剧照 JSON 无效：根节点必须是数组")
                    : QStringLiteral("剧照 JSON 无效：%1").arg(parseError.errorString());
        return false;
    }
    for (const QJsonValue &value : document.array())
    {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const FanartEntry entry{object.value(QStringLiteral("url")).toString().trimmed(),
                                object.value(QStringLiteral("file")).toString().trimmed(),
                                {}};
        if (!entry.url.isEmpty() || !entry.file.isEmpty())
            entries->append(entry);
    }
    return true;
}

QString FanartStripWidget::toJson(const QList<FanartEntry> &entries)
{
    QJsonArray array;
    for (const FanartEntry &entry : entries)
    {
        const QString url = entry.url.trimmed();
        const QString file = entry.file.trimmed();
        if (url.isEmpty() && file.isEmpty())
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("url"), url);
        object.insert(QStringLiteral("file"), file);
        array.append(object);
    }
    return array.isEmpty() ? QString()
                           : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void FanartStripWidget::chooseImage()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("添加剧照"));
    dialog.resize(460, 160);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("图片网址："), &dialog));
    auto *url = new QLineEdit(&dialog);
    url->setPlaceholderText(QStringLiteral("https://…"));
    layout->addWidget(url);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QHBoxLayout;
    auto *local = new QPushButton(QStringLiteral("选择本地图片…"), &dialog);
    auto *add = new QPushButton(QStringLiteral("添加网址"), &dialog);
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    add->setDefault(true);
    local->setAutoDefault(false);
    cancel->setAutoDefault(false);
    buttons->addWidget(local);
    buttons->addStretch();
    buttons->addWidget(add);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, &dialog, [this, &dialog, url, error] {
        QString message;
        if (addImageUrl(url->text(), &message))
            dialog.accept();
        else
            error->setText(message);
    });
    connect(local, &QPushButton::clicked, &dialog, [this, &dialog, error] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("选择剧照"), {},
            QStringLiteral("图片文件 (*.jpg *.jpeg *.png *.webp *.bmp *.gif)"));
        if (path.isEmpty()) return;
        QString message;
        if (addLocalImage(path, &message))
            dialog.accept();
        else
            error->setText(message);
    });
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

void FanartStripWidget::showPreview(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;

    FanartPreviewDialog dialog(this);
    dialog.setObjectName(QStringLiteral("FanartPreviewDialog"));
    // The DVD overlay uses transparent QWidget backgrounds. A top-level
    // preview needs its own opaque surface instead of inheriting that rule.
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#FanartPreviewDialog { background-color: palette(window); color: palette(window-text); }"
        "QDialog#FanartPreviewDialog QLabel { color: palette(window-text); }"));
    dialog.setAutoFillBackground(true);
    dialog.setWindowTitle(QStringLiteral("Fanart"));
    dialog.resize(440, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *image = new FanartPreviewImage(&dialog);
    image->setObjectName(QStringLiteral("FanartPreviewImage"));
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(600, 600);
    image->setStyleSheet(QStringLiteral("QLabel { background-color: transparent; border: none; }"));
    image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(image, 1);

    auto *position = new QLabel(&dialog);
    position->setAlignment(Qt::AlignCenter);
    auto *navigation = new QHBoxLayout;
    auto *previous = new QPushButton(&dialog);
    auto *next = new QPushButton(&dialog);
    previous->setIcon(IconProvider::builtIn(QStringLiteral("arrow_left"), QSize(18, 18),
                                           palette().color(QPalette::ButtonText)));
    next->setIcon(IconProvider::builtIn(QStringLiteral("arrow_right"), QSize(18, 18),
                                       palette().color(QPalette::ButtonText)));
    previous->setIconSize(QSize(18, 18));
    next->setIconSize(QSize(18, 18));
    previous->setToolTip(QStringLiteral("上一张"));
    next->setToolTip(QStringLiteral("下一张"));
    previous->setObjectName(QStringLiteral("FanartPrevious"));
    next->setObjectName(QStringLiteral("FanartNext"));
    previous->setAutoDefault(false);
    next->setAutoDefault(false);
    navigation->addWidget(previous);
    navigation->addWidget(position, 1);
    navigation->addWidget(next);
    layout->addLayout(navigation);

    layout->addWidget(new QLabel(QStringLiteral("图片网址："), &dialog));
    auto *url = new QLineEdit(&dialog);
    url->setReadOnly(m_mode == Mode::Display);
    url->setPlaceholderText(QStringLiteral("图片网址"));
    auto *download = new QPushButton(QStringLiteral("下载"), &dialog);
    download->setIcon(IconProvider::builtIn(QStringLiteral("arrow_down_to_line"), QSize(18, 18),
                                           palette().color(QPalette::ButtonText)));
    download->setIconSize(QSize(18, 18));
    download->setAutoDefault(false);
    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(url, 1);
    urlRow->addWidget(download);
    layout->addLayout(urlRow);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *confirm = new QPushButton(QStringLiteral("确定"), &dialog);
    confirm->setObjectName(QStringLiteral("DesignButton"));
    confirm->setProperty("variant", QStringLiteral("primary"));
    confirm->setDefault(true);
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    actions->addWidget(confirm);
    actions->addWidget(cancel);
    if (m_mode == Mode::Display)
    {
        confirm->hide();
        confirm->setDefault(false);
        cancel->setText(QStringLiteral("关闭"));
        cancel->setDefault(true);
    }
    layout->addLayout(actions);

    int currentIndex = index;
    QStringList draftUrls;
    for (const auto &entry : std::as_const(m_entries))
        draftUrls.append(entry.url);
    connect(url, &QLineEdit::textEdited, &dialog, [&draftUrls, &currentIndex](const QString &text) {
        draftUrls[currentIndex] = text.trimmed();
    });
    QSet<int> autoDownloadAttempted;
    const auto refresh = [this, &currentIndex, &draftUrls, &autoDownloadAttempted, image, position,
                          previous, next, url, download] {
        if (currentIndex < 0 || currentIndex >= m_entries.size())
            return;
        const FanartEntry &entry = m_entries.at(currentIndex);
        const QString path = resolvedPath(entry);
        const QPixmap pixmap(path);
        image->showImage(pixmap, draftUrls.at(currentIndex).isEmpty()
                                    ? QStringLiteral("无预览")
                                    : m_activeDownloadIndex == currentIndex
                                          ? QStringLiteral("图片未下载，正在下载…")
                                          : QStringLiteral("图片未下载"));
        position->setText(QStringLiteral("%1 / %2").arg(currentIndex + 1).arg(m_entries.size()));
        previous->setEnabled(m_entries.size() > 1);
        next->setEnabled(m_entries.size() > 1);
        url->setText(draftUrls.at(currentIndex));
        download->setText(downloadInProgress() ? QStringLiteral("下载中…") : QStringLiteral("下载"));
        download->setEnabled(!draftUrls.at(currentIndex).isEmpty() && !downloadInProgress());

        // Python's FanartEditDialog downloads an unresolved HTTP(S) entry as soon
        // as the enlarged preview is opened or its navigation changes.
        if (path.isEmpty() && !draftUrls.at(currentIndex).isEmpty() && !downloadInProgress() &&
            !autoDownloadAttempted.contains(currentIndex))
        {
            autoDownloadAttempted.insert(currentIndex);
            m_entries[currentIndex].url = draftUrls.at(currentIndex);
            downloadEntry(currentIndex);
        }
    };

    dialog.navigate = [this, &currentIndex, refresh](int step) {
        if (m_entries.size() <= 1)
            return;
        currentIndex = (currentIndex + step + m_entries.size()) % m_entries.size();
        refresh();
    };
    connect(previous, &QPushButton::clicked, &dialog, [&dialog] { dialog.navigate(-1); });
    connect(next, &QPushButton::clicked, &dialog, [&dialog] { dialog.navigate(1); });
    connect(download, &QPushButton::clicked, &dialog,
            [this, &currentIndex, &draftUrls, &autoDownloadAttempted, refresh] {
        if (!downloadInProgress())
        {
            m_entries[currentIndex].url = draftUrls.at(currentIndex);
            autoDownloadAttempted.insert(currentIndex);
            downloadEntry(currentIndex);
        }
        refresh();
    });
    connect(this, &FanartStripWidget::fanartChanged, &dialog, refresh);
    connect(this, &FanartStripWidget::downloadStateChanged, &dialog,
            [refresh](bool) { refresh(); });
    connect(confirm, &QPushButton::clicked, &dialog, [this, &draftUrls, &dialog] {
        if (m_mode == Mode::Edit)
        {
            for (int i = 0; i < m_entries.size() && i < draftUrls.size(); ++i)
                m_entries[i].url = draftUrls.at(i);
            rebuild();
            emit fanartChanged(m_entries);
        }
        dialog.accept();
    });
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    for (auto *widget : dialog.findChildren<QWidget *>())
        widget->installEventFilter(&dialog);
    refresh();
    dialog.exec();
}

void FanartStripWidget::rebuild()
{
    while (QLayoutItem *item = m_stripLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    for (int index = 0; index < m_entries.size(); ++index)
    {
        const FanartEntry &entry = m_entries.at(index);
        auto *cell = new FanartThumbCell(
            [this, index] { QTimer::singleShot(0, this, [this, index] { showPreview(index); }); },
            [this, index] { removeEntry(index); }, this);
        cell->setObjectName(QStringLiteral("FanartCell"));
        cell->setToolTip(entry.url.isEmpty() ? entry.file : entry.url);
        cell->setThumbnail(resolvedPath(entry), !entry.url.isEmpty());
        cell->setEditMode(m_mode == Mode::Edit);
        m_stripLayout->addWidget(cell);
    }
    if (m_mode == Mode::Edit)
    {
        auto *addCell = new FanartThumbCell(
            [this] { chooseImage(); }, {}, this);
        addCell->setObjectName(QStringLiteral("FanartAddPlaceholder"));
        addCell->setAddPlaceholder();
        addCell->setEditMode(m_mode == Mode::Edit);
        addCell->setEnabled(m_canAdd && !downloadInProgress());
        addCell->setToolTip(QStringLiteral("点击添加图片网址或本地图片"));
        m_stripLayout->addWidget(addCell);
    }
    m_stripLayout->addStretch();
}

QString FanartStripWidget::resolvedPath(const FanartEntry &entry) const
{
    if (!entry.localPath.isEmpty() && QFileInfo::exists(entry.localPath))
        return entry.localPath;
    if (entry.file.isEmpty())
        return {};
    const QString fanartPath = QDir(m_fanartDirectory).filePath(entry.file);
    if (QFileInfo::exists(fanartPath))
        return fanartPath;
    const QString legacyPath = QDir(m_legacyCoverDirectory).filePath(entry.file);
    return QFileInfo::exists(legacyPath) ? legacyPath : QString();
}

bool FanartStripWidget::saveAsJpeg(const QString &sourcePath, const QString &targetPath,
                                   QString *errorMessage)
{
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法读取剧照：%1").arg(reader.errorString());
        return false;
    }
    if (image.hasAlphaChannel())
    {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
        image = flattened;
    }
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "JPEG", 92) || !output.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = output.errorString().isEmpty() ? QStringLiteral("剧照写入失败")
                                                           : output.errorString();
        return false;
    }
    return true;
}

} // namespace darkeye
#include <QApplication>
