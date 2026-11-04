#include "darkeye_ui/theme/ThemeService.h"
#include "ui/components/AsyncImageLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/theme/IconProvider.h"
#include "ui/components/CrawlerFieldSelector.h"
#include "ui/components/FanartStripWidget.h"
#include "ui/components/ImageDropWidget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class MediaComponentTest final : public QObject
{
    Q_OBJECT

private slots:
    void iconProviderRendersKnownIcon();
    void iconButtonRefreshesWithTheme();
    void asyncImageLoadsOffThread();
    void asyncImageCanDeferOffscreenWork();
    void clearedImageRejectsStaleResult();
    void imageDropPurposeIsConfigurable();
    void imageDropDoesNotRewriteManagedSource();
    void imageDropRendersDashedPlaceholderBorder();
    void imageDropRetainsBrowserTemporaryFileAfterResize();
    void imageDropAcceptsBrowserBitmapData();
    void imageDropComparesOriginalPixels_data();
    void imageDropComparesOriginalPixels();
    void imageDropRecognizesPreviouslyReencodedJpeg();
    void imageDropPreservesOriginalJpegBytes();
    void fanartPreservesJsonOrderAndRemoteState();
    void fanartFinalizesPendingLocalImages();
    void fanartAddIsAccessibleWithoutLongPress();
    void fanartAddsImageUrl();
    void crawlerFieldsKeepPythonSelectionContract();
};

void MediaComponentTest::fanartAddsImageUrl()
{
    QTemporaryDir directory;
    darkeye::FanartStripWidget widget(directory.path());
    widget.setEntries({{QStringLiteral("https://example.invalid/existing.jpg"), {}, {}}});
    QSignalSpy changed(&widget, &darkeye::FanartStripWidget::fanartChanged);
    QString error;
    QVERIFY(!widget.addImageUrl(QStringLiteral("file:///scene.jpg"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!widget.addImageUrl(QStringLiteral("https://"), &error));
    QCOMPARE(widget.entries().size(), 1);
    QCOMPARE(changed.count(), 0);
    QVERIFY(widget.addImageUrl(QStringLiteral(" https://example.invalid/new.jpg "), &error));
    QCOMPARE(widget.entries().size(), 2);
    QCOMPARE(widget.entries().last().url, QStringLiteral("https://example.invalid/new.jpg"));
    QCOMPARE(changed.count(), 1);
    QList<darkeye::FanartEntry> saved;
    QVERIFY(darkeye::FanartStripWidget::parseJson(
        darkeye::FanartStripWidget::toJson(widget.entries()), &saved));
    QCOMPARE(saved, widget.entries());
    widget.setMode(darkeye::FanartStripWidget::Mode::Display);
    QVERIFY(!widget.addImageUrl(QStringLiteral("https://example.invalid/blocked.jpg"), &error));
    QCOMPARE(widget.entries().size(), 2);
}

void MediaComponentTest::fanartAddIsAccessibleWithoutLongPress()
{
    QTemporaryDir directory;
    darkeye::FanartStripWidget widget(directory.path());
    widget.resize(400, 160);
    widget.show();
    auto addPlaceholder = [&widget]() -> QWidget * {
        for (auto *child : widget.findChildren<QWidget *>(QStringLiteral("FanartAddPlaceholder")))
            if (child->isVisible())
                return child;
        return nullptr;
    };
    QTRY_VERIFY(addPlaceholder() != nullptr);
    QVERIFY(addPlaceholder()->isEnabled());
    widget.setEntries({{QStringLiteral("https://example.invalid/scene.jpg"), {}, {}}});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(addPlaceholder() != nullptr);
    auto *remove = widget.findChild<QToolButton *>();
    QVERIFY(remove != nullptr);
    QVERIFY(remove->isVisible());
    QTest::mouseClick(remove, Qt::LeftButton);
    QVERIFY(widget.entries().isEmpty());
    widget.setEntries({{QStringLiteral("https://example.invalid/scene.jpg"), {}, {}}});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    widget.setCanAdd(false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(addPlaceholder() != nullptr);
    QVERIFY(!addPlaceholder()->isEnabled());
    widget.setMode(darkeye::FanartStripWidget::Mode::Display);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(addPlaceholder() == nullptr);
    QCoreApplication::processEvents();
    for (auto *button : widget.findChildren<QToolButton *>())
        QVERIFY(!button->isVisible());
    widget.setMode(darkeye::FanartStripWidget::Mode::Edit);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(addPlaceholder() != nullptr);
    QTRY_VERIFY(widget.findChild<QToolButton *>()->isVisible());
}

void MediaComponentTest::imageDropRetainsBrowserTemporaryFileAfterResize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString browserPath = directory.filePath(QStringLiteral("browser-cover.png"));
    QImage source(600, 400, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(browserPath));

    darkeye::ImageDropWidget widget(directory.path());
    widget.setPreviewAspectRatio(0.7);
    widget.setFitMode(darkeye::ImageFitMode::RightCover);
    widget.resize(280, 400);
    widget.show();
    auto *preview = widget.findChild<darkeye::AsyncImageLabel *>();
    QVERIFY(preview != nullptr);
    QSignalSpy loaded(preview, &darkeye::AsyncImageLabel::imageLoaded);
    QSignalSpy failed(preview, &darkeye::AsyncImageLabel::imageLoadFailed);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(browserPath)});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(widget.isDirty());
    QVERIFY(widget.resolvedImagePath() != browserPath);
    // Simulate the browser removing its file immediately after the drop.
    QVERIFY(QFile::remove(browserPath));
    QTRY_VERIFY_WITH_TIMEOUT(!preview->pixmap().isNull(), 3000);
    for (const QSize size : {QSize(560, 800), QSize(210, 300)})
    {
        loaded.clear();
        widget.resize(size);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 3000);
        QVERIFY(!preview->pixmap().isNull());
    }
    QCOMPARE(failed.count(), 0);
    QString stored;
    QString error;
    QVERIFY2(widget.persistAsJpeg(QStringLiteral("saved-cover"), &stored, &error),
             qPrintable(error));
    // Saving must retain the full original resolution, not the scaled preview.
    QCOMPARE(QImage(directory.filePath(stored)).size(), source.size());
}

