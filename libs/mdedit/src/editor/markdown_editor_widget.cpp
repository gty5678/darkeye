#include "markdown_editor_widget.h"
#include "highlighter.h"
#include "../markdown/renderer.h"

MarkdownEditorWidget::MarkdownEditorWidget(QWidget *parent):QWidget(parent){
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);layout->addWidget(pages);
    editor->viewport()->installEventFilter(this);
    preview->setAcceptDrops(true);
    preview->viewport()->installEventFilter(this);
    setFocusProxy(preview);
    timer.setSingleShot(true);timer.setInterval(150);
        previewAction=new QAction("Live 编辑",this);previewAction->setCheckable(true);previewAction->setChecked(true);previewAction->setShortcut(QKeySequence("Ctrl+E"));
        previewAction->setToolTip(QString::fromUtf8(shortcutLabel)+"E 切换 Live / 源码");
        previewAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(previewAction);
        previewAction->setObjectName("livePreviewAction");editor->setObjectName("markdownEditor");preview->setObjectName("markdownPreview");
        editor->setFrameShape(QFrame::NoFrame);editor->setFont(QFont(editorFont,14));editor->setTabStopDistance(32);editor->document()->setDocumentMargin(32);highlighter=new Highlighter(editor->document());
        preview->setFrameShape(QFrame::NoFrame);preview->setFont(QFont(previewFont,13));preview->document()->setDocumentMargin(40);preview->setOpenLinks(false);
        pages->addWidget(editor);pages->addWidget(preview);pages->setCurrentIndex(1);
        preview->sourceChanged=[this](const QString &text){
            const QString before=editor->toPlainText();int begin=0,oldEnd=before.size(),newEnd=text.size();
            while(begin<oldEnd&&begin<newEnd&&before[begin]==text[begin])++begin;
            while(oldEnd>begin&&newEnd>begin&&before[oldEnd-1]==text[newEnd-1]){--oldEnd;--newEnd;}
            if(begin==oldEnd&&begin==newEnd)return;
            QTextCursor c(editor->document());c.setPosition(begin);c.setPosition(oldEnd,QTextCursor::KeepAnchor);
            c.insertText(text.mid(begin,newEnd-begin));
        };
        preview->history=[this](bool redo){
            auto *doc=editor->document();
            if(redo?!doc->isRedoAvailable():!doc->isUndoAvailable())return;
            const int scroll=preview->verticalScrollBar()->value();
            const int horizontal=preview->horizontalScrollBar()->value();
            const bool updates=preview->updatesEnabled();
            preview->setUpdatesEnabled(false);
            // The document returns the history operation's caret. The hidden
            // source editor's cursor does not track live edits reliably.
            QTextCursor caret(doc);
            if(redo)doc->redo(&caret);else doc->undo(&caret);
            editor->setTextCursor(caret);
            timer.stop();render();
            preview->restoreSourceSelection(caret.anchor(),caret.position());
            preview->verticalScrollBar()->setValue(scroll);
            preview->horizontalScrollBar()->setValue(horizontal);
            preview->setUpdatesEnabled(updates);
        };
        connect(editor,&QPlainTextEdit::textChanged,this,[this]{timer.start();emit textChanged();});
        connect(editor->document(),&QTextDocument::modificationChanged,this,&MarkdownEditorWidget::modificationChanged);
        connect(&timer,&QTimer::timeout,this,[this]{if(isLiveMode())render();});
        connect(previewAction,&QAction::toggled,this,[this](bool enabled){
            const int caretY=enabled?editor->cursorRect().top():preview->cursorRect().top();
            const bool updates=pages->updatesEnabled();
            pages->setUpdatesEnabled(false);
            timer.stop();
            const auto sourceCaret=editor->textCursor();
            if(enabled){
                render();
            }else{
                const auto selection=preview->sourceSelection();
                QTextCursor caret(editor->document());caret.setPosition(selection.first);
                caret.setPosition(selection.second,QTextCursor::KeepAnchor);editor->setTextCursor(caret);
            }
            pages->setCurrentWidget(enabled?static_cast<QWidget *>(preview):editor);previewAction->setText(enabled?"Live 编辑":"源码");
            setFocusProxy(enabled?static_cast<QWidget *>(preview):editor);
            if(enabled){
                preview->setFocus();
                preview->restoreSourceSelection(sourceCaret.anchor(),sourceCaret.position());
            }else{editor->setFocus();editor->ensureCursorVisible();}
            // Plain text scrolls in visual lines; rich text scrolls in pixels.
            // Measure one scroll step so both views retain the caret's viewport
            // height even with wrapping, different fonts and block spacing.
            auto alignCaret=[caretY](auto *view){
                auto *bar=view->verticalScrollBar();
                for(int attempt=0;attempt<3;++attempt){
                    const int before=bar->value(),y=view->cursorRect().top();
                    const int step=before<bar->maximum()?1:-1;
                    bar->setValue(before+step);
                    const int moved=bar->value()-before;
                    const int pixels=y-view->cursorRect().top();
                    if(!moved||!pixels)break;
                    const int target=before+qRound(qreal(y-caretY)*moved/pixels);
                    bar->setValue(target);
                    if(bar->value()==before)break;
                }
            };
            if(enabled)alignCaret(preview);else alignCaret(editor);
            pages->setUpdatesEnabled(updates);
            emit liveModeChanged(enabled);
        });
        connect(preview,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){
            if(url.scheme()=="mdedit-wiki"){
                followWikiLink(QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).mid(1).toUtf8()));
            }else if(url.isLocalFile()){
                const QFileInfo folder(url.toLocalFile());
                QString error;
                if(!url.isValid()||!url.authority().isEmpty()||folder.isRelative()||!folder.isDir())
                    error="请确认链接指向本机上存在的文件夹。";
                else if(!QDesktopServices::openUrl(QUrl::fromLocalFile(folder.absoluteFilePath())))
                    error="系统文件管理器无法打开此文件夹。";
                if(!error.isEmpty()){
                    auto *message=new QMessageBox(QMessageBox::Information,"无法打开文件夹",error,QMessageBox::Ok,this);
                    message->setAttribute(Qt::WA_DeleteOnClose);message->open();
                }
            }else if(const auto web=normalizedWebLink(url);web.scheme()=="https"||web.scheme()=="http")QDesktopServices::openUrl(web);
            else if(url.isRelative()&&url.path().isEmpty()&&!url.fragment().isEmpty())preview->scrollToAnchor(url.fragment());
        });
        auto command=[this](const QString &name,const QKeySequence &key,std::function<void()> fn){
            auto *action=new QAction(name,this);action->setShortcut(key);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(action);commands.append(action);
            connect(action,&QAction::triggered,this,[fn]{fn();});
        };
        command("加粗",QKeySequence::Bold,[this]{formatSelection("**","**");});
        command("斜体",QKeySequence::Italic,[this]{formatSelection("*","*");});
        command("行内代码",QKeySequence("Ctrl+Shift+C"),[this]{formatSelection("`","`");});
        command("删除线",{},[this]{formatSelection("~~","~~");});
        command("高亮",{},[this]{formatSelection("==","==");});
        const QString basicTable="|  |  |\n| --- | --- |\n|  |  |\n\n";
        const auto insertTableAtLine=[this,basicTable](int offset){
            const auto text=editor->toPlainText();
            offset=qBound(0,offset,int(text.size()));
            const int start=offset>0?text.lastIndexOf('\n',offset-1)+1:0;
            // Replace an empty target line so its existing newline does not
            // add another blank line after the table.
            const int end=start<text.size()&&text.at(start)=='\n'?start+1:start;
            replaceSource(start,end,basicTable,start,start);
        };
        preview->insertTableRequested=insertTableAtLine;
        command("插入表格",QKeySequence("Ctrl+Alt+T"),[this,insertTableAtLine]{
            const auto range=selection();insertTableAtLine(qMin(range.first,range.second));
        });
        command("插入链接",QKeySequence("Ctrl+K"),[this]{formatSelection("[","](url)");});
        command("插入内部链接…",QKeySequence("Ctrl+Shift+L"),[this]{insertWikiLink();});
        commands.last()->setObjectName("insertWikiLinkAction");
        command("插入代码块",{},[this]{
            const auto range=selection();
            const int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
            const auto text=markdown();
            const QString prefix=a>0&&text[a-1]!='\n'?"\n":"";
            const QString suffix=b<text.size()&&text[b]!='\n'?"\n":"";
            const int start=a+prefix.size()+4;
            replaceSource(a,b,prefix+"```\n"+text.mid(a,b-a)+"\n```"+suffix,start,start+b-a);
        });
        command("复制段落 / 选区",QKeySequence::Copy,[this]{clipboardParagraph(false);});
        command("剪切段落 / 选区",QKeySequence::Cut,[this]{clipboardParagraph(true);});
        command("增加缩进",QKeySequence("Ctrl+]"),[this]{indentSelection(false);});
        command("减少缩进",QKeySequence("Ctrl+["),[this]{indentSelection(true);});
        command("查找…",QKeySequence::Find,[this]{showFind(false);});
        command("替换…",QKeySequence("Ctrl+H"),[this]{showFind(true);});
        command("删除当前行",QKeySequence("Ctrl+Shift+K"),[this]{
            const auto r=selection();if(r.first!=r.second)return;
            const auto text=editor->toPlainText();int a=r.first?text.lastIndexOf('\n',r.first-1)+1:0;
            int b=text.indexOf('\n',r.first);if(b<0){b=text.size();if(a>0)--a;}else ++b;
            replaceSource(a,b,{},a,a);
        });
        // Reuse the same commands as the edit menu and keyboard shortcuts.
        const auto populateSelectionMenu=[this](QMenu *menu){
            const auto range=selection();
            const QStringList names=range.first==range.second?
                QStringList{"插入链接","插入内部链接…","插入代码块"}:QStringList{"加粗","斜体","删除线","高亮","插入内部链接…"};
            menu->addSeparator();
            for(const auto &name:names){
                for(auto *action:commands)if(action->text()==name){menu->addAction(action);break;}
            }
        };
        preview->populateSelectionMenu=populateSelectionMenu;
        editor->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(editor,&QWidget::customContextMenuRequested,this,[this,populateSelectionMenu](const QPoint &pos){
            if(!editor->textCursor().hasSelection())editor->setTextCursor(editor->cursorForPosition(pos));
            auto *menu=editor->createStandardContextMenu();
            menu->setObjectName("editorContextMenu");
            populateSelectionMenu(menu);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(editor->viewport()->mapToGlobal(pos));
        });
        setStyleSheet(R"(MarkdownEditorWidget{background:#ffffff;color:#26232d;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#e3d7f5;selection-color:#26232d;}QScrollBar:vertical{background:#ffffff;width:10px;}QScrollBar::handle:vertical{background:#a9a2b2;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#26232d;background:#f2eef7;})");
    applyBaseFontSize(14);
    render();
}

