#include "ui/pages/AvPage.h"

#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QUrl>
#include <QtTest>

class AvPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void buildsDocumentTreeAndRendersMarkdown();
    void editsAndAutomaticallySavesMarkdown();
};

void AvPageTest::buildsDocumentTreeAndRendersMarkdown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("分类")));

    QFile first(directory.filePath(QStringLiteral("01-首页.md")));
    QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(first.write("# 首页\n\n进入[[详情]]") > 0);
    first.close();

    QFile second(directory.filePath(QStringLiteral("分类/详情.md")));
    QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(second.write("## 详情\n\n正文") > 0);
    second.close();

    darkeye::AvPage page(directory.path());
    page.initialize();

    auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("AvWikiTree"));
    auto *browser = page.findChild<QTextBrowser *>(QStringLiteral("AvWikiBrowser"));
    QVERIFY(tree != nullptr);
    QVERIFY(browser != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("01-首页"));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("进入详情")));
    QVERIFY(browser->toHtml().contains(QStringLiteral("internal:详情")));

    browser->anchorClicked(QUrl(QStringLiteral("internal:详情")));
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("详情"));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("正文")));
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
    auto *edit = page.findChild<QPushButton *>(QStringLiteral("AvWikiEditButton"));
    auto *preview = page.findChild<QPushButton *>(QStringLiteral("AvWikiPreviewButton"));
    auto *editor = page.findChild<QPlainTextEdit *>(QStringLiteral("AvWikiEditor"));
    QVERIFY(edit != nullptr);
    QVERIFY(preview != nullptr);
    QVERIFY(editor != nullptr);

    edit->click();
    QCOMPARE(editor->toPlainText(), QStringLiteral("# 原始内容"));
    editor->setPlainText(QStringLiteral("# 已自动保存"));
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        QFile saved(filePath);
        return saved.open(QIODevice::ReadOnly | QIODevice::Text)
               && QString::fromUtf8(saved.readAll()) == QStringLiteral("# 已自动保存");
    }(), 2500);

    preview->click();
    auto *browser = page.findChild<QTextBrowser *>(QStringLiteral("AvWikiBrowser"));
    QVERIFY(browser->toPlainText().contains(QStringLiteral("已自动保存")));
}

QTEST_MAIN(AvPageTest)
#include "AvPageTest.moc"