void MediaComponentTest::imageDropAcceptsBrowserBitmapData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::ImageDropWidget widget(directory.path());
    widget.resize(240, 360);
    widget.show();
    QImage source(600, 400, QImage::Format_RGB32);
    source.fill(Qt::green);
    QMimeData mime;
    mime.setImageData(source);
    mime.setUrls({QUrl(QStringLiteral("https://example.test/cover.png"))});
    QSignalSpy changed(&widget, &darkeye::ImageDropWidget::imageChanged);
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(changed.count(), 1);
    QVERIFY(widget.isDirty());
    QCOMPARE(QImage(widget.resolvedImagePath()), source);
    auto *preview = widget.findChild<darkeye::AsyncImageLabel *>();
    QVERIFY(preview != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!preview->pixmap().isNull(), 3000);
    widget.clearImage();
    QVERIFY(widget.imagePath().isEmpty());
    QVERIFY(preview->pixmap().isNull());
}

void MediaComponentTest::imageDropComparesOriginalPixels_data()
{
    QTest::addColumn<QString>("change");
    QTest::addColumn<bool>("expectedDirty");
    QTest::newRow("same-pixels-different-format") << QStringLiteral("same") << false;
    QTest::newRow("single-channel-change") << QStringLiteral("pixel") << true;
    QTest::newRow("different-resolution") << QStringLiteral("size") << true;
    QTest::newRow("different-alpha") << QStringLiteral("alpha") << true;
}

void MediaComponentTest::imageDropComparesOriginalPixels()
{
    QFETCH(QString, change);
    QFETCH(bool, expectedDirty);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage original(160, 100, QImage::Format_RGB32);
    original.fill(QColor(20, 80, 160));
    const QString originalName = QStringLiteral("original.png");
    QVERIFY(original.save(directory.filePath(originalName)));
    QImage replacement = original;
    if (change == QStringLiteral("pixel"))
        replacement.setPixelColor(159, 99, QColor(21, 80, 160));
    else if (change == QStringLiteral("size"))
        replacement = original.scaled(80, 50);
    else if (change == QStringLiteral("alpha"))
    {
        replacement = original.convertToFormat(QImage::Format_ARGB32);
        replacement.setPixelColor(159, 99, QColor(20, 80, 160, 254));
    }
    const QString replacementPath = directory.filePath(
        change == QStringLiteral("same") ? QStringLiteral("replacement.bmp")
                                          : QStringLiteral("replacement.png"));
    QVERIFY(replacement.save(replacementPath));
    darkeye::ImageDropWidget widget(directory.path());
    widget.setImagePath(originalName);
    QVERIFY(widget.setPendingImagePath(replacementPath));
    QCOMPARE(widget.isDirty(), expectedDirty);
    if (!expectedDirty)
        QCOMPARE(widget.imagePath(), originalName);

    // Comparing to the saved original allows reverting a previous change.
    QVERIFY(widget.setPendingImagePath(directory.filePath(originalName)));
    QVERIFY(!widget.isDirty());
    QCOMPARE(widget.imagePath(), originalName);
    widget.clearImage();
    QVERIFY(widget.isDirty());
    QVERIFY(widget.setPendingImagePath(directory.filePath(originalName)));
    QVERIFY(!widget.isDirty());

    widget.setImagePath({});
    QVERIFY(widget.setPendingImagePath(replacementPath));
    QVERIFY(widget.isDirty());
    widget.clearImage();
    QVERIFY(!widget.isDirty());
}

