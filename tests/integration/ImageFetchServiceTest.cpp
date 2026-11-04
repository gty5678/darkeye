#include "services/ImageFetchService.h"
#include "ui/components/FanartStripWidget.h"

#include <QBuffer>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace
{

class FakeImageProxy final : public QObject
{
public:
    explicit FakeImageProxy(QByteArray responseBody, bool respond = true, int delayMs = 0)
        : m_responseBody(std::move(responseBody)), m_respond(respond), m_delayMs(delayMs)
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                [this]
                {
                    QTcpSocket *socket = m_server.nextPendingConnection();
                    connect(
                        socket, &QTcpSocket::readyRead, this,
                        [this, socket]
                        {
                            m_request = socket->property("request").toByteArray() + socket->readAll();
                            socket->setProperty("request", m_request);
                            const qsizetype headerEnd = m_request.indexOf("\r\n\r\n");
                            if (headerEnd < 0)
                                return;
                            qsizetype contentLength = 0;
                            const QList<QByteArray> headers = m_request.left(headerEnd).split('\n');
                            for (QByteArray header : headers)
                            {
                                header = header.trimmed();
                                if (header.toLower().startsWith("content-length:"))
                                    contentLength = header.mid(sizeof("content-length:") - 1)
                                                        .trimmed()
                                                        .toLongLong();
                            }
                            if (m_request.size() - headerEnd - 4 < contentLength || !m_respond)
                                return;
                            const QByteArray response =
                                QByteArrayLiteral(
                                    "HTTP/1.1 200 OK\r\nContent-Type: "
                                    "application/json\r\nConnection: close\r\nContent-Length: ") +
                                QByteArray::number(m_responseBody.size()) +
                                QByteArrayLiteral("\r\n\r\n") + m_responseBody;
                            QTimer::singleShot(m_delayMs, socket, [socket, response] {
                                socket->write(response);
                                socket->disconnectFromHost();
                            });
                        });
                });
        const bool listening = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(listening);
    }

    [[nodiscard]] QUrl endpoint() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/image").arg(m_server.serverPort()));
    }

    [[nodiscard]] QByteArray requestBody() const
    {
        const qsizetype separator = m_request.indexOf("\r\n\r\n");
        return separator < 0 ? QByteArray() : m_request.mid(separator + 4);
    }

    [[nodiscard]] QByteArray requestLine() const
    {
        return m_request.left(m_request.indexOf("\r\n"));
    }

private:
    QTcpServer m_server;
    QByteArray m_responseBody;
    QByteArray m_request;
    bool m_respond = true;
    int m_delayMs = 0;
};

QByteArray pngPayload()
{
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(QColor(20, 80, 160, 120));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray proxyResponse(bool success, const QByteArray &image = {}, const QString &message = {})
{
    QJsonObject object{{QStringLiteral("success"), success}};
    if (!image.isEmpty())
        object.insert(QStringLiteral("image"), QString::fromLatin1(image.toBase64()));
    if (!message.isEmpty())
        object.insert(QStringLiteral("message"), message);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class ImageFetchServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void downloadsThroughProxyAsJpeg();
    void reportsProxyFailure();
    void preservesExistingFileForInvalidImage();
    void timesOutStalledRequest();
    void suggestsSafeJpegFileName();
    void updatesFanartEntryAfterDownload();
    void displayPreviewPublishesDownloadWithoutConfirmation();
    void changingEntriesAbandonsOldDownload();
    void previewNavigatesWhileDownloading();
};

void ImageFetchServiceTest::downloadsThroughProxyAsJpeg()
{
    FakeImageProxy proxy(proxyResponse(true, pngPayload()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("sample.jpg"));
    darkeye::ImageFetchService service(proxy.endpoint());
    QSignalSpy finished(&service, &darkeye::ImageFetchService::requestFinished);

    const QUrl source(QStringLiteral("https://images.example.test/a%20b.png?size=large"));
    const quint64 requestId = service.fetchToJpeg(source, destination);
    QVERIFY(finished.wait(2000));
    QCOMPARE(finished.count(), 1);
    const QList<QVariant> arguments = finished.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), requestId);
    QVERIFY(arguments.at(1).toBool());
    QCOMPARE(arguments.at(2).toString(), destination);
    QVERIFY(arguments.at(3).toString().isEmpty());
    QVERIFY(proxy.requestLine().startsWith("POST /api/v1/image HTTP/1.1"));
    const QJsonObject request = QJsonDocument::fromJson(proxy.requestBody()).object();
    QCOMPARE(request.value(QStringLiteral("url")).toString(), source.toString());
    QImage saved(destination);
    QVERIFY(!saved.isNull());
    QCOMPARE(saved.size(), QSize(3, 2));
    QCOMPARE(QFileInfo(destination).suffix().toLower(), QStringLiteral("jpg"));
}

