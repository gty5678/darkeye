#include "live_editor.h"
#include "../markdown/renderer.h"

LiveEditor::LiveEditor(){
        initialRenderTimer.setSingleShot(true);
        initialRenderTimer.setInterval(80);
        connect(&initialRenderTimer,&QTimer::timeout,this,[this]{
            const auto caret=textCursor();
            const int position=sourcePosition(caret.position());
            const int anchor=sourcePosition(caret.anchor());
            QScopedValueRollback<bool> refreshing(backgroundRefresh,true);
            rebuild(-1,0,position,active>=0);
            // A user may already have selected or edited the first screen.
            if(anchor!=position)restoreSourceSelection(anchor,position);
        });
        resourceManager.setActiveUrls({});
        resourceWindowTimer.setSingleShot(true);resourceWindowTimer.setInterval(35);
        connect(&resourceWindowTimer,&QTimer::timeout,this,&LiveEditor::updateResourceWindow);
        connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{if(!rebuilding)resourceWindowTimer.start();});

        copyFeedbackTimer.setSingleShot(true);
        copyFeedbackTimer.setInterval(3000);
        connect(&copyFeedbackTimer,&QTimer::timeout,this,[this]{copiedCodeSource=-1;viewport()->update();});
        axisDragScroll.setInterval(35);
        connect(&axisDragScroll,&QTimer::timeout,this,[this]{
            if(axisPressed&&axisStarted){
                const int delta=axisPoint.y()<32?-18:axisPoint.y()>viewport()->height()-32?18:0;
                const int dx=axisPoint.x()<32?-18:axisPoint.x()>viewport()->width()-32?18:0;
                if(delta||dx){if(axisRow)verticalScrollBar()->setValue(verticalScrollBar()->value()+delta);else horizontalScrollBar()->setValue(horizontalScrollBar()->value()+dx);updateAxisDrop();viewport()->update();}return;
            }
        });
        // Publish a burst of worker results in one layout pass. Keep the
        // viewport anchor and caret; refreshAppearance also exposes the caret
        // and calls ensureCursorVisible, which is wrong during scrolling.
        resourceRefreshTimer.setSingleShot(true);
        resourceRefreshTimer.setInterval(40);
        connect(&resourceManager,&mdedit::ResourceManager::ready,this,[this]{
            if(!resourceRefreshTimer.isActive())resourceRefreshTimer.start();
        });
        connect(&resourceRefreshTimer,&QTimer::timeout,this,[this]{
            for(const auto &key:resourceRenderKeys)renderedMarkdown.remove(key);
            resourceRenderKeys.clear();
            if(sourceExposed)return;
            const auto selection=textCursor();
            const int anchor=sourcePosition(selection.anchor());
            const int position=sourcePosition(selection.position());
            const int chosen=active;
            const int offset=chosen>=0?position-regions[chosen].sourceStart:0;
            QScopedValueRollback<bool> refreshing(backgroundRefresh,true);
            rebuild(chosen,offset);
            const int scroll=verticalScrollBar()->value();
            if(selection.hasSelection()){
                // Restore a selection without scrolling it into view.
                auto visible=[&](int sourceOffset){
                    int nearest=0,distance=int(source.size())+1;
                    for(const auto &region:regions){
                        if(sourceOffset<region.sourceStart||sourceOffset>region.sourceEnd)continue;
                        for(int at=region.start;at<=region.end;++at){
                            const int difference=qAbs(sourcePosition(at)-sourceOffset);
                            if(difference<distance){nearest=at;distance=difference;}
                            if(!difference)return at;
                        }
                    }
                    return nearest;
                };
                QScopedValueRollback<bool> guard(rebuilding,true);
                QTextCursor cursor(document());cursor.setPosition(visible(anchor));
                cursor.setPosition(visible(position),QTextCursor::KeepAnchor);setTextCursor(cursor);
                verticalScrollBar()->setValue(scroll);
            }
        });
        document()->setIndentWidth(24);
        installFormulaImageHandler(document());

        setReadOnly(false);setAcceptRichText(false);setUndoRedoEnabled(false);setAcceptDrops(false);setContextMenuPolicy(Qt::DefaultContextMenu);
        viewport()->setMouseTracking(true);
        connect(this,&QTextEdit::cursorPositionChanged,this,[this]{
            // Cursor signals can arrive before textChanged during a multiline edit.
            // Wait for source/region offsets to catch up before switching units.
            if(rebuilding||movingCursor||textCursor().hasSelection()||toPlainText()!=oldText)return;
            activateCaretRegion();
        });
        connect(this,&QTextEdit::selectionChanged,this,[this]{
            if(rebuilding||movingCursor||toPlainText()!=oldText)return;
            if(textCursor().hasSelection())exposeSelectedRegions();
            else if(selectionSourceExpanded)activateCaretRegion();
        });
        connect(this,&QTextEdit::textChanged,this,[this]{
            if(rebuilding)return;
            if(syncNativeTable())return;
            if(active<0)return;
            const QString now=toPlainText();
            auto &r=regions[active];
            const int delta=now.size()-oldText.size();
            // Equal separators outside the editable region can otherwise
            // absorb a newly inserted newline during the prefix scan.
            int a=r.start,b=r.end,c=r.end+delta;
            while(a<b&&a<c&&oldText[a]==now[a])++a;
            while(b>a&&c>a&&oldText[b-1]==now[c-1]){--b;--c;}
            source.replace(r.sourceStart+a-r.start,b-a,now.mid(a,c-a));
            r.sourceEnd+=delta;r.end+=delta;if(r.contentEnd>=0)r.contentEnd+=delta;
            for(int i=active+1;i<regions.size();++i){regions[i].sourceStart+=delta;regions[i].sourceEnd+=delta;if(regions[i].contentEnd>=0)regions[i].contentEnd+=delta;regions[i].start+=delta;regions[i].end+=delta;}
            oldText=now;styleActiveSource();preserveResourceSlot();refreshLineNumbers();if(sourceChanged)sourceChanged(source);
        });
    }