QString MarkdownEditorWidget::markdown() const { return editor->toPlainText(); }

void MarkdownEditorWidget::setThemePalette(const QPalette &palette)
{
    setPalette(palette);
    setStyleSheet(QStringLiteral(R"(
        MarkdownEditorWidget { background: %1; color: %2; }
        QPlainTextEdit, QTextBrowser {
            border: 0; background: %1; color: %2;
            selection-background-color: %3; selection-color: %4;
        }
        QScrollBar:vertical { background: %1; width: 10px; }
        QScrollBar::handle:vertical { background: %5; min-height: 28px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip { background: %6; color: %2; }
    )").arg(palette.color(QPalette::Base).name(), palette.color(QPalette::Text).name(),
             palette.color(QPalette::Highlight).name(), palette.color(QPalette::HighlightedText).name(),
             palette.color(QPalette::Mid).name(), palette.color(QPalette::AlternateBase).name()));
    editor->setThemePalette(palette);
    highlighter->setThemePalette(palette);
    preview->setThemePalette(palette);
    preview->refreshAppearance();
}

QTextDocument *MarkdownEditorWidget::document() const { return editor->document(); }
void MarkdownEditorWidget::setMarkdown(const QString &text){
    resetWikiNavigation();
    editor->setPlainText(text);editor->document()->setModified(false);
    timer.stop();
    editor->verticalScrollBar()->setValue(0);
    if(isLiveMode()){
        preview->document()->setBaseUrl(baseUrl);
        preview->setSource(editor->toPlainText(),true);
    }
}
void MarkdownEditorWidget::setMarkdown(const QString &text,const QUrl &url){
    baseUrl=url;
    setMarkdown(text);
}
void MarkdownEditorWidget::setBaseUrl(const QUrl &url){baseUrl=url;resetWikiNavigation();render();}
bool MarkdownEditorWidget::isLiveMode() const {return previewAction->isChecked();}
void MarkdownEditorWidget::setLiveMode(bool enabled){previewAction->setChecked(enabled);}
void MarkdownEditorWidget::applyBaseFontSize(int size){
    baseFontSize=qBound(9,size,30);
        const auto range=selection();
        const bool modified=editor->document()->isModified();
        const QSignalBlocker blockEditor(editor);
        QFont ui=defaultUiFont;
        ui.setPointSizeF(qMax(9.0,(ui.pointSizeF()>0?ui.pointSizeF():13)+baseFontSize-13));
        setFont(ui);
        editor->setFont(QFont(editorFont,baseFontSize+1));
        editor->setTabStopDistance(QFontMetricsF(editor->font()).horizontalAdvance(' ')*4);
        preview->setFont(QFont(previewFont,baseFontSize+2));
        preview->refreshAppearance();
        static_cast<NumberedEditor<QPlainTextEdit> *>(editor)->refreshLineNumbers();
        preview->refreshLineNumbers();
        selectSource(range.first,range.second);
        editor->document()->setModified(modified);
    }
