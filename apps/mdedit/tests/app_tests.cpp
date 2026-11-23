#include "app_tests.h"
#include "editor/highlighter.h"
#include "window.h"
#include "markdown/renderer.h"
#include "markdown/scanner.h"

int runTableEditTests();
int runPdfTests(QApplication &app);

std::optional<int> runAppTests(QApplication &app){
    if(app.arguments().contains("--web-link-test")){
        QTextBrowser capture;QUrl opened;
        QObject::connect(&capture,&QTextBrowser::anchorClicked,[&](const QUrl &url){opened=url;});
        QTemporaryDir folders;
        if(!folders.isValid()||!QDir(folders.path()).mkdir("项目 资料"))return 1;
        const QUrl local=QUrl::fromLocalFile(folders.filePath("项目 资料"));
        const QString nativeSeparators="file:"+local.path(QUrl::FullyEncoded).replace('/','\\');
        const QUrl web("https://www.baidu.com");
        const QList<QPair<QString,QUrl>> cases={
            {"www.baidu.com",web},{"www\\.baidu.com",web},{web.toString(),web},
            {local.toString(QUrl::FullyEncoded),local},{nativeSeparators,local},
            {"<"+QUrl::fromPercentEncoding(nativeSeparators.toUtf8())+">",local}
        };
        QDesktopServices::setUrlHandler("https",&capture,"anchorClicked");
        QDesktopServices::setUrlHandler("file",&capture,"anchorClicked");
        for(const auto &[target,expected]:cases){
            MarkdownEditorWidget widget;widget.resize(640,360);widget.show();
            const QString source="intro\n\n[打开]("+target+")\n\nend";
            widget.setMarkdown(source);app.processEvents();
            auto *view=widget.findChild<QTextBrowser *>("markdownPreview");
            auto label=view->document()->find("打开");if(label.isNull())return 1;
            label.setPosition(label.selectionStart()+1);
            const QPoint point=view->cursorRect(label).center();
            opened=QUrl();
            QMouseEvent press(QEvent::MouseButtonPress,point,view->viewport()->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view->viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,view->viewport()->mapToGlobal(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view->viewport(),&release);
            if(opened!=expected||widget.markdown()!=source){qWarning()<<target<<opened;return 2;}
        }
        QDesktopServices::unsetUrlHandler("file");
        QDesktopServices::unsetUrlHandler("https");
        if(normalizedWebLink(QUrl("README.md"))!=QUrl("README.md")||normalizedWebLink(QUrl("#heading"))!=QUrl("#heading"))return 3;
        QTextDocument driveLinks;QStringList errors;
        renderMarkdown(&driveLinks,R"([一](file:C:\Users\Notes) [二](file:///C:\Users\Notes) `[三](file:C:\Users\Notes)`)",errors);
        for(const QString &label:{QString("一"),QString("二")}){
            const auto cursor=driveLinks.find(label);
            if(cursor.charFormat().anchorHref()!="file:///C:/Users/Notes")return 4;
        }
        if(!driveLinks.toPlainText().contains(R"([三](file:C:\Users\Notes))"))return 5;
        return 0;
    }
    if(app.arguments().contains("--selection-menu-test")){
        const QList<QStringList> formats={{"加粗","**","**"},{"斜体","*","*"},
            {"删除线","~~","~~"},{"高亮","==","=="}};
        for(bool liveMode:{false,true})for(bool reverse:{false,true})for(const auto &format:formats){
            MarkdownEditorWidget widget;widget.resize(640,360);widget.show();widget.setLiveMode(liveMode);
            widget.setMarkdown("before selected after");
            widget.selectSource(reverse?15:7,reverse?7:15);app.processEvents();
            auto *view=liveMode?static_cast<QAbstractScrollArea *>(widget.findChild<QTextBrowser *>("markdownPreview")):
                static_cast<QAbstractScrollArea *>(widget.findChild<QPlainTextEdit *>("markdownEditor"));
            const QPoint point(100,50);
            QContextMenuEvent event(QContextMenuEvent::Mouse,point,view->viewport()->mapToGlobal(point));
            QApplication::sendEvent(view->viewport(),&event);app.processEvents();
            auto *menu=widget.findChild<QMenu *>("editorContextMenu");
            if(!menu){qWarning()<<"Missing selection context menu"<<liveMode;return 1;}
            QAction *chosen=nullptr;
            for(auto *action:menu->actions()){
                if(action->text()=="行内代码"||action->text()=="插入链接")return 5;
                if(action->text()==format[0])chosen=action;
            }
            if(!chosen){qWarning()<<"Missing format action"<<format[0];return 2;}
            menu->close();chosen->trigger();
            if(widget.markdown()!="before "+format[1]+"selected"+format[2]+" after"){
                qWarning()<<"Context format failed"<<liveMode<<reverse<<format[0]<<widget.markdown();return 3;
            }
            widget.document()->undo();
            if(widget.markdown()!="before selected after")return 4;
        }
        for(bool liveMode:{false,true})for(const QString &name:{QString("插入链接"),QString("插入代码块")}){
            MarkdownEditorWidget widget;widget.resize(640,360);widget.show();widget.setLiveMode(liveMode);
            widget.setMarkdown("before selected after");widget.selectSource(7,7);app.processEvents();
            auto *live=widget.findChild<QTextBrowser *>("markdownPreview");
            auto *plain=widget.findChild<QPlainTextEdit *>("markdownEditor");
            auto *view=liveMode?static_cast<QAbstractScrollArea *>(live):static_cast<QAbstractScrollArea *>(plain);
            const QPoint point=(liveMode?live->cursorRect():plain->cursorRect()).center();
            QContextMenuEvent event(QContextMenuEvent::Mouse,point,view->viewport()->mapToGlobal(point));
            QApplication::sendEvent(view->viewport(),&event);app.processEvents();
            auto *menu=widget.findChild<QMenu *>("editorContextMenu");if(!menu)return 6;
            QAction *chosen=nullptr;
            for(auto *action:menu->actions()){
                if(action->text()=="加粗"||action->text()=="行内代码")return 7;
                if(action->text()==name)chosen=action;
            }
            if(!chosen)return 8;
            menu->close();chosen->trigger();
            const QString expected=name=="插入链接"?"before [](url)selected after":"before \n```\n\n```\nselected after";
            if(widget.markdown()!=expected){qWarning()<<name<<liveMode<<widget.markdown();return 9;}
            widget.document()->undo();if(widget.markdown()!="before selected after")return 10;
        }
        return 0;
    }
    if(app.arguments().contains("--task-marker-test")){
        for(const QString &prefix:{QString("- [ ] "),QString("- [x] "),QString("- [X] "),QString("  - [ ] "),QString("1. [x] "),QString("- ")}){
            LiveEditor editor;editor.resize(640,300);editor.show();
            const QString source="before\n\n"+prefix+"task\n\nafter";
            editor.setSource(source);editor.restoreSourceSelection(0,0);app.processEvents();
            QTextBlock item;
            for(auto block=editor.document()->begin();block.isValid();block=block.next())
                if(block.textList()){item=block;break;}
            if(!item.isValid())return 1;
            const QRect caret=editor.cursorRect(QTextCursor(item));
            const QPointF point(caret.left()-12,caret.center().y());
            QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(editor.viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(editor.viewport(),&release);
            const int expected=8+(prefix.contains('[')?prefix.indexOf('[')+2:0);
            if(editor.sourceSelection()!=qMakePair(expected,expected)||editor.markdown()!=source){
                qWarning()<<"Task marker caret"<<prefix<<editor.sourceSelection()<<expected;return 2;
            }
        }
        return 0;
    }
    if(app.arguments().contains("--inline-boundary-test")){
        // Exposed delimiters stay visible until the caret passes their outer edge.
        for(const QString &token:{QString("**Bold 123**"),QString("**Bold 123\\*\\* end**"),
                                  QString("**Bold 123\\\\**")}){
            for(const bool right:{false,true}){
                LiveEditor editor;
                const QString source="a "+token+" z";
                editor.setSource(source);
                const int begin=2,end=begin+token.size();
                int at=right?end-2:begin+2;
                editor.restoreSourceSelection(at,at);
                while(right?at<=end:at>=begin){
                    if(editor.toPlainText()!=source||editor.sourceSelection()!=qMakePair(at,at)){
                        qWarning()<<"Premature inline collapse"<<token<<right<<at<<editor.toPlainText();return 90;
                    }
                    QKeyEvent event(QEvent::KeyPress,right?Qt::Key_Right:Qt::Key_Left,Qt::NoModifier);
                    QApplication::sendEvent(&editor,&event);
                    at+=right?1:-1;
                }
                if(editor.toPlainText()==source||editor.markdown()!=source||
                   editor.sourceSelection()!=qMakePair(at,at)){
                    qWarning()<<"Inline did not collapse after exit"<<token<<right<<editor.toPlainText();return 91;
                }
            }
        }
        for(const bool backwards:{false,true}){
            for(const auto &entry:QList<QPair<QString,QString>>{
                {QString("before\n\n==的的==\n\n*的的三个*后文\n\nafter"),QString("before\n\n后文\n\nafter")},
                {QString("before\n\n前文==高亮==后文\n\nafter"),QString("before\n\n前文后文\n\nafter")}}){
                LiveEditor editor;editor.setSource(entry.first);editor.restoreSourceSelection(0,0);
                const QString visible=editor.toPlainText();
                const int start=visible.contains("前文")?visible.indexOf("前文")+2:visible.indexOf("的的");
                const int end=visible.indexOf("后文");
                QTextCursor cursor(editor.document());cursor.setPosition(backwards?end:start);
                cursor.setPosition(backwards?start:end,QTextCursor::KeepAnchor);editor.setTextCursor(cursor);
                QKeyEvent event(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
                QApplication::sendEvent(&editor,&event);
                if(editor.markdown()!=entry.second){qWarning()<<"Cross preview deletion"<<editor.markdown();return 6;}
            }
        }
        for(const QString &token:{QString("==的的=="),QString("*斜体*"),QString("**粗体**"),
                                  QString("~~删除线~~"),QString("`code`"),QString("$x+y$"),
                                  QString("[链接](https://example.com)"),QString("***嵌套***")}){
            for(const bool backwards:{false,true})
            for(const int key:{int(Qt::Key_Backspace),int(Qt::Key_Delete)}){
                LiveEditor editor;
                editor.setSource("before\n\n"+token+"\n\nafter");
                editor.restoreSourceSelection(0,0);
                const QString visible=editor.toPlainText();
                const int start=visible.indexOf('\n')+2;
                const int end=visible.lastIndexOf("\n\nafter");
                if(start<2||end<=start){qWarning()<<"Missing preview"<<token<<visible;return 4;}
                QTextCursor selected(editor.document());
                selected.setPosition(backwards?end:start);
                selected.setPosition(backwards?start:end,QTextCursor::KeepAnchor);
                editor.setTextCursor(selected);
                QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);
                QApplication::sendEvent(&editor,&event);
                if(editor.markdown()!="before\n\n\n\nafter"){
                    qWarning()<<"Preview deletion left syntax"<<token<<backwards<<editor.markdown();return 5;
                }
            }
        }
        for(const auto &entry:QList<QPair<QString,int>>{
            {"**bold**",2},{QString::fromUtf8("**粗体**"),2},{"*italic*",1},{"***bold***",3},
            {"`code`",1},{"``a`b``",2},{"~~gone~~",2},{"==highlight==",2},{"$x+y$",1}}){
            const QString token=entry.first;
            const int delimiter=entry.second;
            for(const auto modifiers:{Qt::NoModifier,Qt::KeypadModifier})
            for(const bool right:{true,false})
            for(const int distance:{1,2}){
                LiveEditor editor;
                const QString source="a "+token+" z";
                editor.setSource(source);
                editor.show();editor.setFocus();app.processEvents();
                auto caret=editor.textCursor();
                caret.setPosition(right?2-distance:editor.toPlainText().size()-2+distance);
                editor.setTextCursor(caret);
                const QString preview=editor.toPlainText();
                auto press=[&]{
                    const auto key=right?Qt::Key_Right:Qt::Key_Left;
                    QKeyEvent event(QEvent::KeyPress,key,modifiers);QApplication::sendEvent(&editor,&event);
                    QKeyEvent release(QEvent::KeyRelease,key,modifiers);QApplication::sendEvent(&editor,&release);
                    app.processEvents();
                };
                if(distance==2){
                    press();
                    if(editor.toPlainText()!=preview){qWarning()<<"Expanded before reaching boundary"<<token<<right;return 1;}
                }
                const int edge=right?2+delimiter:2+token.size()-delimiter;
                press();
                if(editor.toPlainText()!=source||editor.sourceSelection()!=qMakePair(edge,edge)){
                    qWarning()<<"Incorrect entry"<<token<<right<<editor.toPlainText()<<editor.sourceSelection();return 2;
                }
                press();
                const int next=edge+(right?1:-1);
                if(editor.sourceSelection()!=qMakePair(next,next)||editor.markdown()!=source)return 3;
            }
        }
        return 0;
    }
    if(app.arguments().contains("--live-pdf-test"))return runPdfTests(app);
    if(app.arguments().contains("--table-edit-test"))return runTableEditTests();
    if(app.arguments().contains("--live-csv-test")){
        QTemporaryDir files;if(!files.isValid())return 1;
        QFile file(files.filePath("sample.csv"));if(!file.open(QIODevice::WriteOnly))return 2;
        file.write("name,value,notes\nConcrete,128,\"hello, world\"\nSteel,12.5,\"sample \"\"A-01\"\"\"\n");file.close();
        LiveEditor editor;editor.resize(900,500);
        editor.document()->setBaseUrl(QUrl::fromLocalFile(files.path()+"/"));
        const QString source=QString::fromUtf8("# CSV 表格预览\n\n![工程材料](sample.csv)\n\n这里继续 Live 编辑。\n");
        editor.setSource(source);editor.show();
        auto imageState=[&](const QString &scheme){
            for(auto block=editor.document()->begin();block.isValid();block=block.next())
                for(auto it=block.begin();!it.atEnd();++it){
                    const auto format=it.fragment().charFormat();
                    if(format.isImageFormat()&&QUrl(format.toImageFormat().name()).scheme()==scheme)return true;
                }
            return false;
        };
        QElapsedTimer elapsed;elapsed.start();
        while(!imageState("csv-ready")&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
        if(!imageState("csv-ready")||editor.markdown()!=source)return 3;
        auto csvCursor=[&]{
            for(auto block=editor.document()->begin();block.isValid();block=block.next())
                for(auto it=block.begin();!it.atEnd();++it)
                    if(it.fragment().charFormat().isImageFormat()){
                        QTextCursor cursor(editor.document());cursor.setPosition(it.fragment().position());return cursor;
                    }
            return QTextCursor();
        };
        auto followingTop=[&]{
            for(auto block=editor.document()->begin();block.isValid();block=block.next())
                if(block.text().contains(QString::fromUtf8("这里继续")))return editor.document()->documentLayout()->blockBoundingRect(block).top();
            return qreal(-1);
        };
        for(int repeat=0;repeat<3;++repeat){
            editor.restoreSourceSelection(0,0);
            auto cursor=csvCursor();if(cursor.isNull())return 7;
            const qreal before=followingTop();
            editor.setTextCursor(cursor);app.processEvents();
            if(!imageState("csv-ready"))return 14;
            if(!editor.editResourceSourceAt(source.indexOf("![")))return 15;
            if(!editor.toPlainText().contains("![工程材料](sample.csv)"))return 8;
            if(qAbs(followingTop()-before)>2){qWarning()<<"CSV layout shift"<<before<<followingTop();return 9;}
            editor.restoreSourceSelection(0,0);
            if(!imageState("csv-ready")||qAbs(followingTop()-before)>2||editor.markdown()!=source)return 10;
        }
        editor.editResourceSourceAt(source.indexOf("!["));
        if(!editor.toPlainText().contains("![工程材料](sample.csv)"))return 4;
        editor.restoreSourceSelection(0,0);
        if(!imageState("csv-ready")||editor.markdown()!=source)return 5;
        editor.insertPlainText("X");if(!editor.markdown().startsWith("X#"))return 6;
        const QString longSource=QString("Before paragraph.\n\n").repeated(30)+
            "![工程材料](sample.csv)\n\nAfter resource.\n\nLast paragraph.\n";
        editor.setSource(longSource);editor.restoreSourceSelection(0,0);app.processEvents();
        for(int repeat=0;repeat<3;++repeat){
            editor.verticalScrollBar()->setValue(editor.verticalScrollBar()->maximum());
            elapsed.restart();
            while(!imageState("csv-ready")&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
            const int scroll=editor.verticalScrollBar()->value();
            auto cursor=csvCursor();if(cursor.isNull())return 11;
            const QPoint point=editor.cursorRect(cursor).center()+QPoint(8,0);
            QMouseEvent click(QEvent::MouseButtonPress,point,editor.viewport()->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(editor.viewport(),&click);app.processEvents();
            if(!imageState("csv-ready"))return 16;
            editor.editResourceSourceAt(longSource.indexOf("!["));
            if(!editor.toPlainText().contains("![工程材料](sample.csv)")||qAbs(editor.verticalScrollBar()->value()-scroll)>2)return 12;
            editor.restoreSourceSelection(longSource.indexOf("After resource"),longSource.indexOf("After resource"));
            if(!imageState("csv-ready")||qAbs(editor.verticalScrollBar()->value()-scroll)>2||editor.markdown()!=longSource)return 13;
        }
        return 0;
    }

    if(app.arguments().contains("--typing-style-test")){
        auto send=[](LiveEditor &editor,int key,const QString &text=QString()){
            QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier,text);QApplication::sendEvent(&editor,&event);
        };
        auto at=[](LiveEditor &editor,int position){
            QTextCursor cursor(editor.document());cursor.setPosition(position);
            cursor.setPosition(position+1,QTextCursor::KeepAnchor);return cursor.charFormat();
        };
        auto plain=[](const QTextCharFormat &format){
            return !format.fontItalic()&&format.fontWeight()!=QFont::Bold&&!format.fontStrikeOut()&&
                format.background().color()!=highlightBackground;
        };
        for(const auto &delimiter:{QString("*"),QString("**"),QString("***"),QString("_"),QString("__"),QString("___"),QString("~~"),QString("==")}){
            LiveEditor editor;editor.setSource("plain ");editor.restoreSourceSelection(6,6);
            QString expected="plain ";
            auto type=[&](const QString &text){
                for(const auto ch:text){send(editor,0,QString(ch));expected+=ch;}
                return editor.markdown()==expected;
            };
            if(!type(delimiter))return 80;
            for(const auto ch:QString::fromUtf8("a中文b")){
                if(!type(QString(ch)))return 81;
                const auto format=at(editor,editor.textCursor().position()-1);
                const bool emphasis=delimiter[0]=='*'||delimiter[0]=='_';
                if(format.fontItalic()!=(emphasis&&delimiter.size()!=2)||
                   (format.fontWeight()==QFont::Bold)!=(emphasis&&delimiter.size()>=2)||
                   format.fontStrikeOut()!=(delimiter=="~~")||
                   (format.background().color()==highlightBackground)!=(delimiter=="==")){
                    qWarning()<<"Typing style mismatch"<<delimiter<<expected;return 82;
                }
            }
            if(!type(delimiter)||!type(" normal"))return 83;
            if(!plain(at(editor,editor.textCursor().position()-1)))return 84;
            // Returning to an existing preview span must also style its source.
            editor.restoreSourceSelection(0,0);editor.restoreSourceSelection(6+delimiter.size()+1,6+delimiter.size()+1);
            const auto existing=at(editor,editor.textCursor().position());
            if(delimiter=="*"&&!existing.fontItalic())return 85;
            // An unfinished delimiter ends at Enter, not at the next paragraph.
            editor.setSource("plain ");editor.restoreSourceSelection(6,6);expected="plain ";
            if(!type(delimiter+"text"))return 86;
            send(editor,Qt::Key_Return,"\n");expected+='\n';
            if(!type("next")||!plain(at(editor,editor.textCursor().position()-1)))return 87;
        }
        LiveEditor editor;
        for(const auto &source:{QString("\\*literal"),QString("* item"),QString("`*code*`"),QString("```\n*code*\n```"),QString("$a*b$"),QString("snake_case")}){
            editor.setSource(source);editor.restoreSourceSelection(source.indexOf("code")>=0?source.indexOf("code"):0,source.indexOf("code")>=0?source.indexOf("code"):0);
            const auto hit=editor.document()->find(source.contains("code")?"code":source.contains("literal")?"literal":source.contains("item")?"item":source.contains("snake")?"snake":"a");
            if(!hit.isNull()&&!plain(hit.charFormat()))return 88;
        }
        editor.setSource("*text*");editor.restoreSourceSelection(6,6);
        send(editor,Qt::Key_Backspace);
        if(editor.markdown()!="*text"||!editor.document()->find("text").charFormat().fontItalic())return 89;
        send(editor,0,"x");
        if(editor.markdown()!="*textx"||!at(editor,editor.textCursor().position()-1).fontItalic())return 90;
        // Nested styles combine, then unwind independently when closed.
        editor.setSource("**bold *both* bold** plain");editor.restoreSourceSelection(10,10);
        auto both=editor.document()->find("both").charFormat();
        if(!both.fontItalic()||both.fontWeight()!=QFont::Bold||
           editor.document()->find("plain").charFormat().fontWeight()==QFont::Bold)return 95;
        editor.setSource("==**marked");editor.restoreSourceSelection(10,10);
        auto combined=editor.document()->find("marked").charFormat();
        if(combined.fontWeight()!=QFont::Bold||combined.background().color()!=highlightBackground)return 96;
        // Input-method commits receive the same styling as ordinary keystrokes.
        editor.setSource("==");editor.restoreSourceSelection(2,2);
        QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("中文"));QApplication::sendEvent(&editor,&commit);
        if(editor.markdown()!=QString::fromUtf8("==中文")||at(editor,3).background().color()!=highlightBackground)return 91;
        // Multiline insertion never carries an unfinished style past a newline.
        editor.setSource("*");editor.restoreSourceSelection(1,1);editor.insertPlainText("first\nsecond");
        if(editor.markdown()!="*first\nsecond"||!plain(editor.document()->find("second").charFormat()))return 92;
        MarkdownEditorWidget widget;widget.setMarkdown("*");widget.selectSource(1,1);
        auto *live=static_cast<LiveEditor *>(widget.findChild<QTextBrowser *>("markdownPreview"));
        send(*live,0,"x");const QString edited=widget.markdown();
        widget.document()->undo();widget.render();widget.selectSource(widget.markdown().size(),widget.markdown().size());
        if(widget.markdown()==edited||live->markdown()!=widget.markdown())return 93;
        widget.document()->redo();widget.render();widget.selectSource(edited.size(),edited.size());
        if(widget.markdown()!=edited||!live->document()->find("x").charFormat().fontItalic())return 94;
        return 0;
    }
    if(app.arguments().contains("--highlight-test")){
        QTextDocument doc;QStringList errors;
        const QString source="before ==yellow== after ==**bold**== and ==*italic*== and ==~~deleted~~==";
        renderMarkdown(&doc,source,errors);
        if(!errors.isEmpty()||doc.toPlainText()!="before yellow after bold and italic and deleted"){qWarning()<<doc.toPlainText()<<errors;return 60;}
        for(const auto &word:{QString("yellow"),QString("bold"),QString("italic"),QString("deleted")}){
            const auto found=doc.find(word);
            if(found.isNull()||found.charFormat().background().color()!=highlightBackground)return 61;
        }
        if(doc.find("bold").charFormat().fontWeight()!=QFont::Bold||
           !doc.find("italic").charFormat().fontItalic()||!doc.find("deleted").charFormat().fontStrikeOut()||
           doc.find("after").charFormat().background().style()!=Qt::NoBrush)return 62;
        for(const auto &literal:{QString("`==code==`"),QString("```\n==code==\n```"),
              QString("    ==code=="),QString("\\==escaped=="),QString("==unfinished"),QString("===="),QString("===literal===")}){
            renderMarkdown(&doc,literal,errors);
            if(!doc.toPlainText().contains("=="))return 63;
            for(auto block=doc.begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it)
                if(it.fragment().charFormat().background().color()==highlightBackground)return 64;
        }
        for(const auto &mixed:{QString("==***nested***=="),QString("**==nested==**"),QString("==first== ==second==")}){
            renderMarkdown(&doc,mixed,errors);
            if(doc.toPlainText().contains("==")||doc.toPlainText().contains("*"))return 69;
        }
        QTextDocument code;code.setPlainText("==yellow==\n```\n==code==\n```\n==**bold**==");
        Highlighter syntax(&code);syntax.rehighlight();
        auto marked=[&](int row){
            for(const auto &format:code.findBlockByNumber(row).layout()->formats())
                if(format.format.background().color()==highlightBackground)return true;
            return false;
        };
        if(!marked(0)||marked(2)||!marked(4))return 70;
        {
            const QString links="[site](https://example.com/a_(b)) <https://example.com> [ref][id]";
            QTextDocument sourceDoc;sourceDoc.setPlainText(links+"\n`https://example.com`\n```\nhttps://example.com\n```");
            Highlighter linkSyntax(&sourceDoc);linkSyntax.rehighlight();
            auto colorAt=[&](int row,int offset){
                for(const auto &range:sourceDoc.findBlockByNumber(row).layout()->formats())
                    if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();
                return QColor();
            };
            if(colorAt(0,2)!=QColor("#2468b4")||colorAt(0,15)!=QColor("#2468b4")||
               colorAt(0,links.indexOf("ref"))!=QColor("#2468b4")||
               colorAt(1,2)==QColor("#2468b4")||colorAt(3,2)==QColor("#2468b4"))return 71;
            LiveEditor linkEditor;linkEditor.setSource("[site](https://example.com)");
            linkEditor.restoreSourceSelection(3,3);
            if(linkEditor.document()->find("site").charFormat().foreground().color()!=QColor("#2468b4"))return 72;
        }

        LiveEditor editor;editor.resize(800,500);editor.setSource(source);
        if(editor.toPlainText()!= "before yellow after bold and italic and deleted")return 65;
        editor.restoreSourceSelection(11,11);
        if(!editor.toPlainText().contains("==yellow==")||editor.markdown()!=source)return 66;
        QKeyEvent typing(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
        QApplication::sendEvent(&editor,&typing);
        QString expected=source;expected.insert(11,"x");
        if(editor.markdown()!=expected)return 67;
        editor.restoreSourceSelection(0,0);
        if(editor.toPlainText().contains("==")||editor.markdown()!=expected)return 68;
        return 0;
    }
    if(app.arguments().contains("--rule-test")){
        // Setext is disabled in both the shared renderer and live preview.
        for(const QString &sample:{QString("before\n---\nafter"),QString("**before**\n-----\nafter")}){
            QTextDocument document;QStringList errors;renderMarkdown(&document,sample,errors);
            int rules=0;
            for(auto block=document.begin();block.isValid();block=block.next()){
                if(block.blockFormat().headingLevel())return 6;
                if(block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))++rules;
            }
            if(rules!=1||!errors.isEmpty())return 7;
            LiveEditor editor;editor.setSource(sample);
            const auto rule=editor.document()->findBlockByNumber(1);
            if(!rule.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)||
               editor.document()->blockCount()!=3||editor.markdown()!=sample)return 8;
            editor.restoreSourceSelection(sample.indexOf('-'),sample.indexOf('-'));
            if(editor.textCursor().block().text()!=sample.split('\n')[1]||editor.markdown()!=sample)return 9;
        }
        for(const QString &sample:{QString("before\n===\nafter"),QString("> before\n> ==="),
                                  QString("before\n--\nafter"),QString("before\n=\nafter")}){
            QTextDocument document;QStringList errors;renderMarkdown(&document,sample,errors);
            for(auto block=document.begin();block.isValid();block=block.next())
                if(block.blockFormat().headingLevel())return 10;
            if(!document.toPlainText().contains(sample.contains('=')?"=":"--"))return 11;
        }
        for(const QString &sample:{QString("```\nbefore\n---\n===\n```"),QString("    before\n    ---\n    ===")}){
            QTextDocument document;QStringList errors;renderMarkdown(&document,sample,errors);
            if(!document.toPlainText().contains("---")||!document.toPlainText().contains("==="))return 12;
        }
        QTextDocument heading;QStringList errors;renderMarkdown(&heading,"## heading",errors);
        if(heading.begin().blockFormat().headingLevel()!=2)return 13;
        for(const QString &sample:{QString("---\nafter"),QString("---\n---"),QString("# heading\n---\nafter")}){
            LiveEditor editor;editor.setSource(sample);
            if(editor.document()->blockCount()!=sample.count('\n')+1){
                qWarning()<<"Rule inserted extra blocks"<<sample<<editor.toPlainText();return 5;
            }
        }
        for(const QString &marker:{QString("---"),QString("***"),QString("___")}){
            LiveEditor editor;editor.resize(640,100);editor.show();
            const QString source="before\n\n"+marker+"\n\nafter";
            editor.setSource(source);app.processEvents();
            auto rule=editor.document()->findBlockByNumber(2);
            if(editor.document()->blockCount()!=5||
               !rule.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))return 1;
            const auto bounds=editor.document()->documentLayout()->blockBoundingRect(rule);
            const auto body=editor.document()->documentLayout()->blockBoundingRect(editor.document()->begin());
            if(bounds.height()>body.height()+1)return 2;
            for(const qreal fraction:{0.25,0.75,0.98}){
                editor.setSource("");editor.setSource(source);app.processEvents();
                rule=editor.document()->findBlockByNumber(2);
                const auto rect=editor.document()->documentLayout()->blockBoundingRect(rule);
                editor.restoreSourceSelection(0,0);
                const auto after=editor.document()->documentLayout()->blockBoundingRect(editor.document()->lastBlock());
                const qreal previousY=after.top()-editor.verticalScrollBar()->value();
                const QPointF point(300,rect.top()+fraction*rect.height()-editor.verticalScrollBar()->value());
                QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(editor.viewport(),&press);
                QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(editor.viewport(),&release);
                if(editor.sourceSelection().second!=8||editor.textCursor().block().text()!=marker)return 3;
                const auto next=editor.document()->documentLayout()->blockBoundingRect(editor.document()->lastBlock());
                if(qAbs(next.top()-editor.verticalScrollBar()->value()-previousY)>1)return 4;
            }
        }
        return 0;
    }
    if(app.arguments().contains("--line-spacing-test")){
        LiveEditor editor;editor.resize(900,700);editor.show();
        const QString source="alpha\nbeta\n\n---\n---\n\n***\n\n___\n\n \n\n**bold** text\nomega\n";
        for(int size:{9,14,24}){
            editor.setFont(QFont(previewFont,size));editor.setSource(source);editor.refreshAppearance();
            auto verify=[&]{
                auto *layout=editor.document()->documentLayout();
                const qreal expected=bodyLineHeight(editor.font());
                qreal previous=-1;
                for(auto block=editor.document()->begin();block.isValid();block=block.next()){
                    const auto rect=layout->blockBoundingRect(block);
                    if(previous>=0&&qAbs(rect.top()-previous-expected)>1){
                        qWarning()<<"Uneven line spacing"<<size<<block.blockNumber()<<rect.top()-previous<<expected;
                        return false;
                    }
                    previous=rect.top();
                }
                return editor.markdown()==source;
            };
            if(!verify())return 50;
            for(int at=0;at<source.size();++at)if(at==0||source[at-1]=='\n'){
                editor.restoreSourceSelection(at,at);
                if(!verify())return 51;
            }
        }
        return 0;
    }
    if(app.arguments().contains("--formula-vector-test")){
        QTextDocument original,vector,raster;QStringList errors;
        original.setDefaultFont(QFont(previewFont,14));
        if(renderMarkdown(&original,R"($E=mc^2$ and $x_i^2$

$$\frac{1}{2}+\sqrt{x}+\int_0^1 x^2\,dx$$)",errors)!=3||!errors.isEmpty())return 40;
        for(auto *doc:{&vector,&raster}){
            doc->setDefaultFont(original.defaultFont());
            installFormulaImageHandler(doc);
            QTextCursor(doc).insertFragment(QTextDocumentFragment(&original));
            doc->setTextWidth(600);
        }
        int count=0;
        for(auto block=raster.begin();block.isValid();block=block.next())
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();auto format=fragment.charFormat();
                if(!format.isImageFormat())continue;
                if(!format.hasProperty(formulaVector))return 41;
                ++count;
                const QUrl url(format.toImageFormat().name());
                raster.addResource(QTextDocument::ImageResource,url,original.resource(QTextDocument::ImageResource,url));
                format.clearProperty(formulaVector);format.clearProperty(formulaVectorSize);
                QTextCursor cursor(&raster);cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);cursor.setCharFormat(format);
            }
        if(count!=3)return 42;
        for(int dpr:{1,2,3}){
            QList<QRect> bounds;
            for(auto *doc:{&vector,&raster}){
                QImage image(QSize(600*dpr,200*dpr),QImage::Format_ARGB32_Premultiplied);
                image.setDevicePixelRatio(dpr);image.fill(Qt::white);
                QPainter painter(&image);doc->drawContents(&painter);painter.end();
                QRect ink;
                for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
                    if(qGray(image.pixel(x,y))<200)ink=ink.united(QRect(x,y,1,1));
                bounds.append(ink);
                if(doc==&vector&&dpr==2)image.save("formula-vector.png");
            }
            if(bounds[0].isEmpty()||qAbs(bounds[0].right()-bounds[1].right())>3*dpr||
               qAbs(bounds[0].bottom()-bounds[1].bottom())>3*dpr)return 43;
        }
        return 0;
    }
    if(app.arguments().contains("--performance-test")){
        QFile file(app.arguments().value(app.arguments().indexOf("--performance-test")+1));
        if(!file.open(QIODevice::ReadOnly))return 30;
        const QString source=QString::fromUtf8(file.readAll());
        LiveEditor editor;editor.resize(900,700);editor.show();
        QElapsedTimer timer;timer.start();editor.setSource(source);app.processEvents();
        const auto load=timer.elapsed();
        timer.restart();
        for(int i=0;i<12;++i){
            editor.restoreSourceSelection(i*int(source.size())/12,i*int(source.size())/12);
            app.processEvents();
        }
        qInfo()<<"Load ms:"<<load<<"; average caret switch ms:"<<timer.elapsed()/12.0;
        if(editor.markdown()!=source)return 31;
        const auto selection=editor.sourceSelection();
        QKeyEvent typing(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
        timer.restart();QApplication::sendEvent(&editor,&typing);app.processEvents();
        qInfo()<<"Typing ms:"<<timer.elapsed();
        QString expected=source;expected.insert(selection.second,"x");
        if(editor.markdown()!=expected)return 32;
        return 0;
    }
    if(app.arguments().contains("--embedded-editor-test")){
        QWidget host;
        auto *layout=new QHBoxLayout(&host);
        auto *first=new MarkdownEditorWidget(&host);
        auto *second=new MarkdownEditorWidget(&host);
        layout->addWidget(first);layout->addWidget(second);
        first->setBaseUrl(QUrl::fromLocalFile(QDir::tempPath()+"/"));
        first->setMarkdown("alpha\nbeta");second->setMarkdown("independent");
        host.resize(900,500);host.show();app.processEvents();
        int changes=0;
        QObject::connect(first,&MarkdownEditorWidget::textChanged,&host,[&]{++changes;});
        first->selectSource(0,5);first->formatSelection("**","**");
        if(first->markdown()!="**alpha**\nbeta"||second->markdown()!="independent"||
           !first->document()->isModified()||changes!=1)return 10;
        first->setLiveMode(false);
        if(first->selection()!=qMakePair(2,7)||first->isLiveMode()||!second->isLiveMode())return 11;
        first->document()->undo();first->render();
        if(first->markdown()!="alpha\nbeta")return 12;
        first->setLiveMode(true);first->selectSource(0,5);
        first->editingActions().first()->trigger();
        if(first->markdown()!="**alpha**\nbeta")return 13;
        for(auto *action:first->editingActions())
            if(action->shortcutContext()!=Qt::WidgetWithChildrenShortcut)return 14;
        if(first->liveModeAction()->shortcutContext()!=Qt::WidgetWithChildrenShortcut||
           first->findChild<QMenuBar *>()||first->findChild<QToolBar *>())return 15;
        first->applyBaseFontSize(19);
        if(!first->document()->isModified())return 16;
        first->setMarkdown("replacement");
        if(first->document()->isModified()||first->markdown()!="replacement")return 17;
        // Drop the previous document's resource base when opening a memory-only record.
        second->setBaseUrl(QUrl::fromLocalFile(QDir::tempPath()+"/"));
        second->setMarkdown("数据库正文",QUrl{});
        auto *secondPreview=second->findChild<QTextBrowser *>("markdownPreview");
        if(!secondPreview||!secondPreview->document()->baseUrl().isEmpty()||
           second->document()->isModified())return 19;
        second->selectSource(0,3);
        second->formatSelection("**","**");
        if(second->markdown()!="**数据库**正文"||!second->document()->isModified())return 20;
        second->document()->setModified(false);
        if(second->document()->isModified())return 21;
        QWidget otherHost;
        first->setParent(&otherHost);first->show();first->setFocus();app.processEvents();
        if(first->parentWidget()!=&otherHost||first->markdown()!="replacement")return 18;
        // Destroy an editor while its delayed render is still pending.
        first->replaceSource(0,0,"x",1,1);delete first;app.processEvents();
        return 0;
    }
    if(app.arguments().contains("--code-background-test")){
        LiveEditor editor;editor.resize(640,480);
        editor.document()->setDocumentMargin(24);
        const QString source="Before\n\n```cpp\nint x = 1;\n\n"+QString("value ").repeated(30)+"\n```\n\nAfter";
        editor.setSource(source);editor.show();app.processEvents();
        auto verify=[&]{
            const auto first=editor.document()->find("```").block();
            const auto last=editor.document()->find("```",editor.document()->characterCount()-1,QTextDocument::FindBackward).block();
            const auto image=editor.viewport()->grab().toImage();
            image.save("code-background.png");
            const qreal scale=image.devicePixelRatio();
            const int x=qRound((editor.viewport()->width()-32)*scale);
            const int top=qCeil(editor.document()->documentLayout()->blockBoundingRect(first).top());
            const int bottom=qFloor(editor.document()->documentLayout()->blockBoundingRect(last).bottom())-1;
            for(int y=top;y<=bottom;++y)
                if(image.pixelColor(x,qRound(y*scale))!=QColor("#f3f5f4"))return false;
            return editor.markdown()==source;
        };
        if(!verify())return 7;
        editor.setTextCursor(QTextCursor(editor.document()->findBlockByNumber(3)));
        app.processEvents();
        if(!verify())return 8;
        editor.grab().save("code-background.png");
        return 0;
    }
    if(!app.arguments().contains("--table-gutter-test") &&
       !app.arguments().contains("--edit-menu-test") &&
       !app.arguments().contains("--smoke-test"))return std::nullopt;
    if(app.arguments().contains("--table-gutter-test")){
        LiveEditor tableView;tableView.setFont(QFont(previewFont,11));tableView.resize(640,480);tableView.show();
        const QString markdown="Before\n\n| Name | Value | Extra |\n| --- | --- | --- |\n| Alpha | One | A |\n| Beta | Two | B |\n\nAfter";
        tableView.setSource(markdown);app.processEvents();
        int labels=0,cells=0;
        for(auto block=tableView.document()->begin();block.isValid();block=block.next()){
            QTextCursor cursor(block);
            if(const auto table=cursor.currentTable()){
                const auto cell=table->cellAt(cursor);
                const int label=tableView.gutterLineNumberAt(cursor);
                if((label>0)!=(cell.column()==0))return 5;
                if(label>0&&label!=3)return 7;
                labels+=label>0;++cells;
            }
        }
        if(cells!=9||labels!=3||tableView.markdown()!=markdown)return 6;
        tableView.grab().save("table-gutter.png");
        return 0;
    }
    int result=0;
    if(app.arguments().contains("--edit-menu-test")){
        Window commands;
        auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
        auto *live=commands.findChild<QTextBrowser *>("markdownPreview");
        bool ok=source&&live;
        const QString original="alpha\nbeta\n\n# Tail heading";
        QTemporaryFile menuFixture;
        if(!menuFixture.open())return 4;
        menuFixture.write(original.toUtf8());menuFixture.flush();
        commands.load(menuFixture.fileName());
        for(const auto reason:{Qt::PopupFocusReason,Qt::MenuBarFocusReason,Qt::OtherFocusReason}){
            source->setPlainText(original);commands.selectSource(1,4);
            QFocusEvent focusOut(QEvent::FocusOut,reason);
            QApplication::sendEvent(live,&focusOut);
            ok=ok&&commands.selection()==qMakePair(1,4);
            for(auto *action:commands.findChildren<QAction *>())if(action->text()==QString::fromUtf8("加粗"))action->trigger();
            ok=ok&&source->toPlainText()=="a**lph**a\nbeta\n\n# Tail heading"&&
                commands.selection()==qMakePair(3,6)&&!live->toPlainText().contains("# Tail heading");
        }
        for(const auto &markers:QList<QPair<QString,QString>>{{"**","**"},{"*","*"},{"`","`"},{"[","](url)"}}){
            source->setPlainText(original);commands.selectSource(0,5);
            commands.formatSelection(markers.first,markers.second);
            ok=ok&&source->toPlainText().startsWith(markers.first+"alpha"+markers.second)&&
                commands.selection()==qMakePair(int(markers.first.size()),int(markers.first.size())+5)&&
                !live->toPlainText().contains("# Tail heading");
        }
        source->setPlainText(original);commands.selectSource(0,6);commands.indentSelection(false);
        ok=ok&&source->toPlainText().startsWith("    alpha")&&!live->toPlainText().contains("# Tail heading");
        commands.indentSelection(true);
        ok=ok&&source->toPlainText()==original&&!live->toPlainText().contains("# Tail heading");
        commands.selectSource(6,10);commands.clipboardParagraph(false);
        ok=ok&&QApplication::clipboard()->text()=="beta"&&!live->toPlainText().contains("# Tail heading");
        commands.clipboardParagraph(true);
        ok=ok&&!source->toPlainText().contains("beta")&&!live->toPlainText().contains("# Tail heading");
        commands.selectSource(source->toPlainText().size(),0);
        ok=ok&&!live->toPlainText().contains("# Tail heading");
        source->setPlainText(original);commands.selectSource(0,0);commands.showFind(true);
        auto *dialog=commands.findChild<QDialog *>("findDialog");
        if(!dialog)dialog=commands.findChild<QDialog *>();
        const auto fields=dialog->findChildren<QLineEdit *>();
        fields[0]->setText("beta");fields[1]->setText("changed");
        auto click=[&](const QString &label){for(auto *button:dialog->findChildren<QPushButton *>())if(button->text()==label)button->click();};
        click("下一个");
        ok=ok&&commands.selection()==qMakePair(6,10)&&!live->toPlainText().contains("# Tail heading");
        click("全部替换");
        ok=ok&&source->toPlainText().contains("changed")&&!live->toPlainText().contains("# Tail heading");
        dialog->close();
        QString deletionDetails;
        {
            LiveEditor deletionProbe;
            const QString inlineSource="Before **hh** and **keep** after\n\n# Tail heading";
            for(const int key:{Qt::Key_Backspace,Qt::Key_Delete}){
                deletionProbe.setSource(inlineSource);
                deletionProbe.restoreSourceSelection(7,13);
                QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
                QApplication::sendEvent(&deletionProbe,&remove);
                deletionDetails+=QString::number(key)+" SOURCE: "+deletionProbe.markdown()+" VIEW: "+deletionProbe.toPlainText()+"\n";
                ok=ok&&deletionProbe.markdown()=="Before  and **keep** after\n\n# Tail heading"&&
                    !deletionProbe.toPlainText().contains("**keep**")&&
                    !deletionProbe.toPlainText().contains("# Tail heading");
            }
            deletionProbe.setSource(inlineSource);
            deletionProbe.restoreSourceSelection(7,13);
            QInputMethodEvent commit;commit.setCommitString("text");
            QApplication::sendEvent(&deletionProbe,&commit);
            deletionDetails+="IME SOURCE: "+deletionProbe.markdown()+" VIEW: "+deletionProbe.toPlainText()+"\n";
            ok=ok&&deletionProbe.markdown()=="Before text and **keep** after\n\n# Tail heading"&&
                !deletionProbe.toPlainText().contains("# Tail heading");
            deletionProbe.setSource("Before **keep** and **unfinished\n\n# Tail heading");
            deletionProbe.restoreSourceSelection(30,30);
            deletionDetails+="UNFINISHED VIEW: "+deletionProbe.toPlainText()+"\n";
            ok=ok&&!deletionProbe.toPlainText().contains("**keep**")&&
                !deletionProbe.toPlainText().contains("# Tail heading");
        }
        QFile report(QDir::current().absoluteFilePath("edit-menu-result.txt"));
        if(report.open(QIODevice::WriteOnly)){report.write(ok?"PASS\n":"FAIL\n");report.write(deletionDetails.toUtf8());}
        return ok?0:1;
    }
    if(app.arguments().contains("--smoke-test")){
        QTextBrowser view;view.resize(900,700);view.setFont(QFont(previewFont,13));QStringList errors;
        int n=renderMarkdown(view.document(),QString::fromUtf8(R"(# Test
Inline $E=mc^2$ and $\frac{a}{b}$.

$$\int_0^1 x^2\,dx = \frac{1}{3}$$

$$\begin{pmatrix}a & b \\ c & d\end{pmatrix}$$

`$literal$`

```tex
$notmath$
```
)"),errors);
        bool images=n==4&&errors.isEmpty()&&view.toPlainText().contains("$literal$")&&view.toPlainText().contains("$notmath$");
        // Regressions: delimiters, multi-line highlighting, and paragraph isolation.
        const QString sample=QString::fromUtf8(R"(前 $x^2$ 后
$$
\frac{1}{2}
$$
普通正文
`$code$` and \$literal\$
```tex
$$not math$$
```
$$$$
)");
        const auto spans=scanSpans(sample);int inlineCount=0,displayCount=0;
        for(const auto &span:spans){if(span.kind==SpanKind::InlineMath)++inlineCount;if(span.kind==SpanKind::DisplayMath)++displayCount;}
        images=images&&inlineCount==1&&displayCount==1;
        images=images&&scanSpans("```tex\n$x$\n```").size()==1;
        images=images&&scanSpans("```tex\n$x$").size()==1;
        for(const QString &nested:{QString("- item\n  ```cpp\n  $code$\nOutside $math$"),
                QString("  - item\n    ```cpp\n    $code$\n    ```\nOutside $math$")}){
            const auto protectedSpans=scanSpans(nested);
            images=images&&protectedSpans.size()==2&&protectedSpans.first().fenced&&
                protectedSpans.last().kind==SpanKind::InlineMath&&
                nested.mid(protectedSpans.last().start,protectedSpans.last().length)=="$math$";
        }
        QTextDocument doc;doc.setPlainText(sample);Highlighter highlighter(&doc);highlighter.rehighlight();
        auto colorAt=[&](int block,int offset){for(const auto &range:doc.findBlockByNumber(block).layout()->formats())if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();return QColor();};
        images=images&&colorAt(1,0)==QColor("#14616b")&&colorAt(2,0)==QColor("#14616b")&&colorAt(3,0)==QColor("#14616b");
        images=images&&!colorAt(4,0).isValid()&&colorAt(7,0)==QColor("#276746");
        {
            QTextDocument codeDocument;
            codeDocument.setPlainText("```cpp\nint value = 42; /* comment\nreturn 1; */\n```\nAfter");
            Highlighter syntax(&codeDocument);syntax.rehighlight();
            auto tokenColor=[&](int row,int offset){
                for(const auto &range:codeDocument.findBlockByNumber(row).layout()->formats())
                    if(offset>=range.start&&offset<range.start+range.length)return range.format.foreground().color();
                return QColor();
            };
            images=images&&tokenColor(1,0)==QColor("#8959a8")&&tokenColor(1,12)==QColor("#c76b29")&&
                tokenColor(2,0)==QColor("#8e908c")&&!tokenColor(4,0).isValid();
        }
        QTextBrowser isolation;isolation.setFont(view.font());isolation.resize(900,700);QStringList isolationErrors;
        images=images&&renderMarkdown(isolation.document(),QString::fromUtf8("前文 $$\\frac{a}{b}$$ 后文 $x^2$ 结束"),isolationErrors)==2&&isolationErrors.isEmpty();
        bool centeredImage=false,inlineImage=false;
        for(auto block=isolation.document()->begin();block.isValid();block=block.next()){
            if(block.text().contains(QChar::ObjectReplacementCharacter)){
                if(block.blockFormat().alignment().testFlag(Qt::AlignHCenter))centeredImage=block.text().trimmed()==QString(QChar::ObjectReplacementCharacter);
                else if(block.text().contains(QString::fromUtf8("后文"))){for(auto i=block.begin();!i.atEnd();++i)if(i.fragment().charFormat().isImageFormat())inlineImage=i.fragment().charFormat().toImageFormat().width()<100;}
            }
            if(block.text().contains(QString::fromUtf8("前文")))images=images&&!block.blockFormat().alignment().testFlag(Qt::AlignHCenter);
        }
        images=images&&centeredImage&&inlineImage;
        // Verify edits really reach the preview through the debounce timer.
        auto checkLive=[&](bool ok,int line){if(!ok){qWarning()<<"Live check failed at"<<line;QFile log(QDir::current().absoluteFilePath("live-failures.txt"));if(log.open(QIODevice::Append))log.write(QByteArray::number(line)+"\n");}return ok;};
        {
            Window commands;
            auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
            auto *mode=commands.findChild<QAction *>("livePreviewAction");
            for(bool liveMode:{true,false}){
                mode->setChecked(liveMode);source->setPlainText("alpha\nbeta");
                commands.selectSource(0,5);commands.formatSelection("**","**");
                images=checkLive(source->toPlainText()=="**alpha**\nbeta"&&commands.selection()==qMakePair(2,7),__LINE__)&&images;
                commands.formatSelection("**","**");
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
                commands.selectSource(0,6);commands.indentSelection(false);
                images=checkLive(source->toPlainText()=="    alpha\nbeta"&&commands.selection()==qMakePair(4,10),__LINE__)&&images;
                commands.indentSelection(true);
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
                commands.selectSource(7,7);commands.clipboardParagraph(true);
                images=checkLive(source->toPlainText()=="alpha\n"&&QApplication::clipboard()->text()=="beta",__LINE__)&&images;
                source->undo();commands.render();
                images=checkLive(source->toPlainText()=="alpha\nbeta",__LINE__)&&images;
            }
        }
        {
            Window commands;
            auto *source=commands.findChild<QPlainTextEdit *>("markdownEditor");
            source->setPlainText("Alpha alpha");commands.selectSource(0,0);commands.showFind(true);
            auto *dialog=commands.findChild<QDialog *>();
            const auto fields=dialog->findChildren<QLineEdit *>();fields[0]->setText("alpha");fields[1]->setText("X");
            auto click=[dialog](const QString &label){for(auto *button:dialog->findChildren<QPushButton *>())if(button->text()==label)button->click();};
            click("下一个");images=checkLive(commands.selection()==qMakePair(0,5),__LINE__)&&images;
            click("下一个");images=checkLive(commands.selection()==qMakePair(6,11),__LINE__)&&images;
            click("下一个");images=checkLive(commands.selection()==qMakePair(0,5),__LINE__)&&images;
            click("全部替换");images=checkLive(source->toPlainText()=="X X",__LINE__)&&images;
            source->undo();images=checkLive(source->toPlainText()=="Alpha alpha",__LINE__)&&images;
            dialog->close();
        }
        {
            Window settingsWindow;
            settingsWindow.findChild<MarkdownEditorWidget *>()->setLiveMode(true);
            auto *source=settingsWindow.findChild<QPlainTextEdit *>("markdownEditor");
            auto *liveView=static_cast<LiveEditor *>(settingsWindow.findChild<QTextBrowser *>("markdownPreview"));
            source->setPlainText("# Heading\n\nBody");settingsWindow.selectSource(2,9);
            source->document()->setModified(false);
            settingsWindow.showSettings();
            auto *dialog=settingsWindow.findChild<QDialog *>("settingsDialog");
            auto *size=dialog->findChild<QSpinBox *>("baseFontSize");size->setValue(17);
            images=checkLive(source->font().pointSize()==18&&liveView->font().pointSize()==19&&
                source->toPlainText()=="# Heading\n\nBody"&&!source->document()->isModified()&&
                settingsWindow.selection()==qMakePair(2,9),__LINE__)&&images;
            const auto heading=liveView->document()->begin().begin().fragment().charFormat();
            images=checkLive(heading.fontPointSize()==38&&!heading.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            Window restored;
            images=checkLive(restored.findChild<QTextBrowser *>("markdownPreview")->font().pointSize()==19,__LINE__)&&images;
            dialog->close();QSettings().remove("appearance/baseFontSize");
        }
        Window live;
        live.findChild<MarkdownEditorWidget *>()->setLiveMode(true);
        auto *edit=live.findChild<QPlainTextEdit *>("markdownEditor");
        auto *output=live.findChild<QTextBrowser *>("markdownPreview");
        auto *toggle=live.findChild<QAction *>("livePreviewAction");
        auto settle=[] {QEventLoop loop;QTimer::singleShot(250,&loop,&QEventLoop::quit);loop.exec();};
        images=checkLive(live.windowTitle()=="MdEdit"&&edit->toPlainText().isEmpty()&&
            edit->placeholderText().isEmpty()&&output->toPlainText().isEmpty()&&!edit->document()->isModified(),__LINE__)&&images;
        images=checkLive(toggle->isChecked()&&edit->isHidden()&&!output->isHidden(),__LINE__)&&images;
        edit->setPlainText("# Live heading\n\nFirst $x^2$");settle();
        images=checkLive(output->toPlainText().contains("Live heading")&&!output->toPlainText().contains("$x^2$"),__LINE__)&&images;
        edit->selectAll();edit->insertPlainText("Updated **content**");settle();
        images=checkLive(output->toPlainText().contains("Updated content")&&!output->toPlainText().contains("Live heading"),__LINE__)&&images;
        edit->undo();settle();images=checkLive(output->toPlainText().contains("Live heading"),__LINE__)&&images;
        toggle->setChecked(false);edit->setPlainText("Hidden edit");settle();
        images=checkLive(output->isHidden()&&!edit->isHidden(),__LINE__)&&images;
        toggle->setChecked(true);images=checkLive(!output->isHidden()&&output->toPlainText().contains("Hidden edit"),__LINE__)&&images;
        edit->clear();settle();images=checkLive(output->toPlainText().isEmpty(),__LINE__)&&images;
        auto *liveOutput=static_cast<LiveEditor *>(output);
        edit->setPlainText("# Heading\n\nBody $x^2$");settle();
        auto caret=output->textCursor();caret.setPosition(2);output->setTextCursor(caret);
        images=checkLive(output->toPlainText().startsWith("# Heading"),__LINE__)&&images;
        output->insertPlainText("X");settle();
        images=checkLive(edit->toPlainText().startsWith("# HeXading")&&liveOutput->markdown()==edit->toPlainText(),__LINE__)&&images;
        caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
        images=checkLive(!output->toPlainText().startsWith("#")&&output->toPlainText().contains("$x^2$"),__LINE__)&&images;
        // Clicking visible text must account for hidden Markdown punctuation.
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        images=checkLive(output->textCursor().position()==2,__LINE__)&&images;
        // Keep the same visible blank lines when activating a paragraph.
        for(const QString &first:{QString("First"),QString("# Heading")}){
            const QString original=first+"\n\nSecond\n\nThird";
            edit->setPlainText(original);settle();
            const int blocks=output->document()->blockCount();
            const auto next=output->document()->findBlock(output->toPlainText().indexOf("Second"));
            const qreal beforeY=output->document()->documentLayout()->blockBoundingRect(next).top();
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            const auto after=output->document()->findBlock(output->toPlainText().indexOf("Second"));
            const qreal afterY=output->document()->documentLayout()->blockBoundingRect(after).top();
            images=checkLive(blocks==5&&output->document()->blockCount()==blocks&&qAbs(beforeY-afterY)<1.0&&
                edit->toPlainText()==original,__LINE__)&&images;
            output->insertPlainText("X");
            images=checkLive(edit->toPlainText().contains("\n\nSecond\n\nThird"),__LINE__)&&images;
            caret=output->textCursor();caret.movePosition(QTextCursor::End);output->setTextCursor(caret);
            images=checkLive(output->toPlainText().contains("\n\n")&&
                edit->toPlainText().contains("\n\nSecond\n\nThird"),__LINE__)&&images;
        }
        // Enter must use the editor behavior, and existing soft breaks must
        // keep the following paragraph at the same height when activated.
        edit->setPlainText("First\nSecond\n\nThird");settle();
        auto following=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        const qreal restingY=output->document()->documentLayout()->blockBoundingRect(following).top();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));
        images=checkLive(liveOutput->lineNumberAt(caret)==2,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        following=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        images=checkLive(qAbs(restingY-output->document()->documentLayout()->blockBoundingRect(following).top())<1.0,__LINE__)&&images;
        caret=output->textCursor();caret.movePosition(QTextCursor::EndOfBlock);output->setTextCursor(caret);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
        QApplication::sendEvent(output,&enter);
        images=checkLive(edit->toPlainText()=="First\n\nSecond\n\nThird"&&
            liveOutput->lineNumberForBlock(output->textCursor().block())==2,__LINE__)&&images;
        edit->setPlainText("First _italic_\nSecond _italic_\nThird");settle();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));output->setTextCursor(caret);
        images=checkLive(output->toPlainText()=="First italic\nSecond _italic_\nThird",__LINE__)&&images;
        const QString adjacent="# Markdown 综合测试\n用 MdEdit 打开本文件，按 **Ctrl+E** 切换编辑和预览。\n## ## 1. 中文与基本文字";
        edit->setPlainText(adjacent);settle();
        const QStringList rawLines=adjacent.split('\n');
        const QStringList previewLines={"Markdown 综合测试","用 MdEdit 打开本文件，按 Ctrl+E 切换编辑和预览。","## 1. 中文与基本文字"};
        const QStringList targets={"Markdown","Ctrl+E","中文与基本文字"};
        for(int i=0;i<targets.size();++i){
            caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf(targets[i]));output->setTextCursor(caret);
            QStringList expected=previewLines;
            expected[i]=rawLines[i];
            images=checkLive(output->toPlainText()==expected.join('\n')&&edit->toPlainText()==adjacent,__LINE__)&&images;
        }
        // Repeated Enter keeps the caret on each new blank line and inserts
        // before any existing whitespace-only lines, even when hidden.
        for(const QString &tail:{QString(),QString("\n \t\n\n# Next")}){
            edit->setPlainText("First"+tail);settle();
            caret=output->textCursor();caret.setPosition(5);output->setTextCursor(caret);
            for(int count=1;count<=3;++count){
                QKeyEvent nextEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
                QApplication::sendEvent(output,&nextEnter);
                const QString expected="First"+QString(count,'\n')+tail;
                images=checkLive(edit->toPlainText()==expected&&liveOutput->markdown()==expected&&
                    output->textCursor().block().text().isEmpty()&&
                    liveOutput->lineNumberAt(output->textCursor())==count+1,__LINE__)&&images;
            }
            output->insertPlainText("New");
            images=checkLive(edit->toPlainText()=="First\n\n\nNew"+tail,__LINE__)&&images;
        }
        // Leading, consecutive, whitespace-only and trailing blank lines survive.
        const QString spaced="\nFirst\n\n \t\n\nSecond\n";
        edit->setPlainText(spaced);settle();
        images=checkLive(output->toPlainText()=="\nFirst\n\n\n\nSecond\n"&&
            output->document()->blockCount()==7&&edit->toPlainText()==spaced,__LINE__)&&images;
        const auto blank=output->document()->findBlockByNumber(2);
        images=checkLive(liveOutput->lineNumberForBlock(blank)==3,__LINE__)&&images;
        output->setTextCursor(QTextCursor(blank));output->insertPlainText("New text");
        images=checkLive(edit->toPlainText()=="\nFirst\nNew text\n \t\n\nSecond\n",__LINE__)&&images;
        edit->setPlainText("# Heading\n> First\n> Second\n\n\n- Third\n  Fourth\n- Fifth\n");settle();
        images=checkLive(output->toPlainText()=="Heading\nFirst\nSecond\n\n\nThird\nFourth\nFifth\n",__LINE__)&&images;
        const auto quoteBlock=output->document()->findBlock(output->toPlainText().indexOf("First"));
        images=checkLive(quoteBlock.blockFormat().background().color()==quoteBackground,__LINE__)&&images;
        // A down arrow landing at a paragraph's end must not skip that paragraph.
        edit->setPlainText("aa\n\nbb\n\ncc");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        caret=output->textCursor();caret.movePosition(QTextCursor::EndOfBlock);output->setTextCursor(caret);
        QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
        for(const QString &expected:{QString(),QString("bb"),QString(),QString("cc")}){
            QApplication::sendEvent(output,&down);
            images=checkLive(output->textCursor().block().text()==expected,__LINE__)&&images;
        }
        QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
        QApplication::sendEvent(output,&up);
        images=checkLive(output->textCursor().block().text().isEmpty(),__LINE__)&&images;
        images=checkLive(edit->toPlainText()=="aa\n\nbb\n\ncc",__LINE__)&&images;
        const QString numbered="Before\n\n1. 1\n2. 2\n3. 3\n4. 4\n\nAfter";
        edit->setPlainText(numbered);settle();
        images=checkLive(output->document()->blockCount()==8,__LINE__)&&images;
        output->setTextCursor(QTextCursor(output->document()->findBlockByNumber(1)));
        for(int line=3;line<=6;++line){
            QApplication::sendEvent(output,&down);
            images=checkLive(liveOutput->lineNumberAt(output->textCursor())==line&&
                (line!=3||output->textCursor().positionInBlock()==0),__LINE__)&&images;
        }
        for(int line=5;line>=2;--line){
            QApplication::sendEvent(output,&up);
            images=checkLive(liveOutput->lineNumberAt(output->textCursor())==line,__LINE__)&&images;
        }
        images=checkLive(edit->toPlainText()==numbered,__LINE__)&&images;
        caret=output->textCursor();
        caret.setPosition(output->document()->findBlockByNumber(3).position());output->setTextCursor(caret);
        const auto nextItem=output->document()->findBlockByNumber(4);
        images=checkLive(output->toPlainText()=="Before\n\n1\n2\n3\n4\n\nAfter"&&
            nextItem.textList()&&nextItem.textList()->itemText(nextItem)=="3.",__LINE__)&&images;
        edit->setPlainText("- First\n  - Nested **bold** $\\frac{a}{b}$\n- Last");settle();
        const auto listItemY=[&](int index){
            const auto block=output->document()->findBlockByNumber(index);
            return output->document()->documentLayout()->blockBoundingRect(block).top();
        };
        const qreal nestedY=listItemY(1), lastY=listItemY(2);
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Nested"));output->setTextCursor(caret);
        images=checkLive(output->toPlainText()=="First\nNested bold "+QString(QChar::ObjectReplacementCharacter)+"\nLast"&&
            output->document()->begin().textList()&&output->document()->lastBlock().textList()&&
            qAbs(listItemY(1)-nestedY)<1&&qAbs(listItemY(2)-lastY)<1,__LINE__)&&images;
        const QString tightList="## Above\n1. First\n2. Second\n## Below";
        edit->setPlainText(tightList);settle();
        const QStringList renderedListLines={"Above","First","Second","Below"};
        const auto rawListLines=tightList.split('\n');
        for(int i=0;i<renderedListLines.size();++i){
            caret=output->textCursor();
            caret.setPosition(output->toPlainText().indexOf(renderedListLines[i]));output->setTextCursor(caret);
            auto expected=renderedListLines;
            if(i==0||i==3)expected[i]=rawListLines[i];
            images=checkLive(output->toPlainText()==expected.join('\n')&&edit->toPlainText()==tightList,__LINE__)&&images;
        }
        edit->setPlainText("first\n\nsecond\n\nthird");settle();
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("second")+6);output->setTextCursor(caret);
        images=checkLive(output->textCursor().block().text()=="second"&&output->textCursor().positionInBlock()==6,__LINE__)&&images;
        // Live gutters count blank lines as source lines too.
        edit->setPlainText("# Heading\n\nSecond\n\nThird");settle();
        for(const auto &entry:QList<QPair<QString,int>>{{"Heading",1},{"Second",3},{"Third",5}}){
            auto block=output->document()->findBlock(output->toPlainText().indexOf(entry.first));
            images=checkLive(liveOutput->lineNumberForBlock(block)==entry.second,__LINE__)&&images;
        }
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("Second"));output->setTextCursor(caret);
        output->insertPlainText("New line\n");
        auto third=output->document()->findBlock(output->toPlainText().indexOf("Third"));
        images=checkLive(liveOutput->lineNumberForBlock(third)==6&&liveOutput->sourceLineCount()==6&&
            edit->toPlainText()=="# Heading\n\nNew line\nSecond\n\nThird",__LINE__)&&images;
        images=checkLive(edit->findChild<QWidget *>("lineNumberArea")&&
            output->findChild<QWidget *>("lineNumberArea"),__LINE__)&&images;
        // Editing headings keeps the preview size, including every '#' marker.
        for(int level=1;level<=6;++level){
            const QString heading=QString(level,'#')+" Heading";
            edit->setPlainText(heading+"\n\nBody");settle();
            const auto expected=headingCharacters(level,output->font());
            const auto previewCharacters=output->document()->begin().begin().fragment().charFormat();
            images=checkLive(previewCharacters.fontPointSize()==expected.fontPointSize()&&
                previewCharacters.fontPointSize()>output->font().pointSizeF()&&
                !previewCharacters.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
            for(int pos:{0,level+1}){
                QTextCursor sample(output->document());sample.setPosition(pos);sample.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                auto actual=sample.charFormat();
                images=checkLive(actual.fontWeight()==expected.fontWeight()&&
                    actual.fontPointSize()==expected.fontPointSize()&&
                    !actual.hasProperty(QTextFormat::FontSizeAdjustment),__LINE__)&&images;
            }
            images=checkLive(edit->toPlainText()==heading+"\n\nBody",__LINE__)&&images;
        }
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        // Opening one inline span must leave its siblings rendered.
        edit->setPlainText("Start $x$ and **bold**、*italic*、**second**、`code`\nplus $y$ end");settle();
        const auto formulaObject=QChar::ObjectReplacementCharacter;
        auto visible=output->toPlainText();
        caret=output->textCursor();caret.setPosition(visible.indexOf(formulaObject));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("$x$")&&!visible.contains("**")&&visible.count(formulaObject)==1,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(visible.indexOf("$x$")+1);
        caret.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
        output->setTextCursor(caret);output->insertPlainText("z");
        images=checkLive(edit->toPlainText()=="Start $z$ and **bold**、*italic*、**second**、`code`\nplus $y$ end",__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(output->toPlainText().indexOf("bold"));output->setTextCursor(caret);
        visible=output->toPlainText();
        images=checkLive(visible.contains("**bold**")&&!visible.contains("**second**")&&visible.count(formulaObject)==2,__LINE__)&&images;
        caret=output->textCursor();caret.setPosition(visible.indexOf("end"));output->setTextCursor(caret);
        images=checkLive(!output->toPlainText().contains("**")&&output->toPlainText().count(formulaObject)==2,__LINE__)&&images;
        edit->setPlainText("# Heading\n\nBody **bold** and $x^2$");settle();
        caret=output->textCursor();caret.setPosition(0);output->setTextCursor(caret);
        // Native input-method commits and cross-paragraph paste must update source safely.
        QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("中文"));
        QApplication::sendEvent(output,&commit);
        images=checkLive(edit->toPlainText().startsWith(QString::fromUtf8("# 中文Heading")),__LINE__)&&images;
        output->selectAll();QApplication::clipboard()->setText("Pasted **text**\n\nSecond paragraph");output->paste();
        images=checkLive(edit->toPlainText()=="Pasted **text**\n\nSecond paragraph",__LINE__)&&images;
        // Deleting a paragraph boundary must leave unrelated blocks rendered.
        const QString tail="\n\n**Bold** $x^2$";
        for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
            edit->setPlainText("First\n\nSecond"+tail);settle();
            caret=output->textCursor();
            caret.setPosition(key==Qt::Key_Backspace?output->toPlainText().indexOf("Second"):5);
            output->setTextCursor(caret);
            QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(output,&remove);
            images=checkLive(edit->toPlainText()=="First\nSecond"+tail&&
                !output->toPlainText().contains("**Bold**")&&output->toPlainText().contains(formulaObject),__LINE__)&&images;
        }
        edit->document()->setModified(false);
        // Exercise startup, opening a file, and saving through the editor.
        QTemporaryDir files;
        QString filename=files.filePath(QString::fromUtf8("打开测试.md"));
        QFile fixture(filename);bool fixtureReady=fixture.open(QIODevice::WriteOnly);
        QByteArray contents=QString::fromUtf8("# 打开文档\n\n正文 **加粗** 与 $x^2$\n").toUtf8();
        if(app.arguments().size()>2){
            QFile documentFixture(app.arguments().at(2));
            bool readable=documentFixture.open(QIODevice::ReadOnly);
            images=checkLive(readable,__LINE__)&&images;
            if(readable)contents=documentFixture.readAll();
        }
        if(fixtureReady){fixtureReady=fixture.write(contents)==contents.size();fixture.close();}
        images=checkLive(fixtureReady,__LINE__)&&images;
        live.show();
        auto *openButton=live.findChild<QPushButton *>("openDocumentButton");
        auto *toolbar=live.findChild<QToolBar *>();
        images=checkLive(openButton->isVisible()&&!toolbar&&!output->isVisible(),__LINE__)&&images;
        openButton->click();
        auto *fileDialog=live.findChild<QFileDialog *>();
        images=checkLive(fileDialog&&fileDialog->isVisible(),__LINE__)&&images;
        if(fileDialog){
            fileDialog->setDirectory(QFileInfo(filename).absolutePath());
            settle(); // QFileSystemModel populates the directory asynchronously.
            fileDialog->findChild<QLineEdit *>("fileNameEdit")->setText(filename);
            settle();
            QMetaObject::invokeMethod(fileDialog,"accept",Qt::DirectConnection);
            settle();
            images=checkLive(!openButton->isVisible()&&!toolbar&&!toggle->associatedObjects().isEmpty()&&
                output->isVisible()&&!edit->document()->isModified(),__LINE__)&&images;
            images=checkLive(edit->toPlainText()==QString::fromUtf8(contents)&&
                liveOutput->markdown()==edit->toPlainText()&&
                !output->toPlainText().isEmpty(),__LINE__)&&images;

            auto savedText=[&]{QFile file(filename);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
            QTextCursor start(output->document());
            start.setPosition(2);
            output->setTextCursor(start);
            output->insertPlainText("Autosaved");
            QEventLoop waitForSave;
            QTimer::singleShot(850,&waitForSave,&QEventLoop::quit);
            waitForSave.exec();
            images=checkLive(savedText()==edit->toPlainText().toUtf8()&&savedText().contains("Autosaved")&&
                !edit->document()->isModified(),__LINE__)&&images;
            toggle->setChecked(false);
            edit->moveCursor(QTextCursor::End);
            edit->insertPlainText(" on close");
            images=checkLive(live.close()&&savedText()==edit->toPlainText().toUtf8()&&savedText().endsWith(" on close"),__LINE__)&&images;
        }
        {
            QFile longFile(files.filePath("long.md"));
            const QByteArray longText=QByteArray("Paragraph\n\n").repeated(80);
            const bool written=longFile.open(QIODevice::WriteOnly)&&longFile.write(longText)==longText.size();
            longFile.close();
            images=checkLive(written,__LINE__)&&images;
            if(written){
                Window reopening;
                reopening.findChild<MarkdownEditorWidget *>()->setLiveMode(true);
                auto *preview=reopening.findChild<QTextBrowser *>("markdownPreview");
                reopening.load(longFile.fileName());
                reopening.show();
                settle();
                images=checkLive(preview->textCursor().atStart()&&preview->verticalScrollBar()->value()==0,__LINE__)&&images;
                preview->moveCursor(QTextCursor::End);
                images=checkLive(preview->verticalScrollBar()->value()>0,__LINE__)&&images;
                reopening.load(longFile.fileName());
                settle();
                images=checkLive(preview->textCursor().atStart()&&preview->verticalScrollBar()->value()==0,__LINE__)&&images;
            }
        }
        {
            LiveEditor focusProbe;
            focusProbe.resize(640,240);
            focusProbe.show();
            focusProbe.setSource(QString("Paragraph\n\n").repeated(60)+"Last paragraph");
            QTextCursor selection(focusProbe.document()->lastBlock());
            focusProbe.setTextCursor(selection);
            selection.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,4);
            focusProbe.setTextCursor(selection);
            focusProbe.ensureCursorVisible();
            const int scroll=focusProbe.verticalScrollBar()->value();
            QFocusEvent focusOut(QEvent::FocusOut,Qt::ActiveWindowFocusReason);
            QFocusEvent focusIn(QEvent::FocusIn,Qt::ActiveWindowFocusReason);
            QApplication::sendEvent(&focusProbe,&focusOut);
            QApplication::sendEvent(&focusProbe,&focusIn);
            images=checkLive(scroll>0&&focusProbe.verticalScrollBar()->value()==scroll&&
                focusProbe.textCursor().position()==selection.position()&&
                focusProbe.textCursor().anchor()==selection.anchor(),__LINE__)&&images;
        }
        // Measure visible layout for units after the first one, where Qt
        // may discard the first block format when inserting a fragment.
        {
            LiveEditor layoutProbe;
            layoutProbe.setFont(output->font());
            layoutProbe.resize(640,480);
            layoutProbe.show();
            QTemporaryFile inlineImageFile;
            QImage tallImage(30,120,QImage::Format_RGB32);
            tallImage.fill(Qt::red);
            const bool inlineImageReady=inlineImageFile.open()&&tallImage.save(&inlineImageFile,"PNG")&&inlineImageFile.flush();
            images=checkLive(inlineImageReady,__LINE__)&&images;
            const QString imageMarkup="Text ![image]("+QUrl::fromLocalFile(inlineImageFile.fileName()).toString(QUrl::FullyEncoded)+") after";
            // Keep this baseline-stability fixture on one source line at all
            // supported font sizes; wrapping is covered separately.
            layoutProbe.resize(qMax(640,qCeil(QFontMetricsF(layoutProbe.font()).horizontalAdvance(imageMarkup))+100),480);
            for(const QString &middle:{QString("Body"),QString("# Heading"),QString("## `code`"),QString("First row\nSecond row"),
                    QString("Body **bold**"),QString("`code`"),QString("**bold**"),QString("_italic_"),
                    QString("`code` _italic_"),QString("~~strike~~ and `code`"),imageMarkup,QString(" \t")}){
                const QString original="Before\n\n"+middle+"\n\nAfter";
                layoutProbe.setSource(original);
                auto *doc=layoutProbe.document();
                auto top=[&](const QTextBlock &block){return doc->documentLayout()->blockBoundingRect(block).top();};
                const qreal before=top(doc->lastBlock());
                const auto unit=doc->findBlockByNumber(2);
                const qreal unitBefore=top(unit);
                const auto baseline=[&]{
                    const auto block=doc->findBlockByNumber(2);
                    doc->documentLayout()->blockBoundingRect(block);
                    const auto line=block.layout()->lineAt(0);
                    return line.y()+line.ascent();
                };
                const qreal baselineBefore=baseline();
                if(middle==imageMarkup){
                    QTextCursor image(unit);
                    image.setPosition(unit.position()+unit.text().indexOf(formulaObject));
                    image.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                    const auto format=image.charFormat().toImageFormat();
                    images=checkLive(format.isValid()&&format.height()>0&&format.height()<tallImage.height()&&
                        qAbs(format.height()-format.width()*4)<0.01,__LINE__)&&images;
                }
                layoutProbe.setTextCursor(QTextCursor(unit));
                images=checkLive(before>unitBefore&&unitBefore>0&&qAbs(top(doc->lastBlock())-before)<1.0&&
                    qAbs(top(doc->findBlockByNumber(2))-unitBefore)<1.0&&layoutProbe.markdown()==original,__LINE__)&&images;
                images=checkLive(qAbs(baseline()-baselineBefore)<1.0,__LINE__)&&images;
            }
            auto fenceAlpha=[&](int position){
                QTextCursor fence(layoutProbe.document());fence.setPosition(position);
                fence.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                return fence.charFormat().foreground().color().alpha();
            };
            // Fence rows stay in place; only their text disappears in preview.
            for(const auto &entry:QList<QPair<QString,QString>>{{"cpp","int value = 42; // $literal$"},
                    {"python","def square(x):\n    return \"$literal$\""},{"latex","\\frac{1}{2} % $literal$"},
                    {"json","{\"value\": true, \"count\": 42}"},{"javascript","const value = \"$literal$\";"}}){
                const QString raw="Before\n\n```"+entry.first+"\n"+entry.second+"\n```\nAfter";
                layoutProbe.setSource(raw);
                const QString previewText=layoutProbe.toPlainText();
                const int bodyStart=raw.indexOf(entry.second);
                const int visibleStart=previewText.indexOf(entry.second);
                QSet<QRgb> colors;
                for(auto block=layoutProbe.document()->findBlock(visibleStart);block.isValid()&&block.position()<visibleStart+entry.second.size();block=block.next())
                    for(auto it=block.begin();!it.atEnd();++it)colors.insert(it.fragment().charFormat().foreground().color().rgb());
                const int opening=raw.indexOf("```"),closing=raw.lastIndexOf("```");
                const auto bodyTop=layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->findBlock(visibleStart)).top();
                const auto tailTop=layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top();
                images=checkLive(previewText==raw&&fenceAlpha(opening)==0&&fenceAlpha(closing)==0&&visibleStart>=0&&colors.size()>1&&
                    !previewText.contains(QChar::ObjectReplacementCharacter)&&layoutProbe.markdown()==raw,__LINE__)&&images;
                QTextCursor click(layoutProbe.document());click.setPosition(visibleStart+2);layoutProbe.setTextCursor(click);
                images=checkLive(layoutProbe.sourceSelection()==qMakePair(bodyStart+2,bodyStart+2)&&
                    layoutProbe.toPlainText()==raw&&fenceAlpha(opening)==255&&fenceAlpha(closing)==255&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->findBlock(visibleStart)).top()==bodyTop&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top()==tailTop,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(layoutProbe.document()->lastBlock()));
                images=checkLive(layoutProbe.toPlainText()==previewText&&layoutProbe.markdown()==raw&&
                    fenceAlpha(opening)==0&&fenceAlpha(closing)==0&&
                    layoutProbe.document()->documentLayout()->blockBoundingRect(layoutProbe.document()->lastBlock()).top()==tailTop,__LINE__)&&images;
            }
            for(const QString &raw:{QString("```cpp\nint value = 42;"),QString("~~~python\nprint('x')\n~~~"),
                    QString("```text\n$literal$\n```"),QString("```cpp\n```"),QString("```cpp")}){
                layoutProbe.setSource(raw+"\n");
                images=checkLive(fenceAlpha(0)==0&&layoutProbe.toPlainText()==raw+"\n"&&
                    layoutProbe.markdown()==raw+"\n",__LINE__)&&images;
            }
            layoutProbe.resize(340,480);
            const QString longCode=QString("some code with spaces ").repeated(4)+"$x$";
            for(const QString &code:{"```cpp\n"+longCode+"\n\n  next\n```",QString("~~~\n\t$x$\n~~~"),
                    QString("    indented\n    code"),QString("    $x$\n\n    **code**"),
                    QString("> ```\n> > literal $x$\n> ```")}){
                const QString original="Before\n\n"+code+"\nAfter";
                layoutProbe.setSource(original);
                auto *doc=layoutProbe.document();
                auto geometry=[&]{
                    QList<QRectF> rects;
                    for(auto block=doc->begin();block.isValid();block=block.next())
                        rects.append(doc->documentLayout()->blockBoundingRect(block));
                    return rects;
                };
                const auto resting=geometry();
                layoutProbe.setTextCursor(QTextCursor(doc->findBlockByNumber(3)));
                images=checkLive(geometry()==resting&&layoutProbe.toPlainText()==original&&
                    layoutProbe.lineNumberAt(layoutProbe.textCursor())==4,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(doc->lastBlock()));
                images=checkLive(geometry()==resting&&layoutProbe.markdown()==original,__LINE__)&&images;
            }
            layoutProbe.resize(640,480);
            const QString editableCode="Before\n\n```cpp\n  value = $x$;\n\n```\nAfter";
            layoutProbe.setSource(editableCode);
            auto *codeDocument=layoutProbe.document();
            QTextCursor codeCaret(codeDocument->findBlockByNumber(3));
            layoutProbe.setTextCursor(codeCaret);
            const auto codeFormat=layoutProbe.textCursor().blockFormat();
            const auto codeFont=layoutProbe.textCursor().block().begin().fragment().charFormat().font();
            QKeyEvent codeEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
            QApplication::sendEvent(&layoutProbe,&codeEnter);
            QKeyEvent codeTyping(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
            QApplication::sendEvent(&layoutProbe,&codeTyping);
            const QString editedCode="Before\n\n```cpp\n\nx  value = $x$;\n\n```\nAfter";
            images=checkLive(layoutProbe.markdown()==editedCode&&
                layoutProbe.textCursor().blockFormat()==codeFormat&&
                layoutProbe.textCursor().charFormat().font()==codeFont,__LINE__)&&images;
            const qreal tailBefore=codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top();
            layoutProbe.setTextCursor(QTextCursor(codeDocument->lastBlock()));
            images=checkLive(qAbs(codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top()-tailBefore)<1.0&&
                layoutProbe.markdown()==editedCode&&fenceAlpha(editedCode.indexOf("```"))==0,__LINE__)&&images;
            for(const QString &code:{QString("- item\n  ```cpp\n  value\n  ```"),
                    QString("  - item\n    ```cpp\n    value\n    ```")}){
                const QString original="Before\n\n"+code+"\n\nAfter";
                layoutProbe.setSource(original);
                auto tailTop=[&]{return codeDocument->documentLayout()->blockBoundingRect(codeDocument->lastBlock()).top();};
                const qreal before=tailTop();
                QTextCursor value(codeDocument);
                value.setPosition(layoutProbe.toPlainText().indexOf("value"));
                layoutProbe.setTextCursor(value);
                images=checkLive(qAbs(tailTop()-before)<1.0&&layoutProbe.markdown()==original,__LINE__)&&images;
                layoutProbe.setTextCursor(QTextCursor(codeDocument->lastBlock()));
                images=checkLive(qAbs(tailTop()-before)<1.0,__LINE__)&&images;
            }
        }
        {
            LiveEditor probe;
            probe.resize(640,480);
            probe.show();
            const QString linkSource="Before\n\n[Browser link](https://example.com#section)\n\n[Section](#section)";
            probe.setSource(linkSource);
            QList<QUrl> clickedLinks;
            QObject::connect(&probe,&QTextBrowser::anchorClicked,&probe,[&](const QUrl &url){clickedLinks.append(url);});
            for(const QString &label:{QString("Browser link"),QString("Section")}){
                QTextCursor link(probe.document());
                link.setPosition(probe.toPlainText().indexOf(label)+2);
                const QPoint point=probe.cursorRect(link).center();
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&press);
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&release);
            }
            images=checkLive(clickedLinks==QList<QUrl>{QUrl("https://example.com#section"),QUrl("#section")}&&
                probe.markdown()==linkSource&&!probe.toPlainText().contains("](https:"),__LINE__)&&images;
            probe.setSource("First\n\n\nLast");
            const auto paragraphHeight=[&](int number){
                const auto block=probe.document()->findBlockByNumber(number);
                return probe.document()->documentLayout()->blockBoundingRect(block).height();
            };
            const qreal bodyHeight=paragraphHeight(0);
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0&&
                qAbs(paragraphHeight(2)-bodyHeight)<1.0,__LINE__)&&images;
            QTextCursor emptyLineCaret(probe.document()->findBlockByNumber(1));
            probe.setTextCursor(emptyLineCaret);
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
            probe.insertPlainText(QString::fromUtf8("正文"));
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
            emptyLineCaret=probe.textCursor();emptyLineCaret.movePosition(QTextCursor::StartOfBlock);
            emptyLineCaret.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
            probe.setTextCursor(emptyLineCaret);probe.insertPlainText("");
            images=checkLive(qAbs(paragraphHeight(1)-bodyHeight)<1.0,__LINE__)&&images;
            probe.setSource("Plain text\n\nBefore $\\frac{a}{b}$ after\n\nTail");
            const auto lineHeight=[&](int blockNumber){
                const auto block=probe.document()->findBlockByNumber(blockNumber);
                const auto *layout=probe.document()->documentLayout();
                return layout->blockBoundingRect(block.next()).top()-layout->blockBoundingRect(block).top();
            };
            const qreal plainHeight=lineHeight(0);
            const auto formulaBlock=probe.document()->findBlock(probe.toPlainText().indexOf(formulaObject));
            const int formulaBlockNumber=formulaBlock.blockNumber();
            const auto baseline=[&]{
                const auto block=probe.document()->findBlockByNumber(formulaBlockNumber);
                const auto rect=probe.document()->documentLayout()->blockBoundingRect(block);
                const auto line=block.layout()->lineAt(0);
                return rect.top()+line.y()+line.ascent();
            };
            const qreal renderedBaseline=baseline();
            QTextCursor formulaCursor(probe.document());
            formulaCursor.setPosition(probe.toPlainText().indexOf(formulaObject));
            formulaCursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            const auto inlineImage=formulaCursor.charFormat().toImageFormat();
            const qreal paintAscent=inlineImage.doubleProperty(formulaPaintAscent);
            const qreal paintDescent=inlineImage.doubleProperty(formulaPaintHeight)-paintAscent;
            images=checkLive(inlineImage.isValid()&&paintAscent>0&&paintDescent>0&&
                qAbs(inlineImage.height()-paintAscent)<0.01&&
                paintDescent<=QFontMetricsF(probe.document()->defaultFont()).descent()+0.01,__LINE__)&&images;
            images=checkLive(qAbs(lineHeight(formulaBlockNumber)-plainHeight)<1.0,__LINE__)&&images;
            caret=probe.textCursor();caret.setPosition(probe.toPlainText().indexOf(formulaObject));
            probe.setTextCursor(caret);
            images=checkLive(probe.toPlainText().contains("$\\frac{a}{b}$")&&
                qAbs(lineHeight(formulaBlockNumber)-plainHeight)<1.0&&
                qAbs(baseline()-renderedBaseline)<1.0,__LINE__)&&images;
            probe.setSource("Start $x$ and **bold**、*italic*、**second**、`code`\nplus $y$ end");
            const QString visible=probe.toPlainText();
            caret=probe.textCursor();caret.setPosition(visible.indexOf(formulaObject));
            const auto leftEdge=probe.cursorRect(caret);
            caret.movePosition(QTextCursor::NextCharacter);
            const auto rightEdge=probe.cursorRect(caret);
            const QPoint hit(rightEdge.left()-2,leftEdge.center().y());
            QMouseEvent click(QEvent::MouseButtonPress,QPointF(hit),
                QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&click);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(hit),
                QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&release);
            images=checkLive(probe.toPlainText().contains("$x$")&&
                probe.toPlainText().count(formulaObject)==1,__LINE__)&&images;
            const QString tail=QString("\n\nTail").repeated(24);
            probe.setSource("**one** and **two** remainder"+tail);
            QTextCursor split=probe.textCursor();
            split.setPosition(probe.toPlainText().indexOf("remainder"));probe.setTextCursor(split);
            probe.verticalScrollBar()->setValue(0);
            const int beforeEnter=probe.cursorRect().top();
            QKeyEvent splitLine(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
            QApplication::sendEvent(&probe,&splitLine);
            images=checkLive(probe.markdown()=="**one** and **two** \nremainder"+tail&&
                probe.cursorRect().top()>beforeEnter&&probe.lineNumberAt(probe.textCursor())==2,__LINE__)&&images;
        }
        {
            LiveEditor probe;
            probe.resize(640,360);probe.show();
            // Every character, including hidden prefixes and item boundaries,
            // remains reachable in both directions without a mouse click.
            for(const QString &list:{QString("- one\n- two\n- three"),
                    QString("1. one\n2. two\n3. three"),
                    QString("- one\n  - two\n- three"),
                    QString("- [x] one\n- [ ] two\n- [x] three")}){
                probe.setSource(list);probe.restoreSourceSelection(0,0);
                for(int direction:{1,-1}){
                    QKeyEvent arrow(QEvent::KeyPress,direction==1?Qt::Key_Right:Qt::Key_Left,Qt::NoModifier);
                    const int start=direction==1?1:int(list.size())-1;
                    const int end=direction==1?int(list.size())+1:-1;
                    for(int at=start;at!=end;at+=direction){
                        QApplication::sendEvent(&probe,&arrow);
                        images=checkLive(probe.sourceSelection()==qMakePair(at,at)&&probe.markdown()==list,__LINE__)&&images;
                    }
                }
                probe.restoreSourceSelection(0,0);
                QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
                QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
                const auto lines=list.split('\n');
                const int second=lines[0].size()+1,third=second+lines[1].size()+1;
                for(int expected:{second,third}){
                    QApplication::sendEvent(&probe,&down);
                    images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
                }
                for(int expected:{second,0}){
                    QApplication::sendEvent(&probe,&up);
                    images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
                }
            }
            // Moving between differently styled blocks must preserve column zero.
            for(const QString &text:{QString("plain\n\n# Heading\n\n> quote\n\n- item\n\nlast"),
                    QString("# Heading\n\nplain\n\n## Smaller\n\nlast")}){
                QList<int> starts{0};
                for(int i=0;i<text.size();++i)if(text[i]=='\n')starts.append(i+1);
                probe.setSource(text);
                for(int direction:{1,-1}){
                    const int first=direction==1?0:starts.size()-1;
                    probe.restoreSourceSelection(starts[first],starts[first]);
                    QKeyEvent arrow(QEvent::KeyPress,direction==1?Qt::Key_Down:Qt::Key_Up,Qt::NoModifier);
                    for(int i=first+direction;i>=0&&i<starts.size();i+=direction){
                        QApplication::sendEvent(&probe,&arrow);
                        images=checkLive(probe.sourceSelection()==qMakePair(starts[i],starts[i])&&
                            probe.markdown()==text,__LINE__)&&images;
                    }
                }
            }
            for(const QString &list:{QString("- **bold** tail"),QString("1. [link](https://example.com)"),
                    QString("- parent\n  - [x] child $x$"),QString("- first\n  continuation\n- last")}){
                probe.setSource(list);probe.restoreSourceSelection(list.size(),list.size());
                QKeyEvent left(QEvent::KeyPress,Qt::Key_Left,Qt::NoModifier);
                int previous=list.size();
                for(int steps=0;steps<list.size()+2&&previous>0;++steps){
                    QApplication::sendEvent(&probe,&left);const int now=probe.sourceSelection().second;
                    images=checkLive(now<previous&&probe.markdown()==list,__LINE__)&&images;
                    if(now>=previous)break;previous=now;
                }
                images=checkLive(probe.sourceSelection()==qMakePair(0,0),__LINE__)&&images;
            }
            probe.setSource("- item");probe.restoreSourceSelection(2,2);
            QKeyEvent selectLeft(QEvent::KeyPress,Qt::Key_Left,Qt::ShiftModifier);
            QApplication::sendEvent(&probe,&selectLeft);QApplication::sendEvent(&probe,&selectLeft);
            images=checkLive(probe.sourceSelection()==qMakePair(2,0),__LINE__)&&images;
            QKeyEvent replaceMarker(QEvent::KeyPress,Qt::Key_Plus,Qt::NoModifier,"+ ");
            QApplication::sendEvent(&probe,&replaceMarker);
            images=checkLive(probe.markdown()=="+ item",__LINE__)&&images;
            const QString uneven="- aaaaaa\n- a\n- aaaaaa";
            probe.setSource(uneven);probe.restoreSourceSelection(5,5);
            QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
            QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
            for(int expected:{12,18}){
                QApplication::sendEvent(&probe,&down);
                images=checkLive(probe.sourceSelection()==qMakePair(expected,expected),__LINE__)&&images;
            }
            for(int expected:{12,5}){
                QApplication::sendEvent(&probe,&up);
                images=checkLive(probe.sourceSelection()==qMakePair(expected,expected)&&probe.markdown()==uneven,__LINE__)&&images;
            }
        }
        {
            LiveEditor probe;
            probe.resize(640,360);probe.show();
            const QString original="- First **bold** $x$\n  - Nested\n- Last";
            for(int line:{0,1,2}){
                probe.setSource(original);
                const auto block=probe.document()->findBlockByNumber(line);
                const QRect bodyEdge=probe.cursorRect(QTextCursor(block));
                const QPoint point(bodyEdge.left()-12,bodyEdge.center().y());
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&press);
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),
                    QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(probe.viewport(),&release);
                const int lineStart=line==0?0:original.indexOf(line==1?"  - Nested":"- Last");
                const int marker=lineStart+(line==1?2:0);
                images=checkLive(probe.sourceSelection()==qMakePair(marker,marker)&&
                    probe.textCursor().block().text().startsWith(line==1?"  - ":"- ")&&
                    !probe.textCursor().block().textList()&&!probe.toPlainText().contains("**")&&
                    probe.toPlainText().count(QChar::ObjectReplacementCharacter)==1,__LINE__)&&images;
                QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(&probe,&remove);
                QString expected=original;expected.remove(marker,1);
                images=checkLive(probe.markdown()==expected,__LINE__)&&images;
            }
            probe.setSource(original);probe.restoreSourceSelection(2,2);
            QKeyEvent left(QEvent::KeyPress,Qt::Key_Left,Qt::NoModifier);
            QApplication::sendEvent(&probe,&left);
            images=checkLive(probe.sourceSelection()==qMakePair(1,1)&&
                probe.toPlainText().startsWith("- First bold "),__LINE__)&&images;
        }
        {
            LiveEditor probe;
            const QString list="- First\n  - Nested **bold** $x$ and $y$\n- Last";
            const QChar object=QChar::ObjectReplacementCharacter;
            probe.setSource(list);
            probe.restoreSourceSelection(list.indexOf("Nested"),list.indexOf("Nested"));
            images=checkLive(probe.toPlainText()=="First\nNested bold "+QString(object)+" and "+QString(object)+"\nLast"&&
                probe.textCursor().block().textList()&&probe.document()->indentWidth()==24,__LINE__)&&images;
            const int bold=list.indexOf("**bold**");
            probe.restoreSourceSelection(bold,bold);
            images=checkLive(probe.toPlainText().contains("**bold**")&&
                probe.toPlainText().count(object)==2&&probe.textCursor().block().textList(),__LINE__)&&images;
            const int formula=list.indexOf("$x$");
            probe.restoreSourceSelection(formula+1,formula+1);
            images=checkLive(probe.toPlainText().contains("$x$")&&!probe.toPlainText().contains("**")&&
                probe.toPlainText().count(object)==1&&probe.sourceSelection()==qMakePair(formula+1,formula+1),__LINE__)&&images;
            QKeyEvent typeZ(QEvent::KeyPress,Qt::Key_Z,Qt::NoModifier,"z");
            QApplication::sendEvent(&probe,&typeZ);
            QString editedList=list;editedList.insert(formula+1,'z');
            images=checkLive(probe.markdown()==editedList&&probe.textCursor().block().textList(),__LINE__)&&images;
            probe.restoreSourceSelection(2,2);
            QKeyEvent removeSpace(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
            QApplication::sendEvent(&probe,&removeSpace);
            images=checkLive(probe.markdown()==QString("-First")+editedList.mid(7),__LINE__)&&images;
        }
        {
            LiveEditor probe;
            // Exercise the real mouse path with the old caret in another block.
            QString clickSource;
            for(int i=0;i<12;++i)clickSource+="Paragraph **bold**\n\n";
            clickSource+="Prefix **kept** $$\\frac{1}{\\frac{1}{x}}$$ Suffix **kept**\n\n";
            for(int i=0;i<20;++i)clickSource+="Following paragraph\n\n";
            probe.resize(700,500);probe.setSource(clickSource);
            probe.restoreSourceSelection(0,0);
            const int object=probe.toPlainText().indexOf(QChar::ObjectReplacementCharacter);
            QTextCursor formula(probe.document());formula.setPosition(object);
            probe.verticalScrollBar()->setValue(probe.cursorRect(formula).top()-180);
            const int scrollBefore=probe.verticalScrollBar()->value();
            const auto tailBefore=probe.document()->documentLayout()->blockBoundingRect(probe.document()->lastBlock()).top()-scrollBefore;
            QPoint hit=probe.cursorRect(formula).center();hit.rx()+=1;
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(hit),QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(hit),QPointF(probe.viewport()->mapToGlobal(hit)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(probe.viewport(),&release);
            const auto tailAfter=probe.document()->documentLayout()->blockBoundingRect(probe.document()->lastBlock()).top()-probe.verticalScrollBar()->value();
            images=checkLive(probe.verticalScrollBar()->value()==scrollBefore&&tailAfter>=tailBefore&&
                probe.toPlainText().contains("$$")&&!probe.toPlainText().contains("**kept**")&&probe.markdown()==clickSource,__LINE__)&&images;
            for(const QString &raw:{QString("$$x^2$$"),QString("$$\nx^2\n$$"),QString("$$\nx^2\n  $$")}){
                const QString original="Before\n\n"+raw+"\n\nAfter";
                for(int side:{0,1}){
                    probe.setSource(QString());probe.setSource(original);
                    const int at=probe.toPlainText().indexOf(QChar::ObjectReplacementCharacter);
                    QTextCursor start(probe.document());start.setPosition(at);
                    const QRect left=probe.cursorRect(start);
                    start.setPosition(at+1);const QRect right=probe.cursorRect(start);
                    const QPoint point(side?right.left()-1:left.left()+1,left.center().y());
                    QMouseEvent down(QEvent::MouseButtonPress,QPointF(point),QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(probe.viewport(),&down);
                    QMouseEvent up(QEvent::MouseButtonRelease,QPointF(point),QPointF(probe.viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(probe.viewport(),&up);
                    const int expected=original.indexOf("x^2")+3;
                    images=checkLive(probe.sourceSelection()==qMakePair(expected,expected)&&probe.markdown()==original,__LINE__)&&images;
                }
            }
            // A tall rendered formula must not collapse when exposed as source.
            const QString tall="Before\n\n$$\\frac{1}{\\frac{1}{\\frac{1}{x}}}$$\n\nAfter";
            probe.resize(700,500);
            probe.setSource(tall);
            const auto tailTop=[&]{return probe.document()->documentLayout()->blockBoundingRect(probe.document()->lastBlock()).top();};
            const qreal originalTail=tailTop();
            const int formulaStart=tall.indexOf("$$");
            probe.restoreSourceSelection(formulaStart+2,formulaStart+2);
            images=checkLive(tailTop()>=originalTail,__LINE__)&&images;
            // The source stays above a live preview inside the same formula region.
            probe.restoreSourceSelection(formulaStart+2,tall.lastIndexOf("$$"));
            probe.insertPlainText("x");
            images=checkLive(tailTop()>=originalTail&&probe.markdown()=="Before\n\n$$x$$\n\nAfter",__LINE__)&&images;
            probe.restoreSourceSelection(formulaStart+3,formulaStart+3);
            images=checkLive(tailTop()>=originalTail,__LINE__)&&images;
            probe.restoreSourceSelection(probe.markdown().indexOf("After"),probe.markdown().indexOf("After"));
            images=checkLive(tailTop()<originalTail-1,__LINE__)&&images;
            const QString display="$$\nx^2\n$$\n\nAfter";
            probe.setSource(display);
            probe.restoreSourceSelection(4,4);
            const auto previewMargin=[&]{return probe.document()->findBlock(probe.toPlainText().indexOf("$$",2)).blockFormat().bottomMargin();};
            const qreal margin=previewMargin();
            const qreal sourceTop=probe.document()->documentLayout()->blockBoundingRect(probe.document()->firstBlock()).top();
            auto *preview=probe.findChild<QWidget *>("mathPreviewView");
            auto *previewDocument=probe.findChild<QTextDocument *>("mathPreviewDocument");
            auto *panel=probe.findChild<QFrame *>("mathPreviewPanel");
            auto previewSource=[&]{
                QString result;
                if(previewDocument)for(auto block=previewDocument->begin();block.isValid();block=block.next())
                    for(auto it=block.begin();!it.atEnd();++it)result+=it.fragment().charFormat().stringProperty(formulaMarkdown);
                return result;
            };
            images=checkLive(panel&&!panel->isWindow()&&panel->parentWidget()==probe.viewport()&&preview&&margin>24&&previewSource().contains("x^2")&&
                probe.markdown()==display&&probe.toPlainText().contains("x^2"),__LINE__)&&images;
            QKeyEvent changeFormula(QEvent::KeyPress,Qt::Key_Y,Qt::NoModifier,"y");
            QApplication::sendEvent(&probe,&changeFormula);
            QString changed=display;changed.insert(4,'y');
            images=checkLive(probe.markdown()==changed&&previewMargin()>24&&
                qAbs(probe.document()->documentLayout()->blockBoundingRect(probe.document()->firstBlock()).top()-sourceTop)<0.1&&previewSource().contains("xy^2")&&
                probe.sourceSelection()==qMakePair(5,5),__LINE__)&&images;
            probe.resize(700,500);probe.show();probe.activateWindow();probe.setFocus();settle();
            // The window remembers its focus widget even when the application is in the background.
            images=checkLive(panel&&!panel->isHidden()&&probe.focusWidget()==&probe&&
                panel->geometry().top()>probe.cursorRect(probe.textCursor()).bottom(),__LINE__)&&images;
            if(panel)panel->grab().save(QDir::current().absoluteFilePath("math-edit-preview.png"));
            probe.grab().save(QDir::current().absoluteFilePath("math-edit-layout.png"));
            probe.restoreSourceSelection(changed.indexOf("After"),changed.indexOf("After"));
            settle();
            images=checkLive(!probe.toPlainText().contains("$$")&&probe.markdown()==changed&&panel&&panel->isHidden(),__LINE__)&&images;
            QStringList matrixRows;
            for(int row=0;row<24;++row)matrixRows.append("x & y");
            const QString matrix="$$\\begin{pmatrix}"+matrixRows.join("\\\\")+"\\end{pmatrix}$$";
            probe.setSource(matrix);probe.restoreSourceSelection(2,2);settle();
            images=checkLive(preview&&preview->findChildren<QScrollBar *>().isEmpty()&&panel->height()>320&&
                previewSource().contains("pmatrix"),__LINE__)&&images;
            // Very wide formulas scale with the editor width rather than clip or scroll.
            const QString wide="$$"+QString("x+").repeated(80)+"x$$";
            probe.setSource(wide);probe.restoreSourceSelection(2,2);settle();
            const int wideHeight=preview->height();
            probe.resize(280,500);settle();
            images=checkLive(preview->height()<wideHeight&&panel->width()<=probe.viewport()->width()&&
                preview->findChildren<QScrollBar *>().isEmpty()&&probe.markdown()==wide,__LINE__)&&images;
            probe.grab().save(QDir::current().absoluteFilePath("math-edit-narrow.png"));
            probe.resize(700,500);settle();
            const QList<QPair<QString,QString>> cases={
                {"- first","- first\n- "},{"9) item","9) item\n10) "},
                {"  - [x] done","  - [x] done\n  - [ ] "},
                {"- ",""},{"intro\n\n- ","intro\n\n"},
                {"```\n- code\n```","```\n- code\n\n```"},
                {"$$\n- math\n$$","$$\n- math\n\n$$"}};
            for(const auto &entry:cases){
                probe.setSource(entry.first);
                const int at=entry.first.contains("code")?entry.first.indexOf("code")+4:
                    entry.first.contains("math")?entry.first.indexOf("math")+4:entry.first.size();
                probe.restoreSourceSelection(at,at);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier,"\r");
                QApplication::sendEvent(&probe,&enter);
                images=checkLive(probe.markdown()==entry.second,__LINE__)&&images;
            }
            probe.setSource("- first");probe.restoreSourceSelection(7,7);
            QKeyEvent softBreak(QEvent::KeyPress,Qt::Key_Return,Qt::ShiftModifier,"\r");
            QApplication::sendEvent(&probe,&softBreak);
            images=checkLive(probe.markdown()=="- first\n",__LINE__)&&images;
            edit->setPlainText("# Heading\n\nSome **bold** text");settle();
            toggle->setChecked(false);
            QTextCursor selection(edit->document());selection.setPosition(24);selection.setPosition(16,QTextCursor::KeepAnchor);
            edit->setTextCursor(selection);toggle->setChecked(true);toggle->setChecked(false);
            images=checkLive(edit->textCursor().anchor()==24&&edit->textCursor().position()==16,__LINE__)&&images;
            toggle->setChecked(true);liveOutput->restoreSourceSelection(5,5);toggle->setChecked(false);
            images=checkLive(edit->textCursor().position()==5&&!edit->textCursor().hasSelection(),__LINE__)&&images;
        }
        {
            LiveEditor probe;probe.resize(640,360);probe.show();
            QString text;
            for(int i=0;i<80;++i)text+=QString("Paragraph %1 **bold**\n\n").arg(i);
            for(bool backspace:{true,false}){
                probe.setSource(text);probe.restoreSourceSelection(text.indexOf("Paragraph 35"),text.indexOf("Paragraph 35"));
                const int begin=text.indexOf("Paragraph 35");
                // Select a whole source line and its following blank line.
                const auto visible=probe.toPlainText();
                QTextCursor selected(probe.document());selected.setPosition(visible.indexOf("Paragraph 35"));
                selected.setPosition(visible.indexOf("Paragraph 36"),QTextCursor::KeepAnchor);probe.setTextCursor(selected);
                probe.verticalScrollBar()->setValue(probe.verticalScrollBar()->value()+probe.cursorRect().top()-160);
                const int scroll=probe.verticalScrollBar()->value();
                QKeyEvent remove(QEvent::KeyPress,backspace?Qt::Key_Backspace:Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(&probe,&remove);
                QString expected=text;expected.remove(begin,text.indexOf("Paragraph 36")-begin);
                images=checkLive(probe.markdown()==expected&&probe.verticalScrollBar()->value()==scroll,__LINE__)&&images;
            }
        }
        {
            LiveEditor codePreview;codePreview.setFont(QFont(previewFont,13));codePreview.resize(760,560);
            codePreview.document()->setDocumentMargin(24);
            codePreview.setSource("# Code preview\n\n```cpp\n#include <iostream>\nint main() {\n    // $x$ stays literal\n    std::cout << \"Hello, Markdown!\";\n    return 0;\n}\n```\n\n```python\ndef square(x):\n    return x ** 2\n\nprint(square(5))\n```\n\nEnd");
            codePreview.grab().save(QDir::current().absoluteFilePath("code-smoke.png"));
        }
        QImage shot(view.size(),QImage::Format_ARGB32);shot.fill(QColor("#ffffff"));view.setStyleSheet("color:#26232d;background:#ffffff;");view.render(&shot);shot.save(QDir::current().absoluteFilePath("math-smoke.png"));
        if(!images){qWarning()<<n<<errors;result=1;}
    }
    return result;
}