void MediaComponentTest::imageDropRecognizesPreviouslyReencodedJpeg()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage source(180, 120, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor((x * 7 + y) % 256,
                                             (y * 11 + x) % 256, (x + y * 3) % 256));
    const QString browserPath = directory.filePath(QStringLiteral("browser.jpg"));
    QVERIFY(source.save(browserPath, "JPEG", 85));
    const QImage browserImage(browserPath);
    // Reproduce the former save path: decode the original, then encode at 92.
    const QString legacyName = QStringLiteral("legacy.jpg");
    QVERIFY(browserImage.save(directory.filePath(legacyName), "JPEG", 92));
    QVERIFY(QImage(directory.filePath(legacyName)) != browserImage);
    darkeye::ImageDropWidget widget(directory.path());
    widget.setImagePath(legacyName);
    QVERIFY(widget.setPendingImagePath(browserPath));
    QVERIFY(!widget.isDirty());
    QCOMPARE(widget.imagePath(), legacyName);

    QImage changed = browserImage;
    QPainter painter(&changed);
    painter.fillRect(QRect(0, 0, 30, 30), Qt::white);
    painter.end();
    const QString changedPath = directory.filePath(QStringLiteral("changed.png"));
    QVERIFY(changed.save(changedPath));
    QVERIFY(widget.setPendingImagePath(changedPath));
    QVERIFY(widget.isDirty());
}

void MediaComponentTest::imageDropPreservesOriginalJpegBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("browser.jpeg"));
    QImage image(160, 100, QImage::Format_RGB32);
    image.fill(QColor(20, 80, 160));
    QVERIFY(image.save(sourcePath, "JPEG", 80));
    darkeye::ImageDropWidget widget(directory.path());
    QVERIFY(widget.setPendingImagePath(sourcePath));
    QString relativePath;
    QString error;
    QVERIFY2(widget.persistAsJpeg(QStringLiteral("saved"), &relativePath, &error),
             qPrintable(error));
    QFile original(sourcePath);
    QFile saved(directory.filePath(relativePath));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), original.readAll());
}

void MediaComponentTest::iconProviderRendersKnownIcon()
{
    const QStringList names = {
        QStringLiteral("menu"),
        QStringLiteral("arrow_left"),
        QStringLiteral("arrow_right"),
        QStringLiteral("square_pen"),
        QStringLiteral("scroll_text"),
        QStringLiteral("check"),
        QStringLiteral("list_plus"),
        QStringLiteral("layout_panel_left"),
        QStringLiteral("circle_plus"),
        QStringLiteral("copy"),
        QStringLiteral("refresh_cw"),
        QStringLiteral("circle_question_mark"),
        QStringLiteral("settings"),
        QStringLiteral("brush_cleaning"),
        QStringLiteral("panel_left_close"),
        QStringLiteral("layout_grid"),
        QStringLiteral("layout_waterfall"),
        QStringLiteral("trash_2"),
        QStringLiteral("tv"),
    };
    for (const QString &name : names)
    {
        QVERIFY2(darkeye::IconProvider::contains(name), qPrintable(name));
        const QIcon rendered = darkeye::IconProvider::builtIn(
            name, QSize(24, 24), QColor(QStringLiteral("#c62828")), 1.0);
        QVERIFY2(!rendered.isNull(), qPrintable(name));
        QVERIFY2(!rendered.pixmap(QSize(24, 24)).isNull(), qPrintable(name));
    }
    QVERIFY(!darkeye::IconProvider::contains(QStringLiteral("missing")));
    const QIcon icon = darkeye::IconProvider::builtIn(QStringLiteral("search"), QSize(24, 24),
                                                      QColor(QStringLiteral("#c62828")), 1.0);
    QVERIFY(!icon.isNull());
    QVERIFY(!icon.pixmap(QSize(24, 24)).isNull());
}

