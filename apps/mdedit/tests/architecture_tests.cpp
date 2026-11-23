#include <QtWidgets>
#include "editor/markdown/block_parser.h"
#include "editor/resources/resource_manager.h"
#include "editor/markdown_editor_widget.h"
#include "editor/live_editor.h"
#include "editor/view/markdown_preview_widget.h"
#include <iostream>
#include "latex.h"
#define CHECK(x) do { if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';return 1;} } while(false)
int main(int argc,char **argv){
    QApplication app(argc,argv);
    const QString source=QString::fromUtf8("# 中文 😀\n\n段落\n\n```md\n![fake](none.png)\n```\n\n![表格](<data file.csv>)\n");
    const auto blocks=mdedit::MarkdownParser::parse(source);
    CHECK(blocks.size()==4);
    CHECK(blocks[2].kind==mdedit::BlockKind::Code);
    CHECK(blocks[3].resource->uri=="data file.csv");
    CHECK(blocks[3].source.begin==source.indexOf("![表格]"));
    tex::LaTeX::init(formulaResourcePath().toStdString());
    // Wiki links render as anchors and resolve against the document folder.
    {
        QTemporaryDir folder;
        CHECK(folder.isValid());
        const QString target=folder.filePath(QString::fromUtf8("中文 note.md"));
        QFile file(target);CHECK(file.open(QIODevice::WriteOnly));file.close();
        MarkdownEditorWidget widget;
        widget.setBaseUrl(QUrl::fromLocalFile(folder.path()+"/"));
        widget.setMarkdown(QString::fromUtf8("intro\n\n[[中文 note|显示文字]]\n\n`[[literal]]`\n\n```\n[[code]]\n```"));
        auto *view=dynamic_cast<LiveEditor *>(widget.findChild<QTextBrowser *>("markdownPreview"));CHECK(view);
        const auto label=view->document()->find(QString::fromUtf8("显示文字"));
        CHECK(!label.isNull());
        const QUrl href(label.charFormat().anchorHref());
        CHECK(href.scheme()=="mdedit-wiki");
        CHECK(view->toPlainText().contains("[[literal]]"));
        CHECK(view->toPlainText().contains("[[code]]"));
        QString opened;
        QObject::connect(&widget,&MarkdownEditorWidget::fileLinkActivated,[&](const QString &path){opened=path;});
        view->anchorClicked(href);
        QElapsedTimer timeout;
        timeout.start();
        while(opened.isEmpty()&&timeout.elapsed()<3000)
            app.processEvents(QEventLoop::WaitForMoreEvents,10);
        CHECK(opened==target);
        CHECK(widget.markdown().contains(QString::fromUtf8("[[中文 note|显示文字]]")));
    }
    // A partial selection exposes every touched inline preview in either direction.
    for(bool reverse:{false,true}){
        LiveEditor selected;
        const QString markdown="plain **bold** and *italic* tail\n\n# Heading";
        selected.setSource(markdown);
        auto cursor=selected.document()->find("ol");
        CHECK(cursor.hasSelection());
        const int begin=cursor.selectionStart(),end=cursor.selectionEnd();
        cursor.setPosition(reverse?end:begin);
        cursor.setPosition(reverse?begin:end,QTextCursor::KeepAnchor);
        selected.setTextCursor(cursor);
        CHECK(selected.toPlainText().contains("**bold**"));
        CHECK(!selected.toPlainText().contains("*italic*"));
        CHECK(!selected.toPlainText().contains("# Heading"));
        CHECK(selected.textCursor().selectedText()=="ol");
        CHECK((selected.textCursor().anchor()>selected.textCursor().position())==reverse);
        CHECK(selected.markdown()==markdown);
        selected.insertPlainText("X");
        CHECK(selected.markdown()=="plain **bXd** and *italic* tail\n\n# Heading");
    }
    // Collapsing a cross-line selection at its original anchor must render
    // the other lines again, including when only the selection changes.
    for(bool reverse:{false,true})
    for(bool click:{false,true}){
        LiveEditor selected;
        selected.resize(700,500);selected.show();
        const QString markdown="**upper** line\n\n*lower* line\n\n# Untouched";
        selected.setSource(markdown);app.processEvents();
        auto upper=selected.document()->find("upper");
        auto lower=selected.document()->find("lower");
        QTextCursor range(selected.document());
        range.setPosition(reverse?lower.selectionEnd():upper.selectionStart());
        range.setPosition(reverse?upper.selectionStart():lower.selectionEnd(),QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        CHECK(selected.toPlainText().contains("**upper**"));
        CHECK(selected.toPlainText().contains("*lower*"));
        const int anchor=selected.sourceSelection().first;
        auto collapsed=selected.textCursor();collapsed.setPosition(collapsed.anchor());
        if(click){
            const QPointF point=selected.cursorRect(collapsed).center();
            QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(selected.viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(selected.viewport(),&release);
        }else selected.setTextCursor(collapsed);
        CHECK(!selected.textCursor().hasSelection());
        CHECK(selected.sourceSelection()==qMakePair(anchor,anchor));
        CHECK(reverse?!selected.toPlainText().contains("**upper**"):!selected.toPlainText().contains("*lower*"));
        CHECK(!selected.toPlainText().contains("# Untouched"));
        CHECK(selected.markdown()==markdown);
    }
    // A blank-line anchor must not drift into the following paragraph when
    // the selected preceding paragraph expands its soft line break.
    for(bool mouse:{false,true})
    for(bool formatted:{false,true}){
        LiveEditor selected;selected.resize(700,500);selected.show();
        QString markdown=QString::fromUtf8("这一行是普通正文。\n下一行也是普通正文，用于比较连续文字行的行距。\n\n上方有一个空行，空行应与一行正文占用相同高度。");
        if(formatted)markdown.prepend("**bold** ");
        selected.setSource(markdown);app.processEvents();
        const auto following=selected.document()->find(QString::fromUtf8("上方有一个空行"));
        const auto blank=following.block().previous();
        CHECK(blank.text().isEmpty());
        const auto upper=selected.document()->find(formatted?QString("bold"):QString::fromUtf8("下一行"));
        QTextCursor range(selected.document());range.setPosition(blank.position());
        range.setPosition(upper.selectionStart(),QTextCursor::KeepAnchor);
        if(mouse){
            QTextCursor blankCursor(blank);
            const QPointF origin=selected.cursorRect(blankCursor).center();
            const QPointF destination=selected.cursorRect(upper).center();
            QMouseEvent press(QEvent::MouseButtonPress,origin,origin,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(selected.viewport(),&press);
            for(int step=1;step<=10;++step){
                const QPointF point=origin+(destination-origin)*step/10.;
                QMouseEvent move(QEvent::MouseMove,point,point,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(selected.viewport(),&move);
                CHECK(selected.sourceSelection().first==markdown.indexOf("\n\n")+1);
            }
            QMouseEvent release(QEvent::MouseButtonRelease,destination,destination,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(selected.viewport(),&release);
        }else selected.setTextCursor(range);
        CHECK(selected.sourceSelection().first==markdown.indexOf("\n\n")+1);
        CHECK(!selected.textCursor().selectedText().contains(QString::fromUtf8("上")));
        CHECK(selected.markdown()==markdown);
    }
    // Dragging back while keeping the mouse held must render withdrawn spans.
    for(bool mouse:{false,true}){
        LiveEditor selected;selected.resize(700,500);selected.show();
        const QString markdown="**upper** line\n\n*middle* line\n\nlower anchor";
        selected.setSource(markdown);app.processEvents();
        auto originCursor=selected.document()->find("anchor");
        originCursor.setPosition(originCursor.selectionEnd());
        const QPointF origin=selected.cursorRect(originCursor).center();
        if(mouse){
            QMouseEvent press(QEvent::MouseButtonPress,origin,origin,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(selected.viewport(),&press);
        }else selected.setTextCursor(originCursor);
        int sourceChanges=0;selected.sourceChanged=[&](const QString &){++sourceChanges;};
        auto dragTo=[&](const QString &word){
            auto target=selected.document()->find(word);target.setPosition(target.selectionStart());
            if(mouse){
                const QPointF point=selected.cursorRect(target).center();
                QMouseEvent move(QEvent::MouseMove,point,point,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(selected.viewport(),&move);
                const auto stableRange=selected.sourceSelection();
                const QString stableText=selected.toPlainText();
                for(int repeat=0;repeat<5;++repeat){
                    QMouseEvent stationary(QEvent::MouseMove,point,point,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(selected.viewport(),&stationary);
                    if(selected.sourceSelection()!=stableRange||selected.toPlainText()!=stableText)
                        qFatal("Stationary drag changed selection or preview state");
                }
            }else{
                auto range=selected.textCursor();range.setPosition(target.position(),QTextCursor::KeepAnchor);
                selected.setTextCursor(range);
            }
        };
        dragTo("upper");
        CHECK(selected.toPlainText().contains("**upper**"));
        CHECK(selected.toPlainText().contains("*middle*"));
        const int anchor=selected.sourceSelection().first;
        dragTo("middle");
        CHECK(!selected.toPlainText().contains("**upper**"));
        CHECK(selected.toPlainText().contains("*middle*"));
        CHECK(selected.sourceSelection().first==anchor);
        dragTo("lower");
        CHECK(!selected.toPlainText().contains("**upper**"));
        CHECK(!selected.toPlainText().contains("*middle*"));
        CHECK(selected.textCursor().hasSelection());
        CHECK(selected.sourceSelection().first==anchor);
        CHECK(selected.markdown()==markdown);CHECK(sourceChanges==0);
    }
    // Cross-line source styling must preserve adjacent inline previews until
    // the reverse selection actually reaches and expands them.
    for(const QString &marker:{QString("**"),QString("*")}){
        LiveEditor selected;
        const QString markdown=marker+"upper"+marker+" plain\n\n*middle* line\n\nlower anchor";
        selected.setSource(markdown);
        auto bottom=selected.document()->find("anchor");
        auto plain=selected.document()->find("plain");
        QTextCursor range(selected.document());range.setPosition(bottom.selectionEnd());
        range.setPosition(plain.selectionStart(),QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        auto upper=selected.document()->find("upper");
        CHECK(marker=="**"?upper.charFormat().fontWeight()>=QFont::Bold:upper.charFormat().fontItalic());
        CHECK(!selected.toPlainText().contains(marker+"upper"+marker));
        range=selected.textCursor();range.setPosition(upper.selectionStart()+2,QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        CHECK(selected.toPlainText().contains(marker+"upper"+marker));
        CHECK(selected.markdown()==markdown);
    }
    // A reverse selection crossing the first list item includes its marker,
    // which Qt paints outside the item's visible text.
    for(const QString &marker:{QString("- "),QString("1. "),QString("- [ ] ")}){
        LiveEditor selected;
        const QString markdown="above\n\n"+marker+"first\n- second **bold** and $a^2$\n  - nested\n- last\n\nbelow";
        selected.setSource(markdown);
        auto last=selected.document()->find("last");
        auto first=selected.document()->find("first");
        QTextCursor range(selected.document());range.setPosition(last.selectionEnd());
        range.setPosition(first.selectionStart(),QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        CHECK(selected.toPlainText().contains(marker+"first"));
        auto above=selected.document()->find("above");
        range=selected.textCursor();range.setPosition(above.selectionEnd(),QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        CHECK(selected.toPlainText().contains(marker+"first"));
        CHECK(selected.textCursor().selectedText().contains(marker+"first"));
        CHECK(selected.markdown()==markdown);
    }
    // Exposing inline source must preserve adjacent previews and keep the
    // document stable when the endpoint moves within the same source span.
    {
        LiveEditor selected;
        const QString markdown="plain **bold** then *italic* tail **end**";
        selected.setSource(markdown);
        auto range=selected.document()->find("plain");
        range.setPosition(range.selectionStart()+2,QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        auto bold=selected.document()->find("bold");
        CHECK(bold.charFormat().fontWeight()>=QFont::Bold);
        CHECK(!selected.toPlainText().contains("**bold**"));
        CHECK(!selected.toPlainText().contains("*italic*"));
        CHECK(selected.document()->find("end").charFormat().fontWeight()>=QFont::Bold);
        auto italic=selected.document()->find("italic");
        range=selected.textCursor();range.setPosition(italic.selectionEnd(),QTextCursor::KeepAnchor);
        selected.setTextCursor(range);
        CHECK(selected.toPlainText().contains("**bold**"));
        CHECK(selected.toPlainText().contains("*italic*"));
        CHECK(selected.document()->find("end").charFormat().fontWeight()>=QFont::Bold);
        const QString expanded=selected.toPlainText();
        auto *layout=selected.document()->documentLayout();int layouts=0;
        QObject::connect(layout,&QAbstractTextDocumentLayout::documentSizeChanged,&selected,[&](const QSizeF &){++layouts;});
        for(int i=0;i<4;++i){
            range=selected.textCursor();range.setPosition(range.position()-1,QTextCursor::KeepAnchor);
            selected.setTextCursor(range);
            CHECK(selected.toPlainText()==expanded);
        }
        CHECK(layouts==0);
        CHECK(selected.markdown()==markdown);
    }
    // Live blocks show their opening source line without renumbering later text.
    LiveEditor numbered;
    const QString numberedMarkdown="Before\n\n| A | B |\n| --- | --- |\n| C | D |\n| E | F |\n\n$$\nx + y\n= z\n$$\n\n![missing](missing.csv)\n\nAfter";
    numbered.setSource(numberedMarkdown);
    CHECK(numbered.sourceLineCount()==15);
    auto tableCursor=numbered.document()->find("A");
    CHECK(tableCursor.currentTable());
    auto table=tableCursor.currentTable();
    for(int row=0;row<table->rows();++row)
        for(int column=0;column<table->columns();++column)
            CHECK(numbered.lineNumberAt(table->cellAt(row,column).firstCursorPosition())==3);
    auto afterCursor=numbered.document()->find("After");
    CHECK(numbered.lineNumberAt(afterCursor)==15);
    const int formula=numberedMarkdown.indexOf("x + y");
    numbered.restoreSourceSelection(formula,formula);
    CHECK(numbered.lineNumberAt(numbered.textCursor())==8);
    const int closingFormula=numberedMarkdown.indexOf("$$",formula);
    numbered.restoreSourceSelection(closingFormula,closingFormula);
    CHECK(numbered.lineNumberAt(numbered.textCursor())==8);
    CHECK(numbered.sourceLineCount()==15);
    numbered.setSource("Before\n\n$$\nx + y\n$$\n\nAfter");
    CHECK(numbered.lineNumberAt(numbered.document()->find("After"))==7);
    CHECK(numbered.sourceLineCount()==7);
    numbered.setSource("plain\n\ntext\n");
    CHECK(numbered.sourceLineCount()==4);
    // Rules and escaped rule text must not merge adjacent link edit regions.
    for(const QString &prefix:{QString("---"),QString("\\---")})
    for(const QString &escape:{QString(),QString("\\")}){
        LiveEditor links;
        const QString first="[Qt 官方网站]"+escape+"([https://www.qt.io/](https://www.qt.io/))";
        const QString second="[相对路径链接：项目说明]"+escape+"(README.md)";
        const QString markdown=prefix+'\n'+first+'\n'+second;
        links.setSource(markdown);
        const int position=markdown.indexOf(first)+2;
        links.restoreSourceSelection(position,position);
        CHECK(links.toPlainText().contains(first));
        CHECK(!links.toPlainText().contains(second));
        const int secondPosition=markdown.indexOf(second)+2;
        links.restoreSourceSelection(secondPosition,secondPosition);
        CHECK(links.toPlainText().contains(second));
        CHECK(!links.toPlainText().contains(first));
        CHECK(links.markdown()==markdown);
    }
    // Revealing quote markers keeps the body width and following paragraphs stable.
    for(const QString &quote:{QString("> A quoted paragraph with enough words to wrap across the editor width. ").repeated(1),
                              QString("> First quoted line\n> Second quoted line"),
                              QString("> > Nested quoted text with enough words to wrap across the editor width.")}){
        LiveEditor quoted;quoted.resize(420,400);
        const QString markdown=QString("Before\n\n").repeated(20)+quote+"\n\nAfter\n\n"+QString("Tail\n\n").repeated(20);
        quoted.setSource(markdown);quoted.show();app.processEvents();
        auto afterTop=[&]{return quoted.document()->documentLayout()->blockBoundingRect(
            quoted.document()->findBlock(quoted.document()->find("After").selectionStart())).top();};
        const qreal before=afterTop();
        for(int repeat=0;repeat<3;++repeat){
            const auto label=quoted.document()->find("quoted");
            CHECK(!label.isNull());
            quoted.verticalScrollBar()->setValue(quoted.cursorRect(label).top()+quoted.verticalScrollBar()->value()-120);
            const int scroll=quoted.verticalScrollBar()->value();
            const QPointF point=quoted.cursorRect(label).center();
            QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(quoted.viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(quoted.viewport(),&release);
            app.processEvents();
            CHECK(quoted.toPlainText().contains("> "));
            CHECK(qAbs(afterTop()-before)<=1);
            CHECK(qAbs(quoted.verticalScrollBar()->value()-scroll)<=1);
            CHECK(quoted.markdown()==markdown);
            quoted.restoreSourceSelection(0,0);
            CHECK(qAbs(afterTop()-before)<=1);
        }
    }
    // Long documents have their real content and geometry before scrolling.
    LiveEditor lazy;lazy.resize(900,700);
    QString large;
    for(int i=0;i<1500;++i)large+=QString("Paragraph **token%1**.\n\n").arg(i);
    lazy.setSource(large);
    CHECK(lazy.markdown()==large);
    CHECK(lazy.toPlainText().contains("token1499"));
    lazy.show();app.processEvents();
    CHECK(lazy.toPlainText().contains("token0"));
    lazy.verticalScrollBar()->setValue(lazy.verticalScrollBar()->maximum());
    CHECK(lazy.toPlainText().contains("token1499"));
    const int tail=large.indexOf("token1499");
    lazy.restoreSourceSelection(tail,tail+9);
    CHECK(lazy.sourceSelection()==qMakePair(tail,tail+9));
    CHECK(lazy.markdown()==large);
    // Switching visible blocks must never publish the empty document's range.
    lazy.verticalScrollBar()->setValue(lazy.verticalScrollBar()->maximum()/2);
    int collapsedRanges=0;
    QObject::connect(lazy.verticalScrollBar(),&QScrollBar::rangeChanged,
                     &lazy,[&](int minimum,int maximum){if(minimum==maximum)++collapsedRanges;});
    const int clickScroll=lazy.verticalScrollBar()->value();
    for(int y:{100,200,100}){
        const QPointF point(120,y);
        QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(lazy.viewport(),&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(lazy.viewport(),&release);
        app.processEvents();
        CHECK(qAbs(lazy.verticalScrollBar()->value()-clickScroll)<=2);
    }
    CHECK(collapsedRanges==0);
    // Removing blank lines must publish one source edit without exposing the
    // entire Markdown document to QTextControl as an intermediate plain view.
    for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
        LiveEditor blankLines;blankLines.resize(900,700);
        QString expected="# Heading\n\n\n\nBody **bold**\n\n```cpp\nint value;\n```\n";
        blankLines.setSource(expected);
        int position=key==Qt::Key_Backspace?expected.indexOf("Body"):expected.indexOf('\n')+1;
        blankLines.restoreSourceSelection(position,position);
        int published=0,intermediate=0;
        blankLines.sourceChanged=[&](const QString &){++published;};
        QObject::connect(&blankLines,&QTextEdit::textChanged,[&]{++intermediate;});
        for(int i=0;i<3;++i){
            const int removed=key==Qt::Key_Backspace?position-1:position;
            expected.remove(removed,1);position=removed;
            QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(&blankLines,&remove);
            CHECK(blankLines.markdown()==expected);
            CHECK(blankLines.sourceSelection()==qMakePair(position,position));
            CHECK(!blankLines.toPlainText().contains("**bold**"));
        }
        CHECK(published==3);CHECK(intermediate==0);
    }
    // Repeated ordinary deletions must preserve unrelated document blocks.
    for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
        LiveEditor incremental;
        QString expected="ordinary paragraph text\n\n# Tail heading";
        incremental.setSource(expected);
        int position=12;
        incremental.restoreSourceSelection(position,position);
        auto tail=incremental.document()->lastBlock();
        auto *marker=new QTextBlockUserData;
        tail.setUserData(marker);
        int published=0;
        incremental.sourceChanged=[&](const QString &){++published;};
        for(int i=0;i<4;++i){
            const int removed=key==Qt::Key_Backspace?position-1:position;
            expected.remove(removed,1);position=removed;
            QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(&incremental,&remove);
            CHECK(incremental.markdown()==expected);
            CHECK(incremental.sourceSelection()==qMakePair(position,position));
            CHECK(incremental.document()->lastBlock().userData()==marker);
            CHECK(!incremental.toPlainText().contains("# Tail heading"));
        }
        CHECK(published==4);
    }
    QTemporaryDir dir;CHECK(dir.isValid());
    QFile csv(dir.filePath("data file.csv"));CHECK(csv.open(QIODevice::WriteOnly));
    csv.write("name,value\n\"hello, world\",\"a\"\"b\"\n");csv.close();
    const QUrl base=QUrl::fromLocalFile(dir.path()+"/");
    const auto url=mdedit::ResourceResolver::resolve("data%20file.csv",base);
    CHECK(url.toLocalFile()==csv.fileName());
    mdedit::ResourceManager manager;
    const QString key=manager.request(url);CHECK(manager.request(url)==key);
    const QString missing=manager.request(QUrl::fromLocalFile(dir.filePath("missing.png")));
    const QString remote=manager.request(QUrl("https://example.com/image.png"));
    QElapsedTimer elapsed;elapsed.start();
    while((!manager.lookup(key)||!manager.lookup(missing)||!manager.lookup(remote))&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(manager.lookup(key));CHECK(manager.lookup(key)->error.isEmpty());
    CHECK(manager.lookup(key)->rows[1]==QStringList({"hello, world","a\"b"}));
    CHECK(manager.lookup(missing)&&!manager.lookup(missing)->error.isEmpty());
    CHECK(manager.lookup(remote)&&!manager.lookup(remote)->error.isEmpty());
    const auto sized=mdedit::MarkdownParser::parse("![Photo|400](photo.png)");
    CHECK(sized.first().resource->width==400);CHECK(sized.first().resource->alt=="Photo");
    CHECK(mdedit::MarkdownParser::parse("![Photo|0](photo.png)").first().resource->width==0);
    QImage photo(800,400,QImage::Format_RGB32);photo.fill(Qt::red);
    CHECK(photo.save(dir.filePath("photo.png")));
    CHECK(photo.save(dir.filePath("attachment.ico"),"PNG"));
    const auto unsupported=manager.request(QUrl::fromLocalFile(dir.filePath("attachment.ico")));
    elapsed.restart();while(!manager.lookup(unsupported)&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(manager.lookup(unsupported)&&!manager.lookup(unsupported)->error.isEmpty());
    CHECK(manager.lookup(unsupported)->image.isNull());
    for(const auto &extension:QStringList{"png","svg","jpg","jpeg","bmp","gif","webp","tif","tiff"})
        CHECK(mdedit::ResourceResolver::isImageResource(QUrl("file:///sample."+extension.toUpper())));
    LiveEditor attachment;attachment.resize(900,700);attachment.document()->setBaseUrl(base);
    attachment.setSource("![Attachment](attachment.ico)");
    CHECK(attachment.toPlainText().contains("attachment.ico"));
    CHECK(!attachment.toPlainText().contains(QChar::ObjectReplacementCharacter));
    CHECK(attachment.document()->size().height()<100);
    bool hasOpenLink=false;
    for(auto block=attachment.document()->begin();block.isValid();block=block.next())
        for(auto it=block.begin();!it.atEnd();++it){
            const QUrl action(it.fragment().charFormat().anchorHref());
            if(action.path()=="/open")hasOpenLink=QUrl(QUrlQuery(action).queryItemValue("file",QUrl::FullyDecoded)).toLocalFile()==dir.filePath("attachment.ico");
        }
    CHECK(hasOpenLink);
    LiveEditor images;images.resize(900,700);images.document()->setBaseUrl(base);
    const QString imageSource="Before.\n\n![Photo|400](photo.png)\n\nAfter.\n";
    images.setSource(imageSource);images.show();
    auto sizedImage=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().startsWith("image-ready:"))return f.width()==400&&f.height()==200;
        }return false;
    };
    elapsed.restart();while(!sizedImage()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(sizedImage());CHECK(images.markdown()==imageSource);
    images.restoreSourceSelection(imageSource.indexOf("!["),imageSource.indexOf("!["));
    CHECK(sizedImage());
    for(auto b=images.document()->begin();b.isValid();b=b.next())if(b.text().contains(QChar::ObjectReplacementCharacter)){
        const auto bounds=images.document()->documentLayout()->blockBoundingRect(b);
        const QPointF pt(bounds.left()+30,bounds.top()-images.verticalScrollBar()->value()+60);
        QMouseEvent click(QEvent::MouseButtonPress,pt,images.viewport()->mapToGlobal(pt.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(images.viewport(),&click);break;
    }
    CHECK(sizedImage());CHECK(!images.toPlainText().contains("![Photo|400]"));
    CHECK(images.editResourceSourceAt(imageSource.indexOf("![")));
    CHECK(images.toPlainText().contains("![Photo|400]"));
    images.restoreSourceSelection(0,0);CHECK(images.markdown()==imageSource);
    images.setSource("Before.\n\nText ![Inline|200](photo.png) here.\n");
    auto inlineSized=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().endsWith("photo.png"))return f.width()==200&&f.height()==100;
        }return false;
    };
    elapsed.restart();while(!inlineSized()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(inlineSized());
    auto writeText=[&](const QString &name,const QByteArray &bytes){
        QFile file(dir.filePath(name));if(!file.open(QIODevice::WriteOnly))return QString();
        file.write(bytes);file.close();return manager.request(QUrl::fromLocalFile(file.fileName()));
    };
    const auto txt=writeText("note.TXT",QString::fromUtf8("中文 <b>plain</b>\r\nsecond").toUtf8());
    const auto json=writeText("data.json","{\"name\": \"value\"}");
    const auto empty=writeText("empty.log",{});
    const auto utf16=writeText("unicode.txt",QByteArray::fromHex("fffe2d4e8765"));
    const auto badText=writeText("binary.txt",QByteArray("a\0b",3));
    const auto longText=writeText("long.log",QByteArray("line\n").repeated(100));
    auto textReady=[&]{return manager.lookup(txt)&&manager.lookup(json)&&manager.lookup(empty)&&manager.lookup(utf16)&&manager.lookup(badText)&&manager.lookup(longText);};
    elapsed.restart();while(!textReady()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(textReady());
    CHECK(manager.lookup(txt)->text==QString::fromUtf8("中文 <b>plain</b>\nsecond"));
    CHECK(manager.lookup(json)->text.has_value());
    CHECK(manager.lookup(empty)->text.has_value()&&manager.lookup(empty)->text->isEmpty());
    CHECK(manager.lookup(utf16)->text==QString::fromUtf8("中文"));
    CHECK(!manager.lookup(badText)->error.isEmpty());
    CHECK(manager.lookup(longText)->textTruncated);
    const QString textSource="Before.\n\n![说明](note.TXT)\n\nAfter.\n";
    images.setSource(textSource);
    auto textPreviewReady=[&]{
        for(auto b=images.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){
            const auto f=it.fragment().charFormat().toImageFormat();
            if(f.isValid()&&f.name().startsWith("text-ready:"))return true;
        }return false;
    };
    elapsed.restart();while(!textPreviewReady()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    CHECK(textPreviewReady());CHECK(!images.toPlainText().contains("打开原文件"));CHECK(images.markdown()==textSource);
    // The top-right action exposes the attachment source without modifying it.
    for(auto b=images.document()->begin();b.isValid();b=b.next())if(b.text().contains(QChar::ObjectReplacementCharacter)){
        const auto bounds=images.document()->documentLayout()->blockBoundingRect(b);
        const qreal width=qBound(24.0,QFontMetricsF(images.font()).height()+4,30.0);
        const QPointF pt(qMin(qreal(images.viewport()->width()-8),bounds.right())-width/2,bounds.top()-images.verticalScrollBar()->value()+8);
        QMouseEvent press(QEvent::MouseButtonPress,pt,images.viewport()->mapToGlobal(pt.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(images.viewport(),&press);break;
    }
    CHECK(images.toPlainText().contains("![说明](note.TXT)"));
    CHECK(images.markdown()==textSource);
    images.restoreSourceSelection(textSource.indexOf("!["),textSource.indexOf("!["));
    CHECK(images.toPlainText().contains("![说明](note.TXT)"));
    images.restoreSourceSelection(0,0);CHECK(images.markdown()==textSource);
    // Keep the comprehensive document's real resource fixtures loadable.
    const QDir project(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath(".."));
    QFile comprehensive(project.filePath(QString::fromUtf8("综合测试/综合测试.md")));
    CHECK(comprehensive.open(QIODevice::ReadOnly));
    const auto resourceBlocks=mdedit::MarkdownParser::parse(QString::fromUtf8(comprehensive.readAll()));
    const QUrl fixtureBase=QUrl::fromLocalFile(QFileInfo(comprehensive).absolutePath()+"/");
    QList<QPair<QString,bool>> fixtureKeys;
    for(const auto &block:resourceBlocks)if(block.resource){
        const auto fixtureUrl=mdedit::ResourceResolver::resolve(block.resource->uri,fixtureBase);
        if(!fixtureUrl.isLocalFile())continue;
        const bool expectedError=fixtureUrl.path().contains("/errors/")||fixtureUrl.path().endsWith("does-not-exist.png");
        fixtureKeys.append({manager.request(fixtureUrl),expectedError});
    }
    CHECK(fixtureKeys.size()>40);
    auto fixturesReady=[&]{for(const auto &entry:fixtureKeys)if(!manager.lookup(entry.first))return false;return true;};
    elapsed.restart();while(!fixturesReady()&&elapsed.elapsed()<10000){app.processEvents();QThread::msleep(1);}
    CHECK(fixturesReady());
    for(const auto &entry:fixtureKeys){
        const auto *snapshot=manager.lookup(entry.first);
        if(snapshot->error.isEmpty()==entry.second)qWarning()<<entry.first<<snapshot->error;
        CHECK(snapshot->error.isEmpty()!=entry.second);
    }
    MarkdownEditorWidget widget;widget.setMarkdown(source);widget.setLiveMode(false);
    CHECK(widget.findChildren<QSplitter *>().isEmpty());
    widget.selectSource(blocks[3].source.begin,blocks[3].source.begin);CHECK(widget.selection().first==blocks[3].source.begin);
    widget.resize(900,600);widget.show();app.processEvents();
    mdedit::MarkdownPreviewWidget standalone;standalone.setSource(source,base);standalone.resize(450,600);standalone.show();
    auto *preview=&standalone;
    QObject::connect(preview,&mdedit::MarkdownPreviewWidget::sourceActivated,&widget,[&](int position){widget.selectSource(position,position);});
    QMouseEvent click(QEvent::MouseButtonPress,QPointF(30,30),QPointF(30,30),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(preview->viewport(),&click);CHECK(widget.selection()==qMakePair(0,0));
    CHECK(!widget.grab().isNull());
    widget.replaceSource(0,0,"new",3,3);CHECK(widget.markdown().startsWith("new#"));
    widget.document()->undo();CHECK(widget.markdown()==source);
    widget.setLiveMode(false);CHECK(!widget.isLiveMode());CHECK(widget.markdown()==source);
    widget.setLiveMode(true);widget.setLiveMode(false);CHECK(widget.markdown()==source);
    // Mode switches preserve both source offsets and the caret's viewport height.
    widget.setMarkdown(large);widget.setLiveMode(false);
    auto *raw=widget.findChild<QPlainTextEdit *>("markdownEditor");CHECK(raw);
    auto *modeLive=widget.findChild<QTextBrowser *>("markdownPreview");CHECK(modeLive);
    const int switchAt=large.indexOf("token750")+3;
    for(int desiredY:{100,280,430}){
        widget.selectSource(switchAt,switchAt);
        raw->verticalScrollBar()->setValue(raw->verticalScrollBar()->value()+
            qRound(qreal(raw->cursorRect().top()-desiredY)/raw->fontMetrics().lineSpacing()));
        const int originalY=raw->cursorRect().top();
        for(int repeat=0;repeat<3;++repeat){
            widget.setLiveMode(true);app.processEvents();
            CHECK(modeLive->updatesEnabled());
            CHECK(modeLive->viewport()->updatesEnabled());
            CHECK(widget.selection()==qMakePair(switchAt,switchAt));
            CHECK(qAbs(modeLive->cursorRect().top()-originalY)<=raw->fontMetrics().lineSpacing());
            const int liveY=modeLive->cursorRect().top();
            widget.setLiveMode(false);app.processEvents();
            CHECK(raw->updatesEnabled());
            CHECK(raw->viewport()->updatesEnabled());
            CHECK(widget.selection()==qMakePair(switchAt,switchAt));
            CHECK(qAbs(raw->cursorRect().top()-liveY)<=raw->fontMetrics().lineSpacing());
        }
    }
    // History keeps a scrolled viewport stable and restores the actual edit caret.
    widget.setMarkdown(large);widget.setLiveMode(true);
    auto *live=dynamic_cast<LiveEditor *>(widget.findChild<QTextBrowser *>("markdownPreview"));CHECK(live);
    const int editAt=large.indexOf("token750");
    widget.replaceSource(editAt,editAt,"XYZ",editAt+3,editAt+3);
    app.processEvents();
    const int historyScroll=live->verticalScrollBar()->value();
    auto historyKey=[&](QKeySequence::StandardKey key){
        const auto combination=QKeySequence(key)[0];
        QKeyEvent event(QEvent::KeyPress,combination.key(),combination.keyboardModifiers());
        QApplication::sendEvent(live,&event);
    };
    historyKey(QKeySequence::Undo);
    CHECK(widget.markdown()==large);
    CHECK(live->sourceSelection()==qMakePair(editAt,editAt));
    CHECK(live->verticalScrollBar()->value()==historyScroll);
    historyKey(QKeySequence::Redo);
    CHECK(widget.markdown().mid(editAt,3)=="XYZ");
    CHECK(live->sourceSelection()==qMakePair(editAt+3,editAt+3));
    CHECK(live->verticalScrollBar()->value()==historyScroll);
    app.processEvents();
    CHECK(live->verticalScrollBar()->value()==historyScroll);
    return 0;
}
