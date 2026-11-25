#include "editor/markdown_editor_widget.h"
#include "markdown/renderer.h"
#include "markdown/scanner.h"
#include <QTextTable>

static bool openingHeadingStaysRendered(MarkdownEditorWidget &editor,const QString &source){
    editor.setMarkdown(source);
    auto *live=editor.findChild<QTextBrowser *>("markdownPreview");
    auto rendered=[live]{
        const auto first=live->document()->begin();
        return first.text()=="Heading"&&first.blockFormat().headingLevel()==2;
    };
    if(!rendered())return false;
    QFocusEvent focusIn(QEvent::FocusIn,Qt::OtherFocusReason);
    QApplication::sendEvent(live,&focusIn);
    if(!rendered())return false;
    QFocusEvent focusOut(QEvent::FocusOut,Qt::OtherFocusReason);
    QApplication::sendEvent(live,&focusOut);

    QEventLoop loop;
    QTimer::singleShot(120,&loop,&QEventLoop::quit);
    loop.exec();
    if(!rendered()||!live->toPlainText().endsWith("Tail")||editor.markdown()!=source)return false;

    QApplication::sendEvent(live,&focusIn);
    QKeyEvent type(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
    QApplication::sendEvent(live,&type);
    return editor.markdown()=="## x"+source.mid(3);
}

int main(int argc,char **argv){
    QApplication app(argc,argv);
    const QString source="# Heading\n\n**Bold** and $x$\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n";
    MarkdownEditorWidget editor;
    editor.setMarkdown(source);
    for(bool live:{true,false,true}){
        editor.setLiveMode(live);
        if(editor.markdown()!=source)return 1;
    }
    QTextDocument document;QStringList errors;
    if(renderMarkdown(&document,source,errors)!=0||!errors.isEmpty())return 2;
    if(!document.toPlainText().contains("$x$"))return 3;
    bool table=false;
    for(auto *frame:document.rootFrame()->childFrames())table|=qobject_cast<QTextTable *>(frame)!=nullptr;
    if(!table)return 4;
    if(!scanSpans("$x$ and $$y$$").isEmpty())return 5;
    for(const auto &extension:{"png","jpg","jpeg","bmp","gif"})
        if(!mdedit::ResourceResolver::isImageResource(QUrl::fromLocalFile(QString("/tmp/test.")+extension)))return 6;
    for(const auto &extension:{"svg","webp","tif","tiff","pdf"})
        if(mdedit::ResourceResolver::isPreviewResource(QUrl::fromLocalFile(QString("/tmp/test.")+extension)))return 7;
    const QString heading="## Heading\n\n";
    if(!openingHeadingStaysRendered(editor,heading+"Tail"))return 8;
    if(!openingHeadingStaysRendered(editor,heading+QString("Body paragraph.\n\n").repeated(400)+"Tail"))return 9;
    return 0;
}