void ImageFetchServiceTest::reportsProxyFailure()
{
    FakeImageProxy proxy(proxyResponse(false, {}, QStringLiteral("上游拒绝访问")));
    QTemporaryDir directory;
    darkeye::ImageFetchService service(proxy.endpoint());
    QSignalSpy finished(&service, &darkeye::ImageFetchService::requestFinished);

    QVERIFY(service.fetchToJpeg(QUrl(QStringLiteral("https://example.test/fanart.jpg")),
                                directory.filePath(QStringLiteral("fanart.jpg"))) > 0);
    QVERIFY(finished.wait(2000));
    QVERIFY(!finished.first().at(1).toBool());
    QCOMPARE(finished.first().at(3).toString(), QStringLiteral("上游拒绝访问"));
}

void ImageFetchServiceTest::preservesExistingFileForInvalidImage()
{
    const QByteArray invalidImage("this is not an image");
    FakeImageProxy proxy(proxyResponse(true, invalidImage));
    QTemporaryDir directory;
    const QString destination = directory.filePath(QStringLiteral("existing.jpg"));
    QImage original(2, 2, QImage::Format_RGB32);
    original.fill(Qt::red);
    QVERIFY(original.save(destination, "JPEG"));
    QFile beforeFile(destination);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();
    darkeye::ImageFetchService service(proxy.endpoint());
    QSignalSpy finished(&service, &darkeye::ImageFetchService::requestFinished);

    QVERIFY(service.fetchToJpeg(QUrl(QStringLiteral("https://example.test/invalid.jpg")),
                                destination) > 0);
    QVERIFY(finished.wait(2000));
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("不是可读取")));
    QFile afterFile(destination);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);
}

void ImageFetchServiceTest::timesOutStalledRequest()
{
    FakeImageProxy proxy({}, false);
    QTemporaryDir directory;
    darkeye::ImageFetchService service(proxy.endpoint());
    service.setTimeoutMilliseconds(30);
    QSignalSpy finished(&service, &darkeye::ImageFetchService::requestFinished);

    QVERIFY(service.fetchToJpeg(QUrl(QStringLiteral("https://example.test/slow.jpg")),
                                directory.filePath(QStringLiteral("slow.jpg"))) > 0);
    QVERIFY(finished.wait(2000));
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("超时")));
    QVERIFY(!service.isBusy());
}

void ImageFetchServiceTest::suggestsSafeJpegFileName()
{
    QCOMPARE(darkeye::ImageFetchService::suggestedJpegFileName(
                 QUrl(QStringLiteral("https://example.test/path/my%20fanart.webp?size=large"))),
             QStringLiteral("my fanart.jpg"));
    QCOMPARE(darkeye::ImageFetchService::suggestedJpegFileName(
                 QUrl(QStringLiteral("https://example.test/path/CON%3Acover.png"))),
             QStringLiteral("CON_cover.jpg"));
    QVERIFY(darkeye::ImageFetchService::suggestedJpegFileName(
                QUrl(QStringLiteral("https://example.test/")))
                .isEmpty());
}

