#include "ui/pages/AvPage.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "editor/markdown_editor_widget.h"
#include "editor/project_file_tree.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QMimeData>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

class AvPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void buildsDocumentTreeAndRendersMarkdown();
    void editsAndAutomaticallySavesMarkdown();
    void importsDroppedImagesInBothEditorModes();
    void followsDarkeyeThemeWhileEditing();
    void scrollsInsideChamferedDocumentPanel();
};

void AvPageTest::buildsDocumentTreeAndRendersMarkdown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("分类")));

    QFile first(directory.filePath(QStringLiteral("01-首页.md")));
    QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(first.write("# 首页\n\n进入[[./分类/详情.markdown|详情]]") > 0);
    first.close();

    QFile second(directory.filePath(QStringLiteral("分类/详情.markdown")));
    QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(second.write("## 详情\n\n正文") > 0);
    second.close();

    darkeye::AvPage page(directory.path());
    page.initialize();

    auto *tree = page.findChild<mdedit::ProjectFileTree *>();
    auto *browser = page.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
    QVERIFY(tree != nullptr);
    QVERIFY(browser != nullptr);
    auto *model = tree->fileModel();
    QCOMPARE(QFileInfo(model->filePath(tree->rootIndex())).canonicalFilePath(),
             QFileInfo(directory.path()).canonicalFilePath());
    QVERIFY(!tree->currentIndex().isValid());
    QTRY_COMPARE(model->rowCount(tree->rootIndex()), 2);
    QTRY_VERIFY(model->index(first.fileName()).isValid());
    tree->clicked(model->index(first.fileName()));
    QCOMPARE(model->filePath(tree->currentIndex()), first.fileName());
    QVERIFY(browser->toPlainText().contains(QStringLiteral("进入详情")));
    QTextCursor link = browser->document()->find(QStringLiteral("详情"));
    QVERIFY(link.charFormat().isAnchor());
    browser->anchorClicked(QUrl(link.charFormat().anchorHref()));
    QTRY_COMPARE(model->filePath(tree->currentIndex()), second.fileName());
    QVERIFY(tree->isExpanded(tree->currentIndex().parent()));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("正文")));

    QVERIFY(second.remove());
    auto *editor = page.findChild<MarkdownEditorWidget *>(QStringLiteral("AvWikiEditor"));
    QTRY_VERIFY(editor->isHidden());
}

void AvPageTest::editsAndAutomaticallySavesMarkdown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("首页.md"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(file.write("# 原始内容") > 0);
    file.close();

    darkeye::AvPage page(directory.path());
    page.initialize();
    auto *editor = page.findChild<MarkdownEditorWidget *>(QStringLiteral("AvWikiEditor"));
    QVERIFY(editor != nullptr);
    auto *tree = page.findChild<mdedit::ProjectFileTree *>();
    QVERIFY(tree != nullptr);
    QTRY_COMPARE(tree->fileModel()->rowCount(tree->rootIndex()), 1);
    tree->clicked(tree->fileModel()->index(0, 0, tree->rootIndex()));
    QVERIFY(editor->isLiveMode());
    QVERIFY(!editor->liveModeAction()->isEnabled());
    QCOMPARE(editor->markdown(), QStringLiteral("# 原始内容"));

    auto *liveEditor = editor->findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
    QVERIFY(liveEditor != nullptr);
    const QString expected = QStringLiteral("# 原始内容!");
    const int end = editor->markdown().size();
    editor->selectSource(end, end);
    QTest::keyClicks(liveEditor, "!");
    QCOMPARE(editor->markdown(), expected);
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        QFile saved(filePath);
        return saved.open(QIODevice::ReadOnly | QIODevice::Text)
               && QString::fromUtf8(saved.readAll()) == expected;
    }(), 2500);

    QVERIFY(liveEditor->toPlainText().contains(QStringLiteral("原始内容!")));

    page.resize(900, 600);
    page.show();
    QTRY_VERIFY(tree->width() >= 175 && tree->width() <= 200);
    auto *addFolder = page.findChild<QToolButton *>(QStringLiteral("addProjectFolderButton"));
    auto *addFile = page.findChild<QToolButton *>(QStringLiteral("newProjectFileButton"));
    QVERIFY(addFolder != nullptr);
    QVERIFY(addFile != nullptr);
    addFolder->click();
    auto *folderDialog = page.findChild<QInputDialog *>(QStringLiteral("newProjectFolderDialog"));
    QVERIFY(folderDialog != nullptr);
    folderDialog->setTextValue(QStringLiteral("新目录"));
    folderDialog->accept();

    const QString folder = directory.filePath(QStringLiteral("新目录"));
    QVERIFY(QFileInfo(folder).isDir());
    QTRY_COMPARE(tree->fileModel()->rowCount(tree->rootIndex()), 2);
    const auto folderIndex = tree->fileModel()->index(folder);
    QTRY_VERIFY(tree->visualRect(folderIndex).isValid());
    tree->setCurrentIndex(folderIndex);
    addFile->click();
    auto *fileDialog = page.findChild<QInputDialog *>(QStringLiteral("newMarkdownFileDialog"));
    QVERIFY(fileDialog != nullptr);
    fileDialog->setTextValue(QStringLiteral("新文档"));
    fileDialog->accept();
    const QString newFile = QDir(folder).filePath(QStringLiteral("新文档.md"));
    QTRY_COMPARE(tree->fileModel()->filePath(tree->currentIndex()), newFile);
    QVERIFY(QFileInfo(newFile).isFile());
    QVERIFY(editor->markdown().isEmpty());
}