QPair<int,int> MarkdownEditorWidget::selection() const{
        if(isLiveMode())return preview->sourceSelection();
        const auto c=editor->textCursor();return {c.anchor(),c.position()};
    }

void MarkdownEditorWidget::selectSource(int anchor,int position){
        QTextCursor c(editor->document());c.setPosition(anchor);c.setPosition(position,QTextCursor::KeepAnchor);
        editor->setTextCursor(c);
        if(isLiveMode()){render();preview->restoreSourceSelection(anchor,position);}
        else editor->ensureCursorVisible();
    }

void MarkdownEditorWidget::replaceSource(int start,int end,const QString &text,int anchor,int position){
        QTextCursor c(editor->document());c.setPosition(start);c.setPosition(end,QTextCursor::KeepAnchor);
        c.beginEditBlock();c.insertText(text);c.endEditBlock();selectSource(anchor,position);
    }

void MarkdownEditorWidget::formatSelection(const QString &left,const QString &right){
        if(isLiveMode()&&preview->formatNativeSelection(left,right))return;
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        if(a>=left.size()&&text.mid(a-left.size(),left.size())==left&&text.mid(b,right.size())==right){
            replaceSource(a-left.size(),b+right.size(),text.mid(a,b-a),a-left.size(),b-left.size());
        }else{
            replaceSource(a,b,left+text.mid(a,b-a)+right,a+left.size(),b+left.size());
        }
    }