void ImageFetchServiceTest::updatesFanartEntryAfterDownload()
{
    FakeImageProxy proxy(proxyResponse(true, pngPayload()));
    QTemporaryDir directory;
    darkeye::FanartStripWidget strip(directory.path(), {}, proxy.endpoint());
    strip.setEntries({{QStringLiteral("https://images.example.test/remote.png"), {}, {}}});
    QSignalSpy changed(&strip, &darkeye::FanartStripWidget::fanartChanged);
    QSignalSpy downloadState(&strip, &darkeye::FanartStripWidget::downloadStateChanged);

    QVERIFY(strip.downloadEntry(0));
    QVERIFY(strip.downloadInProgress());
    QVERIFY(changed.wait(2000));
    QVERIFY(!strip.downloadInProgress());
    QCOMPARE(strip.entries().at(0).file, QStringLiteral("remote.jpg"));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("remote.jpg"))));
    QCOMPARE(downloadState.count(), 2);
    QVERIFY(downloadState.at(0).at(0).toBool());
    QVERIFY(!downloadState.at(1).at(0).toBool());
}

void ImageFetchServiceTest::changingEntriesAbandonsOldDownload()
{
    FakeImageProxy proxy({}, false);
    QTemporaryDir directory;
    darkeye::FanartStripWidget strip(directory.path(), {}, proxy.endpoint());
    strip.setEntries({{QStringLiteral("https://images.example.test/old.png"), {}, {}}});
    QSignalSpy rejected(&strip, &darkeye::FanartStripWidget::imageRejected);

    QVERIFY(strip.downloadEntry(0));
    strip.setEntries({{QStringLiteral("https://images.example.test/new.png"), {}, {}}});
    QVERIFY(!strip.downloadInProgress());
    QTest::qWait(100);
    QCOMPARE(strip.entries().size(), 1);
    QCOMPARE(strip.entries().at(0).url, QStringLiteral("https://images.example.test/new.png"));
    QCOMPARE(rejected.count(), 0);
}

void ImageFetchServiceTest::displayPreviewPublishesDownloadWithoutConfirmation()
{
    FakeImageProxy proxy(proxyResponse(true, pngPayload()), true, 50);
    QTemporaryDir directory;
    darkeye::FanartStripWidget strip(directory.path(), {}, proxy.endpoint());
    strip.setMode(darkeye::FanartStripWidget::Mode::Display);
    strip.setStyleSheet(QStringLiteral("QWidget { background: transparent; } QLabel { color: #354052; }"));
    strip.setEntries({{QStringLiteral("https://images.example.test/scene.png"), {}, {}}});
    strip.show();
    QSignalSpy changed(&strip, &darkeye::FanartStripWidget::fanartChanged);
    QString persistedJson;
    connect(&strip, &darkeye::FanartStripWidget::fanartChanged, &strip,
            [&](const QList<darkeye::FanartEntry> &entries) {
                persistedJson = darkeye::FanartStripWidget::toJson(entries);
            });
    bool checked = false;
    QTimer::singleShot(5000, &strip, [&] {
        if (auto *dialog = strip.findChild<QDialog *>(QStringLiteral("FanartPreviewDialog")))
            dialog->reject();
    });
    connect(&strip, &darkeye::FanartStripWidget::fanartChanged, &strip, [&] {
        QTimer::singleShot(50, &strip, [&] {
            auto *dialog = strip.findChild<QDialog *>(QStringLiteral("FanartPreviewDialog"));
            QVERIFY(dialog);
            QCOMPARE(changed.count(), 1);
            QVERIFY(dialog->isVisible());
            auto *image = dialog->findChild<QLabel *>(QStringLiteral("FanartPreviewImage"));
            QVERIFY(image);
            QVERIFY(!image->pixmap().isNull());
            QVERIFY(!strip.entries().first().file.isEmpty());
            QVERIFY(QFile::exists(directory.filePath(strip.entries().first().file)));
            QVERIFY(persistedJson.contains(strip.entries().first().file));
            for (auto *button : dialog->findChildren<QPushButton *>())
                QVERIFY(!button->isVisible() || (button->text() != QStringLiteral("确定") &&
                                                 button->text() != QStringLiteral("取消")));
            checked = true;
            dialog->reject();
        });
    });
    auto *cell = strip.findChild<QWidget *>(QStringLiteral("FanartCell"));
    QVERIFY(cell);
    QTest::mouseClick(cell, Qt::LeftButton);
    QTRY_VERIFY(checked);
    QCOMPARE(changed.count(), 1);
}