void AvPageTest::importsDroppedImagesInBothEditorModes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("notes")));

    const QString imageName = QStringLiteral("样图 (1).png");
    const QString imagePath = directory.filePath(imageName);
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    const QString notes = directory.filePath(QStringLiteral("notes"));
    MarkdownEditorWidget editor;
    editor.resize(640, 480);
    editor.setMarkdown({}, QUrl::fromLocalFile(notes + '/'));
    editor.show();

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(imagePath)});
    for (const bool live : {true, false}) {
        editor.setLiveMode(live);
        auto *view = editor.findChild<QAbstractScrollArea *>(
            live ? QStringLiteral("markdownPreview") : QStringLiteral("markdownEditor"));
        QVERIFY(view != nullptr);
        const QPointF point(20, 20);
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        QVERIFY(drop.isAccepted());
    }

    const QDir resources(QDir(notes).filePath(QStringLiteral("resources")));
    for (const auto &name : {imageName, QStringLiteral("样图 (1)-2.png")}) {
        QFile imported(resources.filePath(name));
        QFile original(imagePath);
        QVERIFY(imported.open(QIODevice::ReadOnly));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(imported.readAll(), original.readAll());
        const QString reference = QStringLiteral("![](resources/%1)")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
        QVERIFY(editor.markdown().contains(reference));
    }
    QVERIFY(editor.document()->isModified());
}

void AvPageTest::followsDarkeyeThemeWhileEditing()
{
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("主题.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("# Theme\n\n[Reference](https://example.com)\n\n```text\nsample\n```\n") > 0);
    file.close();

    darkeye::AvPage page(directory.path(), themes);
    page.initialize();
    auto *editor = page.findChild<MarkdownEditorWidget *>();
    auto *tree = page.findChild<mdedit::ProjectFileTree *>();
    QVERIFY(editor != nullptr);
    QVERIFY(tree != nullptr);
    auto *live = editor->findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
    QVERIFY(live != nullptr);
    QTRY_COMPARE(tree->fileModel()->rowCount(tree->rootIndex()), 1);
    tree->clicked(tree->fileModel()->index(0, 0, tree->rootIndex()));
    QTest::keyClicks(live, "!");
    QVERIFY(editor->document()->isModified());
    const QString edited = editor->markdown();

    const QColor lightBase = live->palette().color(QPalette::Base);
    QCOMPARE(lightBase, QColor(themes.currentTokens().pageBackground));
    const QColor lightLink = live->document()->find(QStringLiteral("Reference")).charFormat().foreground().color();
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark));
    const auto dark = themes.currentTokens();
    QCOMPARE(live->palette().color(QPalette::Base), QColor(dark.pageBackground));
    QCOMPARE(editor->parentWidget()->palette().color(QPalette::Base), QColor(dark.pageBackground));
    QCOMPARE(live->palette().color(QPalette::Text), QColor(dark.text));
    QVERIFY(lightBase != live->palette().color(QPalette::Base));
    QCOMPARE(tree->palette().color(QPalette::Text), QColor(dark.text));
    QVERIFY(tree->styleSheet().contains(dark.pageBackground));
    const auto link = live->document()->find(QStringLiteral("Reference"));
    QVERIFY(link.charFormat().isAnchor());
    QCOMPARE(link.charFormat().foreground().color(), live->palette().color(QPalette::Link));
    QVERIFY(link.charFormat().foreground().color() != lightLink);
    QCOMPARE(live->document()->find(QStringLiteral("sample")).blockFormat().background().color(),
             QColor(dark.inputBackground));
    QCOMPARE(editor->markdown(), edited);
    QVERIFY(editor->document()->isModified());
    QVERIFY(themes.setTheme(darkeye::ThemeId::Purple));
    QCOMPARE(live->palette().color(QPalette::Base), QColor(themes.currentTokens().pageBackground));
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
}

void AvPageTest::scrollsInsideChamferedDocumentPanel()
{
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("scroll.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QByteArray("Text that scrolls past the top of the editor.\n\n").repeated(100)) > 0);
    file.close();

    darkeye::AvPage page(directory.path(), themes);
    page.resize(800, 480);
    page.initialize();
    page.show();
    auto *tree = page.findChild<mdedit::ProjectFileTree *>();
    auto *editor = page.findChild<MarkdownEditorWidget *>();
    QVERIFY(tree);
    QVERIFY(editor);
    auto *live = editor->findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
    QVERIFY(live);
    QTRY_COMPARE(tree->fileModel()->rowCount(tree->rootIndex()), 1);
    tree->clicked(tree->fileModel()->index(0, 0, tree->rootIndex()));
    auto *scrollBar = live->verticalScrollBar();
    QTRY_VERIFY(scrollBar->maximum() > 2);
    scrollBar->setValue(scrollBar->maximum() / 2);
    QCoreApplication::processEvents();

    QVERIFY(scrollBar->value() > 0);
    QVERIFY(!editor->graphicsEffect());
    QVERIFY(!live->graphicsEffect());

    auto *panel = editor->parentWidget();
    QImage frame(panel->size(), QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    panel->render(&frame, {}, {}, QWidget::DrawChildren);
    QCOMPARE(frame.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(frame.pixelColor(frame.width() / 2, 0), QColor(themes.currentTokens().border));
}

QTEST_MAIN(AvPageTest)
#include "AvPageTest.moc"