void MediaComponentTest::iconButtonRefreshesWithTheme()
{
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::IconButton button(QStringLiteral("settings"), &themes);
    const QImage light = button.icon().pixmap(button.iconSize()).toImage();
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark));
    const QImage dark = button.icon().pixmap(button.iconSize()).toImage();
    QVERIFY(light != dark);
}

void MediaComponentTest::asyncImageLoadsOffThread()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cover.png"));
    QImage source(300, 200, QImage::Format_RGB32);
    source.fill(Qt::red);
    QVERIFY(source.save(path));

    darkeye::AsyncImageLabel label;
    label.resize(120, 80);
    QSignalSpy loaded(&label, &darkeye::AsyncImageLabel::imageLoaded);
    label.setFitMode(darkeye::ImageFitMode::Cover);
    label.setSource(path);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    QVERIFY(!label.pixmap().isNull());
    QCOMPARE(label.pixmap().size(), QSize(120, 80));
}

void MediaComponentTest::asyncImageCanDeferOffscreenWork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("deferred-cover.jpg"));
    QImage source(1200, 800, QImage::Format_RGB32);
    source.fill(Qt::green);
    QVERIFY(source.save(path));

    darkeye::AsyncImageLabel label;
    label.resize(120, 80);
    label.setDeferredLoading(true);
    QSignalSpy loaded(&label, &darkeye::AsyncImageLabel::imageLoaded);
    label.setSource(path);
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 0);
    QVERIFY(label.pixmap().isNull());

    label.startDeferredLoad(100);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
    QCOMPARE(label.pixmap().size(), QSize(120, 80));
}

void MediaComponentTest::clearedImageRejectsStaleResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large.png"));
    QImage source(2000, 2000, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    darkeye::AsyncImageLabel label;
    label.resize(300, 300);
    QSignalSpy loaded(&label, &darkeye::AsyncImageLabel::imageLoaded);
    label.setSource(path);
    label.clearSource();
    QTest::qWait(300);
    QCOMPARE(loaded.count(), 0);
    QVERIFY(label.pixmap().isNull());
    QCOMPARE(label.text(), QStringLiteral("无图片"));
}

void MediaComponentTest::imageDropPurposeIsConfigurable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::ImageDropWidget imageDrop(directory.path());
    imageDrop.setPurpose(QStringLiteral("作品封面"));
    QCOMPARE(imageDrop.purpose(), QStringLiteral("作品封面"));
}

void MediaComponentTest::imageDropDoesNotRewriteManagedSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fileName = QStringLiteral("42-已有头像.jpg");
    const QString path = directory.filePath(fileName);
    QImage source(48, 64, QImage::Format_RGB32);
    source.fill(QColor(60, 100, 140));
    QVERIFY(source.save(path, "JPEG", 61));

    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();

    darkeye::ImageDropWidget imageDrop(directory.path());
    imageDrop.setImagePath(fileName);
    QString storedPath;
    QString errorMessage;
    QVERIFY2(imageDrop.persistAsJpeg(fileName, &storedPath, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(storedPath, fileName);

    QFile persisted(path);
    QVERIFY(persisted.open(QIODevice::ReadOnly));
    QCOMPARE(persisted.readAll(), originalBytes);
}

void MediaComponentTest::imageDropRendersDashedPlaceholderBorder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::ImageDropWidget imageDrop(directory.path());
    imageDrop.resize(220, 260);
    imageDrop.show();
    QCoreApplication::processEvents();

    auto *preview = imageDrop.findChild<darkeye::AsyncImageLabel *>(
        QStringLiteral("ImageDropPreview"));
    QVERIFY(preview != nullptr);
    const QImage rendered = preview->grab().toImage();
    const QColor expectedBorder(QStringLiteral("#8a8a8a"));
    bool foundBorderPixel = false;
    for (int y = 0; y < rendered.height() && !foundBorderPixel; ++y)
    {
        for (int x = 0; x < rendered.width(); ++x)
        {
            if (rendered.pixelColor(x, y) == expectedBorder)
            {
                foundBorderPixel = true;
                break;
            }
        }
    }
    QVERIFY2(foundBorderPixel, "empty image drops must retain a dashed placeholder border");
}