void ImageFetchServiceTest::previewNavigatesWhileDownloading()
{
    FakeImageProxy proxy(proxyResponse(true, pngPayload()), true, 250);
    QTemporaryDir directory;
    const QString localPath = directory.filePath(QStringLiteral("local.png"));
    QImage localImage(40, 20, QImage::Format_RGB32);
    localImage.fill(Qt::red);
    QVERIFY(localImage.save(localPath));
    darkeye::FanartStripWidget strip(directory.path(), {}, proxy.endpoint());
    strip.setEntries({{QStringLiteral("https://images.example.test/first.png"), {}, {}},
                      {{}, {}, localPath},
                      {QStringLiteral("https://images.example.test/third.png"), {}, {}}});
    strip.show();
    bool checked = false;
    QTimer::singleShot(5000, &strip, [&strip] {
        if (auto *dialog = strip.findChild<QDialog *>(QStringLiteral("FanartPreviewDialog")))
            dialog->reject();
    });
    QTimer::singleShot(50, &strip, [&] {
        auto *dialog = strip.findChild<QDialog *>(QStringLiteral("FanartPreviewDialog"));
        QVERIFY(dialog);
        QVERIFY(strip.downloadInProgress());
        auto *previous = dialog->findChild<QPushButton *>(QStringLiteral("FanartPrevious"));
        auto *next = dialog->findChild<QPushButton *>(QStringLiteral("FanartNext"));
        auto *image = dialog->findChild<QLabel *>(QStringLiteral("FanartPreviewImage"));
        auto *url = dialog->findChild<QLineEdit *>();
        QVERIFY(previous && next && image && url);
        QVERIFY(previous->isEnabled());
        QVERIFY(next->isEnabled());
        next->click();
        QVERIFY(!image->pixmap().isNull());
        const QSize beforeResize = image->pixmap().size();
        dialog->resize(dialog->width() + 120, dialog->height() + 120);
        QTRY_VERIFY(image->pixmap().width() > beforeResize.width());
        url->setFocus();
        QTest::keyClick(url, Qt::Key_Right);
        QCOMPARE(url->text(), QString());
        QTest::keyClicks(url, "https://images.example.test/local.png");
        next->setFocus();
        QTest::keyClick(next, Qt::Key_Right);
        QCOMPARE(url->text(), QStringLiteral("https://images.example.test/third.png"));
        // Completion belongs to the original entry; the selected remote entry
        // starts once it finishes, without moving the preview back.
        QTRY_VERIFY(!strip.entries().at(0).file.isEmpty());
        QCOMPARE(url->text(), QStringLiteral("https://images.example.test/third.png"));
        QTRY_VERIFY(!strip.entries().at(2).file.isEmpty());
        QTRY_VERIFY(!strip.downloadInProgress());
        QVERIFY(!image->pixmap().isNull());
        next->click();
        QCOMPARE(url->text(), QStringLiteral("https://images.example.test/first.png"));
        previous->click();
        QCOMPARE(url->text(), QStringLiteral("https://images.example.test/third.png"));
        previous->click();
        QCOMPARE(url->text(), QStringLiteral("https://images.example.test/local.png"));
        checked = true;
        auto *confirm = dialog->findChild<QPushButton *>(QStringLiteral("DesignButton"));
        QVERIFY(confirm);
        confirm->click();
    });
    auto *cell = strip.findChild<QWidget *>(QStringLiteral("FanartCell"));
    QVERIFY(cell);
    QTest::mouseClick(cell, Qt::LeftButton);
    QTRY_VERIFY(checked);
    QCOMPARE(strip.entries().at(1).url, QStringLiteral("https://images.example.test/local.png"));
}

QTEST_MAIN(ImageFetchServiceTest)
#include "ImageFetchServiceTest.moc"


