#include "ui/components/ClickableLabel.h"
#include "ui/components/WikiHighlighter.h"
#include "ui/components/WikiTextEdit.h"

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

class TextWidgetTest final : public QObject
{
    Q_OBJECT

private slots:
    void clickableLabelCopiesAndRequestsJump();
    void wikiHighlighterKeepsPythonFormats();
    void wikiEditorRecognizesLinksAndCompletionRegions();
    void wikiEditorLoadsAndInsertsCompletions();
};

void TextWidgetTest::clickableLabelCopiesAndRequestsJump()
{
    darkeye::ClickableLabel label(QStringLiteral("三上悠亚"), true);
    QSignalSpy clicked(&label, &darkeye::ClickableLabel::clicked);
    QSignalSpy jump(&label, &darkeye::ClickableLabel::actressJumpRequested);
    label.show();

    QTest::mouseClick(&label, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("三上悠亚"));
    QCOMPARE(clicked.count(), 1);

    QTest::mouseClick(&label, Qt::RightButton);
    QCOMPARE(jump.count(), 1);
    QCOMPARE(jump.takeFirst().at(0).toString(), QStringLiteral("三上悠亚"));
    QCOMPARE(label.sizeHint().width(), label.fontMetrics().horizontalAdvance(label.text()));
}

void TextWidgetTest::wikiHighlighterKeepsPythonFormats()
{
    QTextDocument document;
    darkeye::WikiHighlighter highlighter(&document);
    document.setPlainText(QStringLiteral("# 标题\n[[SONE-979|作品]] **粗体** *斜体*"));
    highlighter.rehighlight();

    const auto headingFormats = document.firstBlock().layout()->formats();
    QVERIFY(!headingFormats.isEmpty());
    QCOMPARE(headingFormats.first().format.foreground().color(), QColor(QStringLiteral("#e67e22")));
    QCOMPARE(headingFormats.first().format.fontPointSize(), 24.0);

    const auto bodyFormats = document.firstBlock().next().layout()->formats();
    QVERIFY(bodyFormats.size() >= 3);
    QCOMPARE(bodyFormats.first().format.foreground().color(), QColor(QStringLiteral("#3498db")));
    QVERIFY(bodyFormats.first().format.fontUnderline());
}

void TextWidgetTest::wikiEditorRecognizesLinksAndCompletionRegions()
{
    QCOMPARE(darkeye::WikiTextEdit::linkTargetAt(
                 QStringLiteral("查看 [[SONE-979|作品]]"), 8),
             QStringLiteral("SONE-979"));
    QVERIFY(darkeye::WikiTextEdit::linkTargetAt(QStringLiteral("普通文本"), 2).isEmpty());

    darkeye::WikiTextEdit editor;
    editor.setPlainText(QStringLiteral("输入 [[SON"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    QCOMPARE(editor.completionPrefix(), QStringLiteral("SON"));

    editor.setPlainText(QStringLiteral("[[SONE-979]] 后面"));
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    QVERIFY(editor.completionPrefix().isNull());
}

void TextWidgetTest::wikiEditorLoadsAndInsertsCompletions()
{
    darkeye::WikiTextEdit editor;
    editor.setCompleterList({QStringLiteral("SONE-979"), QStringLiteral("START-451")});
    QCOMPARE(editor.completerList().size(), 2);

    editor.setPlainText(QStringLiteral("[[SON"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    editor.insertCompletion(QStringLiteral("SONE-979"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("[[SONE-979]]"));

    QSignalSpy loaded(&editor, &darkeye::WikiTextEdit::completerWordsLoaded);
    editor.setCompleterLoader([] { return QStringList{QStringLiteral("ABP-001")}; });
    QTRY_COMPARE(loaded.count(), 1);
    QTRY_COMPARE(editor.completerList(), QStringList{QStringLiteral("ABP-001")});
}

QTEST_MAIN(TextWidgetTest)
#include "TextWidgetTest.moc"