void LiveEditor::setSource(const QString &text,bool resetCursor){
    const bool changed=text!=source||renderedBaseUrl!=document()->baseUrl();
    if(!changed&&!resetCursor&&!regions.isEmpty())return;

    cancelAxisDrag();
    cellRangeTable.clear();cellRangePressed=cellRangeDragging=false;
    axisSource=axisIndex=-1;resourceSourceEditStart=-1;
    mathEditSlotHeight=0;mathEditSourceStart=-1;
    if(changed){
        resourceManager.setActiveUrls({});
        resourceManager.clear();
        // Clearing emits ready(), but the new document already gets a full rebuild.
        resourceRefreshTimer.stop();
        activeResourceTexts.clear();
        resourceRenderKeys.clear();
        renderedMarkdown.clear();
    }
    if(resetCursor){
        // The old document's source offsets and viewport do not apply to a file switch.
        regions.clear();
        active=-1;
    }
    source=text;
    // Render complete opening blocks first; keep source authoritative throughout.
    initialRenderLimit=resetCursor&&text.size()>6000?2500:0;
    const bool staged=initialRenderLimit>0;
    // Start at the top without exposing the first block's Markdown syntax.
    rebuild(-1,0,resetCursor?0:-1,false);
    if(staged)initialRenderTimer.start();
    if(resetCursor){
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
    }
}

QString LiveEditor::markdown()const{return source;}

void LiveEditor::setThemePalette(const QPalette &palette)
{
    NumberedEditor<QTextBrowser>::setThemePalette(palette);
    renderedMarkdown.clear();
    resourceRenderKeys.clear();
}

void LiveEditor::refreshAppearance(){
        const auto range=sourceSelection();rebuild(-1,0);restoreSourceSelection(range.first,range.second);
    }

void LiveEditor::updateResourceWindow(){
    if(rebuilding)return;
    QSet<QUrl> urls;
    QSet<QString> texts;
    if(!sourceExposed){
        const qreal top=verticalScrollBar()->value()-viewport()->height();
        const qreal bottom=verticalScrollBar()->value()+2*viewport()->height();
        auto *layout=document()->documentLayout();
        for(const auto &region:regions){
            const auto first=document()->findBlock(region.start),last=document()->findBlock(region.end);
            if(layout->blockBoundingRect(last).bottom()<top||layout->blockBoundingRect(first).top()>bottom)continue;
            const auto text=source.mid(region.sourceStart,region.sourceEnd-region.sourceStart);
            const auto referenced=mdedit::ResourceManager::referencedUrls(text,document()->baseUrl());
            if(!referenced.isEmpty())texts.insert(text.trimmed());
            urls.unite(referenced);
        }
    }
    const bool changed=resourceManager.setActiveUrls(urls)||activeResourceTexts!=texts;
    activeResourceTexts=texts;
    if(changed)resourceRefreshTimer.start();
}