void MarkdownEditorWidget::clipboardParagraph(bool cut){
        if(isLiveMode()&&preview->clipboardNativeCell(cut))return;
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        if(a==b){a=a?text.lastIndexOf('\n',a-1)+1:0;b=text.indexOf('\n',b);if(b<0)b=text.size();else ++b;}
        QApplication::clipboard()->setText(text.mid(a,b-a));
        if(cut)replaceSource(a,b,{},a,a);
    }

void MarkdownEditorWidget::indentSelection(bool outdent){
        const auto range=selection();int a=qMin(range.first,range.second),b=qMax(range.first,range.second);
        const auto text=editor->toPlainText();
        int start=a?text.lastIndexOf('\n',a-1)+1:0;
        int last=b>a&&text[b-1]=='\n'?b-1:b;
        int end=text.indexOf('\n',last);if(end<0)end=text.size();
        auto lines=text.mid(start,end-start).split('\n');
        int delta=0,firstDelta=0;
        for(int i=0;i<lines.size();++i){
            int d=4;
            if(outdent){int n=0;if(lines[i].startsWith('\t'))n=1;else while(n<4&&n<lines[i].size()&&lines[i][n]==' ')++n;lines[i].remove(0,n);d=-n;}
            else lines[i].prepend("    ");
            if(i==0)firstDelta=d;delta+=d;
        }
        const int na=qMax(start,a+firstDelta),nb=qMax(na,b+delta);
        replaceSource(start,end,lines.join('\n'),range.first<=range.second?na:nb,range.first<=range.second?nb:na);
    }

