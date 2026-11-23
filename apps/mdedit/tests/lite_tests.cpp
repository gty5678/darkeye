#include "editor/markdown_editor_widget.h"
#include "markdown/renderer.h"
#include "markdown/scanner.h"
#include <QTextTable>

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
    return 0;
}