void MediaComponentTest::fanartPreservesJsonOrderAndRemoteState()
{
    QList<darkeye::FanartEntry> entries;
    QString errorMessage;
    QVERIFY(darkeye::FanartStripWidget::parseJson(
        QStringLiteral("[{\"url\":\"https://example.invalid/一.jpg\",\"file\":\"one.jpg\"},"
                       "{\"url\":\"https://example.invalid/two.png\",\"file\":\"\"}]"),
        &entries, &errorMessage));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).file, QStringLiteral("one.jpg"));
    QCOMPARE(entries.at(1).url, QStringLiteral("https://example.invalid/two.png"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::FanartStripWidget strip(directory.path());
    strip.setEntries(entries);
    QSignalSpy changed(&strip, &darkeye::FanartStripWidget::fanartChanged);
    strip.setUrlList({QStringLiteral("https://example.invalid/一.jpg"),
                      QStringLiteral("https://example.invalid/two.png")});
    QCOMPARE(changed.count(), 0);
    QCOMPARE(strip.entries().at(0).file, QStringLiteral("one.jpg"));
    QVERIFY(strip.moveEntry(1, 0));
    QCOMPARE(changed.count(), 1);
    const QString serialized = darkeye::FanartStripWidget::toJson(strip.entries());
    QList<darkeye::FanartEntry> roundTrip;
    QVERIFY(darkeye::FanartStripWidget::parseJson(serialized, &roundTrip, &errorMessage));
    QCOMPARE(roundTrip, strip.entries());
    QVERIFY(
        !darkeye::FanartStripWidget::parseJson(QStringLiteral("{}"), &roundTrip, &errorMessage));
}

void MediaComponentTest::fanartFinalizesPendingLocalImages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.png"));
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(QColor(10, 20, 30, 120));
    QVERIFY(source.save(sourcePath));
    const QString fanartDirectory = directory.filePath(QStringLiteral("fanart"));
    darkeye::FanartStripWidget strip(fanartDirectory);
    QString errorMessage;
    QVERIFY(strip.addLocalImage(sourcePath, &errorMessage));
    strip.setUrlList({QStringLiteral("https://example.invalid/remote.jpg")});
    QVERIFY(strip.addLocalImage(sourcePath, &errorMessage));

    QList<darkeye::FanartEntry> finalized;
    QStringList createdFiles;
    QVERIFY(strip.finalizedEntries(QStringLiteral("ABC-123"), &finalized, &createdFiles,
                                   &errorMessage));
    QCOMPARE(finalized.size(), 2);
    QCOMPARE(finalized.at(0).url, QStringLiteral("https://example.invalid/remote.jpg"));
    QCOMPARE(finalized.at(1).localPath, QString());
    QVERIFY(finalized.at(1).file.startsWith(QStringLiteral("abc123_fa_")));
    QVERIFY(finalized.at(1).file.endsWith(QStringLiteral(".jpg")));
    QCOMPARE(createdFiles.size(), 1);
    QVERIFY(QFileInfo::exists(createdFiles.first()));
    QVERIFY(!QImage(createdFiles.first()).isNull());
}

void MediaComponentTest::crawlerFieldsKeepPythonSelectionContract()
{
    const QList<darkeye::CrawlerFieldDefinition> definitions =
        darkeye::CrawlerFieldSelector::availableFields();
    QCOMPARE(definitions.size(), 15);
    QCOMPARE(definitions.first().key, QStringLiteral("release_date"));
    QCOMPARE(definitions.first().label, QStringLiteral("发布日期"));
    QCOMPARE(definitions.last().key, QStringLiteral("fanart"));
    QCOMPARE(definitions.last().label, QStringLiteral("剧照"));

    darkeye::CrawlerFieldSelector selector;
    QVERIFY(selector.selectedFields().isEmpty());
    QSignalSpy changed(&selector, &darkeye::CrawlerFieldSelector::selectionChanged);
    selector.setSelectedFields(
        {QStringLiteral("cover"), QStringLiteral("fanart"), QStringLiteral("unknown")});
    QCOMPARE(selector.selectedFields(),
             QSet<QString>({QStringLiteral("cover"), QStringLiteral("fanart")}));
    QCOMPARE(changed.count(), 1);
    QVERIFY(selector.isFieldChecked(QStringLiteral("cover")));
    QVERIFY(!selector.setFieldChecked(QStringLiteral("unknown"), true));
    selector.invertSelection();
    QCOMPARE(selector.selectedFields().size(), 13);
    selector.selectAll();
    QCOMPARE(selector.selectedFields().size(), 15);
    selector.clearSelection();
    QVERIFY(selector.selectedFields().isEmpty());
}

QTEST_MAIN(MediaComponentTest)
#include "MediaComponentTest.moc"