void MarkdownEditorWidget::showFind(bool replace){
        auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(replace?"查找和替换":"查找");
        auto *layout=new QFormLayout(dialog);auto *query=new QLineEdit;auto *replacement=new QLineEdit;
        const auto r=selection();query->setText(editor->toPlainText().mid(qMin(r.first,r.second),qAbs(r.second-r.first)));
        layout->addRow("查找",query);if(replace)layout->addRow("替换为",replacement);
        auto *sensitive=new QCheckBox("区分大小写");layout->addRow(sensitive);
        auto *message=new QLabel;layout->addRow(message);
        auto find=[this,query,sensitive,message](bool backwards){
            if(query->text().isEmpty())return;
            const auto r=selection();QTextCursor from(editor->document());
            from.setPosition(backwards?qMin(r.first,r.second):qMax(r.first,r.second));
            QTextDocument::FindFlags flags;if(sensitive->isChecked())flags|=QTextDocument::FindCaseSensitively;
            if(backwards)flags|=QTextDocument::FindBackward;
            auto hit=editor->document()->find(query->text(),from,flags);
            if(hit.isNull()){from.movePosition(backwards?QTextCursor::End:QTextCursor::Start);hit=editor->document()->find(query->text(),from,flags);}
            message->setText(hit.isNull()?"未找到匹配项":"");
            if(!hit.isNull())selectSource(hit.selectionStart(),hit.selectionEnd());
        };
        auto *next=new QPushButton("下一个");next->setDefault(true);layout->addRow(next);
        connect(next,&QPushButton::clicked,dialog,[find]{find(false);});
        auto *previous=new QPushButton("上一个");layout->addRow(previous);
        connect(previous,&QPushButton::clicked,dialog,[find]{find(true);});
        if(replace){
            auto *one=new QPushButton("替换");layout->addRow(one);
            connect(one,&QPushButton::clicked,dialog,[this,query,replacement,sensitive,find]{
                const auto r=selection();const int a=qMin(r.first,r.second),b=qMax(r.first,r.second);
                if(a!=b&&!query->text().isEmpty()&&editor->toPlainText().mid(a,b-a).compare(query->text(),sensitive->isChecked()?Qt::CaseSensitive:Qt::CaseInsensitive)==0)
                    replaceSource(a,b,replacement->text(),a,a+replacement->text().size());
                find(false);
            });
            auto *all=new QPushButton("全部替换");layout->addRow(all);
            connect(all,&QPushButton::clicked,dialog,[this,query,replacement,sensitive,message]{
                if(query->text().isEmpty())return;
                const auto before=editor->toPlainText();auto after=before;
                const auto cs=sensitive->isChecked()?Qt::CaseSensitive:Qt::CaseInsensitive;
                int count=0;qsizetype offset=0;
                while((offset=after.indexOf(query->text(),offset,cs))>=0){++count;offset+=query->text().size();}
                after.replace(query->text(),replacement->text(),cs);
                if(after!=before)replaceSource(0,before.size(),after,0,0);
                message->setText(QString("已替换 %1 处").arg(count));
            });
        }
        dialog->open();query->setFocus();query->selectAll();
    }


void MarkdownEditorWidget::render(){
    const int scroll=preview->verticalScrollBar()->value();
    preview->document()->setBaseUrl(baseUrl);
    preview->setSource(editor->toPlainText());preview->verticalScrollBar()->setValue(scroll);
}
