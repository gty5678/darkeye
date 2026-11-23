#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>

void LiveEditor::mouseMoveEvent(QMouseEvent *event){
        tableHoverPoint=event->position().toPoint();viewport()->update();
        if(cellRangePressed&&cellRangeTable&&(event->buttons()&Qt::LeftButton)){
            if(!cellRangeDragging&&(tableHoverPoint-cellRangeOrigin).manhattanLength()>=QApplication::startDragDistance()){
                cellRangeDragging=true;pressedLink.clear();
                const int sy=verticalScrollBar()->value(),sx=horizontalScrollBar()->value();
                QScopedValueRollback<bool> guard(movingCursor,true);
                if(nativeTable)revealNativeCell(QTextCursor(document()));
                setTextCursor(cellRangeTable->cellAt(cellAnchorRow,cellAnchorColumn).firstCursorPosition());
                // Collapsing cell source temporarily moves the caret to the
                // document start. Grid selection must keep its viewport fixed.
                verticalScrollBar()->setValue(sy);horizontalScrollBar()->setValue(sx);
            }
            if(cellRangeDragging)for(const auto &item:axisControls())if(item.table==cellRangeTable){
                cellEndRow=item.rows.size()-1;cellEndColumn=item.columns.size()-1;
                for(int r=0;r<item.rows.size();++r)if(tableHoverPoint.y()<=item.rows[r].bottom()){cellEndRow=r;break;}
                for(int c=0;c<item.columns.size();++c)if(tableHoverPoint.x()<=item.columns[c].right()){cellEndColumn=c;break;}
                viewport()->setCursor(Qt::CrossCursor);break;
            }
            event->accept();return;
        }
        for(const auto &item:codeCopyControls())if(item.rect.contains(tableHoverPoint)){
            viewport()->setCursor(Qt::PointingHandCursor);viewport()->setToolTip("复制代码");return;
        }
        for(const auto &item:blockControls())if(item.open.contains(tableHoverPoint)||item.edit.contains(tableHoverPoint)){
            const bool disabled=item.open.contains(tableHoverPoint)&&item.file.isEmpty();
            viewport()->setCursor(disabled?Qt::ArrowCursor:Qt::PointingHandCursor);
            viewport()->setToolTip(disabled?"此块没有关联的原文件":item.open.contains(tableHoverPoint)?"打开原文件":"编辑此块的 Markdown 原代码");return;
        }
        if(axisPressed){
            axisPoint=tableHoverPoint;
            if(!axisStarted&&(axisPoint-axisOrigin).manhattanLength()>=QApplication::startDragDistance()){axisStarted=true;axisDragScroll.start();viewport()->setCursor(Qt::ClosedHandCursor);}
            if(axisStarted)updateAxisDrop();event->accept();return;
        }
        for(const auto &item:axisControls()){
            if(item.addRow.contains(tableHoverPoint)||item.addColumn.contains(tableHoverPoint)){viewport()->setCursor(Qt::PointingHandCursor);viewport()->setToolTip(item.addRow.contains(tableHoverPoint)?"追加一行":"追加一列");return;}
            for(const auto &rect:item.rows)if(QRectF(item.frame.left()-22,rect.top()-2,23,rect.height()+4).contains(tableHoverPoint)){viewport()->setCursor(Qt::OpenHandCursor);viewport()->setToolTip("点击选择行，Shift 点击扩选，拖动调整顺序");return;}
            for(const auto &rect:item.columns)if(QRectF(rect.left()-2,item.frame.top()-22,rect.width()+4,23).contains(tableHoverPoint)){viewport()->setCursor(Qt::OpenHandCursor);viewport()->setToolTip("点击选择列，Shift 点击扩选，拖动调整顺序");return;}
        }
        viewport()->setToolTip({});
        if(event->buttons()&Qt::LeftButton){
            // A layout change must not turn another event at the same physical
            // pointer position into a new selection boundary.
            const QPoint point=event->globalPosition().toPoint();
            if(selectionDragPointValid&&point==lastSelectionDragPoint)return;
            lastSelectionDragPoint=point;selectionDragPointValid=true;
        }else selectionDragPointValid=false;
        QTextBrowser::mouseMoveEvent(event);
        if(event->buttons()==Qt::NoButton){
            const bool overLink=!clickableAnchorAt(event->position().toPoint()).isEmpty();
            viewport()->setCursor(overLink?Qt::PointingHandCursor:Qt::IBeamCursor);
        }
    }

void LiveEditor::mousePressEvent(QMouseEvent *event){
        selectionDragPointValid=false;
        QScopedValueRollback<bool> mouseActivation(mouseActivating,true);
        // A grip projects the current cell rectangle onto its row/column axis.
        const QPointer<QTextTable> selectedTable=cellRangeDragging?cellRangeTable:nullptr;
        const int selectedTop=qMin(cellAnchorRow,cellEndRow),selectedBottom=qMax(cellAnchorRow,cellEndRow);
        const int selectedLeft=qMin(cellAnchorColumn,cellEndColumn),selectedRight=qMax(cellAnchorColumn,cellEndColumn);
        if(event->button()==Qt::LeftButton){
            cellRangeTable.clear();cellRangePressed=cellRangeDragging=false;viewport()->update();
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            for(const auto &item:codeCopyControls())if(item.rect.contains(event->position())){
                const auto &region=regions[item.region];
                const QString raw=source.mid(region.sourceStart,region.contentEnd-region.sourceStart);
                const auto body=codeBody(raw,region.codeFenceStart);
                QApplication::clipboard()->setText(raw.mid(body.start,body.end-body.start));
                copiedCodeSource=region.sourceStart;copyFeedbackTimer.start();viewport()->update();
                event->accept();return;
            }
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            for(const auto &item:blockControls()){
                if(item.open.contains(event->position())){
                    if(!item.file.isEmpty())QDesktopServices::openUrl(item.file);
                    event->accept();return;
                }
                if(item.edit.contains(event->position())){
                    editResourceSourceAt(regions[item.region].sourceStart);setFocus();event->accept();return;
                }
            }
        }
        if(event->button()==Qt::LeftButton&&!(event->modifiers()&~Qt::ShiftModifier)){
            const QPoint point=event->position().toPoint();
            for(const auto &item:axisControls()){
                if(item.addRow.contains(point)||item.addColumn.contains(point)){
                    axisSource=axisIndex=-1;
                    changeMarkdownTableAt(item.start,item.rows.size()-1,item.columns.size()-1,item.addRow.contains(point)?"insertRowAfter":"insertColumnAfter");event->accept();return;
                }
                for(int axis=0;axis<2;++axis){const auto &rects=axis==0?item.rows:item.columns;
                    for(int i=0;i<rects.size();++i){const QRectF handle=axis==0?QRectF(item.frame.left()-22,rects[i].top()-2,23,rects[i].height()+4):QRectF(rects[i].left()-2,item.frame.top()-22,rects[i].width()+4,23);
                        if(!handle.contains(point))continue;
                        const int first=axis==0?selectedTop:selectedLeft,last=axis==0?selectedBottom:selectedRight;
                        if(selectedTable==item.table&&i>=first&&i<=last){
                            axisSource=item.start;axisRow=axis==0;axisIndex=first;axisLast=last;
                        }
                        const bool same=axisSource==item.start&&axisRow==(axis==0)&&axisIndex>=0;
                        const bool extend=same&&(event->modifiers()&Qt::ShiftModifier);
                        const bool retain=same&&i>=qMin(axisIndex,axisLast)&&i<=qMax(axisIndex,axisLast);
                        if(!extend&&!retain)axisIndex=axisLast=i;
                        if(extend)axisLast=i;
                        axisSource=item.start;axisRow=axis==0;axisPressed=true;axisStarted=false;
                        axisSnapshot=source;axisOrigin=axisPoint=point;
                        const int sy=verticalScrollBar()->value(),sx=horizontalScrollBar()->value();
                        // Axis selection is structural. Restore any cell source
                        // and keep the text cursor collapsed to avoid text highlighting.
                        {QScopedValueRollback<bool> guard(movingCursor,true);
                            if(nativeTable)revealNativeCell(QTextCursor(document()));
                            setTextCursor(item.table->cellAt(axisRow?i:0,axisRow?0:i).firstCursorPosition());}
                        verticalScrollBar()->setValue(sy);horizontalScrollBar()->setValue(sx);
                        viewport()->update();event->accept();return;
                    }
                }
            }
            axisSource=axisIndex=-1;
        }
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
        }
        int formulaRegion=-1;
        if(event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier){
            const auto point=event->position().toPoint();
            // Track a possible grid drag even when the press hits a link;
            // an unmoved click retains the normal link action.
            // A horizontal rule has no glyphs. Qt's fuzzy text hit test can
            // choose the next paragraph even when clicking the painted line.
            for(int i=0;i<regions.size();++i){
                const auto &region=regions[i];
                if(i==active)continue;
                const auto block=document()->findBlock(region.start);
                if(!block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))continue;
                const auto bounds=document()->documentLayout()->blockBoundingRect(block)
                    .translated(-horizontalScrollBar()->value(),-verticalScrollBar()->value());
                if(point.y()>=bounds.top()&&point.y()<=bounds.bottom()){
                    // This mouse hit precedes Qt's native caret placement.
                    // Rebuild's keyboard scroll compensation would otherwise
                    // anchor the new caret to the old caret's screen position.
                    const int scroll=verticalScrollBar()->value();
                    rebuild(i,0);
                    verticalScrollBar()->setValue(scroll);
                    event->accept();return;
                }
            }
            const auto hitCursor=cursorForPosition(point);
            if(auto *table=hitCursor.currentTable()){
                const int index=regionAt(table->firstPosition());
                const auto node=index>=0?tableNodeAt(source,regions[index].sourceStart):std::nullopt;
                if(node&&!node->resource){
                    const auto cell=table->cellAt(hitCursor);
                    cellRangeTable=table;cellRangeSource=node->source.begin;
                    cellAnchorRow=cellEndRow=cell.row();cellAnchorColumn=cellEndColumn=cell.column();
                    cellRangePressed=true;cellRangeOrigin=point;
                }
            }
            const QString anchor=clickableAnchorAt(point);
            if(!anchor.isEmpty()){
                pressedLink=anchor;
                linkPressPosition=point;
                event->accept();
                return;
            }
            const int mathRegion=regionAt(cursorForPosition(point).position());
            if(event->modifiers()==Qt::NoModifier&&mathRegion>=0&&mathRegion!=active){
                const auto &region=regions[mathRegion];
                const QString raw=source.mid(region.sourceStart,
                (region.contentEnd>=0?region.contentEnd:region.sourceEnd)-region.sourceStart);
                for(const auto &span:scanSpans(raw)){
                    if(span.kind!=SpanKind::DisplayMath)continue;
                    int offset=span.start+span.length-2;
                    int lineEnd=offset;
                    while(lineEnd>span.start+2&&(raw[lineEnd-1]==' '||raw[lineEnd-1]=='\t'))--lineEnd;
                    if(lineEnd>span.start+2&&raw[lineEnd-1]=='\n'){
                        offset=lineEnd-1;
                        if(offset>span.start+2&&raw[offset-1]=='\r')--offset;
                    }
                    rebuild(mathRegion,offset);
                    event->accept();return;
                }
            }
            const int resourceRegion=regionAt(cursorForPosition(point).position());
            if(isResourceRegion(resourceRegion)&&resourceRegion!=active){event->accept();return;}
            if(!isResourceRegion(resourceRegion))resourceSourceEditStart=-1;
            const int hit=cursorForPosition(point).position();
            const auto nativeCursor=cursorForPosition(point);
            if(nativeCursor.currentTable()&&bindNativeTable(nativeCursor)){
                const auto cell=nativeTable->cellAt(nativeCursor);
                cellRangeTable=nativeTable;cellRangeSource=nativeStart;
                cellAnchorRow=cellEndRow=cell.row();cellAnchorColumn=cellEndColumn=cell.column();
                cellRangePressed=true;cellRangeOrigin=point;
                QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::mousePressEvent(event);return;
            }
            const auto block=document()->findBlock(hit);
            if(block.textList()){
                const QRect edge=cursorRect(QTextCursor(block));
                const int at=sourcePosition(block.position());
                const int lineStart=at==0?0:source.lastIndexOf('\n',at-1)+1;
                const auto marker=listMarker().match(source.mid(lineStart).section('\n',0,0));
                const qreal width=qMax(document()->indentWidth(),QFontMetricsF(font()).horizontalAdvance(marker.captured())+8);
                if(marker.hasMatch()&&point.y()>=edge.top()&&point.y()<=edge.bottom()&&
                    point.x()<edge.left()&&point.x()>=edge.left()-width){
                    const QString prefix=marker.captured();
                    const int task=prefix.indexOf('[');
                    // Enter the task state just before ']' so Backspace can
                    // replace the space or x without disturbing the brackets.
                    const int offset=task>=0?task+2:prefix.indexOf(QRegularExpression("[-+*0-9]"));
                    rebuild(-1,0,lineStart+offset);
                    event->accept();
                    return;
                }
            }
            for(int pos:{hit,hit-1}){
                if(pos<0||pos>=oldText.size()||oldText[pos]!=QChar::ObjectReplacementCharacter)continue;
                const int region=regionAt(pos);
                if(region<0||!regions[region].inlinePart)continue;
                QTextCursor edge(document());edge.setPosition(pos);
                const auto left=cursorRect(edge);
                edge.setPosition(pos+1);
                const auto right=cursorRect(edge);
                if(left.united(right).contains(point)){formulaRegion=region;break;}
            }
        }
        {
            QScopedValueRollback<bool> guard(movingCursor,true);
            QTextBrowser::mousePressEvent(event);
        }
        if(formulaRegion>=0)rebuild(formulaRegion,0);
        else activateCaretRegion();
    }

void LiveEditor::mouseReleaseEvent(QMouseEvent *event){
        selectionDragPointValid=false;
        if(event->button()==Qt::LeftButton&&cellRangePressed){
            cellRangePressed=false;
            if(cellRangeDragging){viewport()->unsetCursor();viewport()->update();event->accept();return;}
        }
        if(event->button()==Qt::LeftButton&&axisPressed){
            const int offset=axisSource,from=qMin(axisIndex,axisLast),count=qAbs(axisLast-axisIndex)+1,boundary=axisBoundary;const bool row=axisRow;
            bool inside=false;for(const auto &item:axisControls())if(item.start==axisSource)inside=item.frame.adjusted(-25,-25,25,25).contains(event->position());
            const bool apply=axisStarted&&source==axisSnapshot&&inside&&viewport()->rect().contains(event->position().toPoint());
            cancelAxisDrag();if(apply){if(reorderMarkdownAxisAt(offset,row,from,boundary,count)){axisIndex=boundary>from?boundary-count:boundary;axisLast=axisIndex+count-1;}}event->accept();return;
        }
        if(event->button()==Qt::LeftButton&&!pressedLink.isEmpty()){
            const QString anchor=pressedLink;
            pressedLink.clear();
            const auto point=event->position().toPoint();
            event->accept();
            if(event->modifiers()==Qt::NoModifier&&anchorAt(point)==anchor&&
               (point-linkPressPosition).manhattanLength()<QApplication::startDragDistance())
                {if(QUrl(anchor).scheme()=="mdedit-pdf")handlePdfAction(QUrl(anchor));
                 else emit anchorClicked(QUrl(anchor));}
            return;
        }
        QTextBrowser::mouseReleaseEvent(event);
    }

void LiveEditor::focusInEvent(QFocusEvent *event){
        QTextBrowser::focusInEvent(event);
        if(event->reason()!=Qt::ActiveWindowFocusReason&&active<0)activateCaretRegion();
    }

void LiveEditor::focusOutEvent(QFocusEvent *event){
        if(mathPreviewPanel)mathPreviewPanel->hide();
        if(axisPressed)cancelAxisDrag();
        cellRangePressed=false;
        QTextBrowser::focusOutEvent(event);
        // Menus and dialogs take focus before their actions read the selection.
        // Rebuilding here discards it and moves the cursor to the document end.
        // The next caret movement will switch the active region when needed.
    }

void LiveEditor::keyPressEvent(QKeyEvent *event){
        if(cellRangeTable&&cellRangeDragging){
            if(event->matches(QKeySequence::Copy)||event->matches(QKeySequence::Cut)){
                editSelectedCells(true,event->matches(QKeySequence::Cut));event->accept();return;
            }
            if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace){
                editSelectedCells(false,true);event->accept();return;
            }
            if(event->key()==Qt::Key_Shift||event->key()==Qt::Key_Control||event->key()==Qt::Key_Meta||event->key()==Qt::Key_Alt){event->accept();return;}
            cellRangeTable.clear();cellRangeDragging=false;viewport()->update();
            if(event->key()==Qt::Key_Escape){event->accept();return;}
        }
        if(axisPressed&&event->key()==Qt::Key_Escape){cancelAxisDrag();event->accept();return;}
        if(axisSource>=0&&axisIndex>=0&&!axisPressed&&(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)){
            removeSelectedAxes();event->accept();return;
        }
        if(axisSource>=0&&axisIndex>=0&&!axisPressed){
            if(event->matches(QKeySequence::Copy)||event->matches(QKeySequence::Cut)){
                clipboardNativeCell(event->matches(QKeySequence::Cut));event->accept();return;
            }
            axisSource=axisIndex=axisLast=-1;viewport()->update();
            if(event->key()==Qt::Key_Escape){event->accept();return;}
        }
        // A selected native table is a structural selection, not a cell edit.
        if(textCursor().hasSelection()&&
           (event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete)){
            const auto cursor=textCursor();
            int start=-1,end=-1;
            if(cursor.selectionStart()==0&&cursor.selectionEnd()==document()->characterCount()-1){
                start=0;end=source.size();
            }else if(cursor.currentTable()&&bindNativeTable(cursor)){
                int row,count,column,columns;
                cursor.selectedTableCells(&row,&count,&column,&columns);
                if(row==0&&column==0&&count==nativeTable->rows()&&columns==nativeTable->columns()){
                    start=nativeStart;end=nativeEnd;
                }
            }
            if(start>=0){
                source.remove(start,end-start);
                rebuild(-1,0,start);
                if(sourceChanged)sourceChanged(source);
                event->accept();return;
            }
        }
        // Qt can tag native arrow keys with KeypadModifier (including macOS
        // arrows). It identifies the key, not an extra navigation command.
        const auto navigationModifiers=event->modifiers()&~Qt::KeypadModifier;
        QScopedValueRollback<bool> verticalNavigation(movingVertically,event->key()==Qt::Key_Up||event->key()==Qt::Key_Down);
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){
                if(history)history(event->matches(QKeySequence::Redo));event->accept();return;
            }
            const auto cell=nativeTable->cellAt(textCursor());
            const bool leaveAbove=event->key()==Qt::Key_Up&&cell.row()==0&&cursorRect().top()<=cursorRect(cell.firstCursorPosition()).top();
            const bool leaveBelow=event->key()==Qt::Key_Down&&cell.row()+1==nativeTable->rows()&&cursorRect().bottom()>=cursorRect(cell.lastCursorPosition()).bottom();
            if((leaveAbove||leaveBelow)&&navigationModifiers==Qt::NoModifier&&!textCursor().hasSelection()){
                QTextCursor outside(document());outside.setPosition(leaveAbove?qMax(0,nativeTable->firstPosition()-2):qMin(document()->characterCount()-1,nativeTable->lastPosition()+2));
                {QScopedValueRollback<bool> guard(movingCursor,true);setTextCursor(outside);}
                revealNativeCell(textCursor());nativeTable.clear();activateCaretRegion();event->accept();return;
            }
            if(event->key()==Qt::Key_Tab||event->key()==Qt::Key_Backtab){
                const bool backwards=event->key()==Qt::Key_Backtab||(event->modifiers()&Qt::ShiftModifier);
                int next=cell.row()*nativeTable->columns()+cell.column()+(backwards?-1:1);
                if(next>=nativeTable->rows()*nativeTable->columns())nativeTable->appendRows(1);
                if(next>=0)setTextCursor(nativeTable->cellAt(next/nativeTable->columns(),next%nativeTable->columns()).firstCursorPosition());
                else{
                    QTextCursor outside(document());outside.setPosition(qMax(0,nativeTable->firstPosition()-2));
                    {QScopedValueRollback<bool> guard(movingCursor,true);setTextCursor(outside);}
                    revealNativeCell(textCursor());nativeTable.clear();activateCaretRegion();
                }
                event->accept();return;
            }
            if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&
               !(navigationModifiers&~Qt::ShiftModifier)){
                auto cursor=textCursor();
                const bool append=!(navigationModifiers&Qt::ShiftModifier)&&!cursor.hasSelection()&&
                    cell.row()==nativeTable->rows()-1&&cursor.position()==cell.lastPosition();
                {
                    QScopedValueRollback<bool> guard(movingCursor,true);
                    cursor.beginEditBlock();
                    if(append){
                        nativeTable->appendRows(1);
                        cursor=nativeTable->cellAt(nativeTable->rows()-1,cell.column()).firstCursorPosition();
                    }else cursor.insertText(QString(QChar::LineSeparator));
                    cursor.endEditBlock();
                    setTextCursor(cursor);
                }
                activateCaretRegion();event->accept();return;
            }
            // Crossing a cell boundary must not merge cells or remove the table.
            if(!textCursor().hasSelection()&&((event->key()==Qt::Key_Backspace&&textCursor().position()==cell.firstPosition())||
                (event->key()==Qt::Key_Delete&&textCursor().position()==cell.lastPosition()))){event->accept();return;}
            {QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::keyPressEvent(event);}
            activateCaretRegion();
            return;
        }
        const bool deleting=event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete||event->matches(QKeySequence::Cut);
        const int scrollBeforeEdit=verticalScrollBar()->value();
        const bool newline=event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter;
        if(newline&&!(event->modifiers()&~(Qt::KeypadModifier|Qt::ShiftModifier))){
            const auto cursor=textCursor();
            const bool all=cursor.hasSelection()&&cursor.selectionStart()==0&&
                cursor.selectionEnd()==document()->characterCount()-1;
            const int start=all?0:sourcePosition(cursor.selectionStart());
            const int end=all?int(source.size()):sourcePosition(cursor.selectionEnd());
            int replaceStart=start;
            QString insertion="\n";
            if(!cursor.hasSelection()&&!(event->modifiers()&Qt::ShiftModifier)){
                const int lineStart=start==0?0:source.lastIndexOf('\n',start-1)+1;
                int lineEnd=source.indexOf('\n',start);if(lineEnd<0)lineEnd=source.size();
                const QString line=source.mid(lineStart,lineEnd-lineStart);
                bool protectedBlock=false;
                for(const auto &span:scanSpans(source))
                    if(span.start<=start&&start<span.start+span.length&&
                       (span.fenced||span.kind==SpanKind::DisplayMath))protectedBlock=true;
                static const QRegularExpression item(R"(^([ \t]*)([-+*]|([0-9]{1,9})([.)]))([ \t]+)(?:\[([ xX])\]([ \t]+))?)");
                const auto match=item.match(line);
                if(!protectedBlock&&match.hasMatch()&&start>=lineStart+match.capturedEnd()){
                    if(line.mid(match.capturedEnd()).trimmed().isEmpty()){
                        replaceStart=lineStart;insertion.clear();
                    }else{
                        QString marker=match.captured(2);
                        if(!match.captured(3).isEmpty())marker=QString::number(match.captured(3).toLongLong()+1)+match.captured(4);
                        insertion+=match.captured(1)+marker+match.captured(5);
                        if(!match.captured(6).isEmpty())insertion+="[ ]"+match.captured(7);
                    }
                }
            }
            source.replace(replaceStart,qMax(0,end-replaceStart),insertion);
            rebuild(-1,0,replaceStart+insertion.size());
            if(sourceChanged)sourceChanged(source);
            event->accept();
            return;
        }
        if(event->matches(QKeySequence::Undo)||event->matches(QKeySequence::Redo)){
            if(history)history(event->matches(QKeySequence::Redo));
            event->accept();return;
        }
        // A plain newline deletion has no grapheme or word-boundary rules to
        // delegate to QTextControl. Apply it to the authoritative source before
        // activating a region: activation and exposeSource would each rebuild
        // the whole document before the final preview rebuild below.
        if(!textCursor().hasSelection()&&event->modifiers()==Qt::NoModifier&&
           (event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete)){
            const int at=sourcePosition(textCursor().position());
            const int removeAt=event->key()==Qt::Key_Backspace?at-1:at;
            if(removeAt>=0&&removeAt<source.size()&&source[removeAt]=='\n'){
                source.remove(removeAt,1);
                rebuild(-1,0,removeAt);
                verticalScrollBar()->setValue(scrollBeforeEdit);
                const QRect caret=cursorRect();
                if(caret.top()<0||caret.bottom()>=viewport()->height())ensureCursorVisible();
                if(sourceChanged)sourceChanged(source);
                event->accept();return;
            }
        }
        const bool horizontalArrow=(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)&&
            navigationModifiers==Qt::NoModifier&&!textCursor().hasSelection();
        auto renderedInline=[this](int i){
            const auto &r=regions[i];
            return i!=active&&r.inlinePart&&r.end>r.start&&
                source.mid(r.sourceStart,r.contentEnd-r.sourceStart)!=oldText.mid(r.start,r.end-r.start);
        };
        auto enterRenderedInline=[&]{
            const int position=textCursor().position();
            for(int i=0;i<regions.size();++i){
                const auto &r=regions[i];
                if(!renderedInline(i))continue;
                const bool enterLeft=event->key()==Qt::Key_Right&&position==r.start;
                const bool enterRight=event->key()==Qt::Key_Left&&position==r.end;
                if(!enterLeft&&!enterRight)continue;
                const QString raw=source.mid(r.sourceStart,r.contentEnd-r.sourceStart);
                int contentStart=0,contentEnd=raw.size();
                const auto spans=scanSpans(raw);
                if(spans.size()==1&&spans.first().start==0&&spans.first().length==raw.size()){
                    // Code and math scanners include their delimiters. Enter
                    // their source bodies directly, including multi-backtick code.
                    int delimiter=1;
                    while(delimiter<raw.size()&&raw[delimiter]==raw[0])++delimiter;
                    contentStart=delimiter;contentEnd=raw.size()-delimiter;
                }else{
                    // Emphasis, strikeout and highlighting styles already
                    // describe content ranges without the surrounding markers.
                    const auto styles=inlineEditingStyles(raw,theme);
                    if(!styles.isEmpty()){
                        contentStart=raw.size();contentEnd=0;
                        for(const auto &style:styles){
                            contentStart=qMin(contentStart,style.start);
                            contentEnd=qMax(contentEnd,style.start+style.length);
                        }
                    }
                }
                const int offset=enterLeft?contentStart:contentEnd;
                // Expand at the content edge without taking a visual step,
                // which would consume the first or last content character.
                rebuild(i,offset);
                QScopedValueRollback<bool> guard(movingCursor,true);
                auto caret=textCursor();
                caret.setPosition(regions[active].start+offset);
                setTextCursor(caret);
                event->accept();return true;
            }
            return false;
        };
        if(horizontalArrow&&enterRenderedInline())return;
        if(active<0)activateCaretRegion();
        if(active<0){event->ignore();return;}
        if((event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)&&
            !(navigationModifiers&~Qt::ShiftModifier)){
            const auto selection=sourceSelection();const int at=selection.second;
            QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme,source);boundary.setPosition(at);
            int next=event->key()==Qt::Key_Left?boundary.toPreviousBoundary():boundary.toNextBoundary();
            if(next>=0){
                const auto here=listPrefixAt(at),there=listPrefixAt(next);
                if(here||there){
                    if(event->modifiers()&Qt::ShiftModifier)exposeSource(selection.first,next);
                    else{
                        if(selection.first!=selection.second)next=event->key()==Qt::Key_Left?
                            qMin(selection.first,selection.second):qMax(selection.first,selection.second);
                        rebuild(-1,0,next);
                    }
                    event->accept();return;
                }
            }
        }
        if(!textCursor().hasSelection()){
            int pos=textCursor().position();
            if(event->key()==Qt::Key_Backspace&&pos==regions[active].start&&regions[active].sourceStart>0){
                const int at=regions[active].sourceStart;
                exposeSource(at,at);
            }
            if(event->key()==Qt::Key_Delete&&pos==regions[active].end&&active+1<regions.size()){
                const int at=regions[active].sourceStart+pos-regions[active].start;
                exposeSource(at,at);
            }
        }
        if(newline||event->key()==Qt::Key_Backspace||event->key()==Qt::Key_Delete||
           event->matches(QKeySequence::Cut)||event->matches(QKeySequence::Paste)||
           (!event->text().isEmpty()&&!(event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier))))prepareSelection();
        // A source line start must stay a line start even when the next
        // rendered block has a different indent or hidden Markdown prefix.
        const int beforeMove=sourcePosition(textCursor().position());
        const bool keepLineStart=movingVertically&&navigationModifiers==Qt::NoModifier&&
            !textCursor().hasSelection()&&(beforeMove==0||source[beforeMove-1]=='\n');
        std::optional<QPair<int,int>> verticalPrefix;
        int prefixColumn=0;
        if((event->key()==Qt::Key_Up||event->key()==Qt::Key_Down)&&
            navigationModifiers==Qt::NoModifier&&!textCursor().hasSelection()){
            const int at=sourcePosition(textCursor().position());
            verticalPrefix=listPrefixAt(at);
            if(verticalPrefix&&at<verticalPrefix->second)prefixColumn=at-verticalPrefix->first;
            else verticalPrefix.reset();
        }
        // Plain text edits already update source and all following offsets in
        // textChanged. Keep that incremental document instead of laying out the
        // entire preview again for every Backspace/Delete in a paragraph.
        bool incrementalDeletion=false;
        if(deleting&&!sourceExposed&&active>=0&&event->modifiers()==Qt::NoModifier){
            const auto &region=regions[active];
            const QString raw=source.mid(region.sourceStart,
                (region.contentEnd>=0?region.contentEnd:region.sourceEnd)-region.sourceStart);
            static const QRegularExpression syntax(R"([\\`*_{}\[\]()<>#+.!|~=$:\n\r-])");
            auto removed=textCursor();
            if(!removed.hasSelection())
                removed.movePosition(event->key()==Qt::Key_Backspace?QTextCursor::PreviousCharacter:QTextCursor::NextCharacter,
                                     QTextCursor::KeepAnchor);
            incrementalDeletion=removed.hasSelection()&&removed.selectionStart()>region.start&&
                removed.selectionEnd()<region.end&&!raw.contains(syntax)&&
                raw==oldText.mid(region.start,region.end-region.start);
            // Keep the inline editing region when its body is deleted. Empty
            // backtick pairs are not Markdown code spans, so reparsing here
            // would expose the surrounding paragraph as source.
            if(region.inlinePart&&raw.startsWith('`')&&
               raw==oldText.mid(region.start,region.end-region.start)){
                const auto spans=scanSpans(raw);
                if(spans.size()==1&&spans.first().kind==SpanKind::Code&&!spans.first().fenced&&
                   spans.first().start==0&&spans.first().length==raw.size()){
                    int delimiter=1;
                    while(delimiter<raw.size()&&raw[delimiter]=='`')++delimiter;
                    if(removed.hasSelection()&&removed.selectionStart()>=region.start+delimiter&&
                       removed.selectionEnd()<=region.end-delimiter&&
                       !removed.selectedText().contains(QChar::ParagraphSeparator))incrementalDeletion=true;
                }
            }
        }
        {
            // QTextControl still uses the old document positions until its key
            // handler returns. Rebuild only after that movement has finished.
            QScopedValueRollback<bool> guard(movingCursor,true);
            if(newline)QTextEdit::keyPressEvent(event);
            else QTextBrowser::keyPressEvent(event);
        }
        // Cross-region edits temporarily use the full source so Qt can apply
        // its usual deletion and selection rules across Markdown boundaries.
        // Selection signals were suppressed while QTextControl moved. Expose
        // the selected previews now, preserving both anchor and moving edge;
        // rebuilding with just the caret would collapse Shift navigation.
        if(textCursor().hasSelection()&&toPlainText()==oldText){
            exposeSelectedRegions();
            return;
        }
        if(keepLineStart){
            auto destination=textCursor();
            destination.movePosition(QTextCursor::StartOfLine);
            int at=sourcePosition(destination.position());
            if(destination.atBlockStart())at=at==0?0:source.lastIndexOf('\n',at-1)+1;
            rebuild(-1,0,at);
            event->accept();return;
        }
        if(verticalPrefix){
            const auto destination=listPrefixAt(sourcePosition(textCursor().position()));
            if(destination&&destination->first!=verticalPrefix->first){
                rebuild(-1,0,destination->first+qMin(prefixColumn,destination->second-destination->first-1));
                event->accept();return;
            }
        }
        if(sourceExposed||(deleting&&!incrementalDeletion))rebuild(-1,0,sourcePosition(textCursor().position()));
        else if(toPlainText()==oldText){
            // Expand on arrival at the inward edge, before generic region
            // activation can map the caret past a content character.
            if(horizontalArrow&&enterRenderedInline())return;
            activateCaretRegion();
        }
        if(deleting){
            // Exposing source and restoring preview can both scroll the cursor
            // into view using temporary geometry. Keep the viewport anchored
            // to the final layout instead of inheriting those scroll changes.
            verticalScrollBar()->setValue(scrollBeforeEdit);
            const QRect caret=cursorRect();
            if(caret.top()<0||caret.bottom()>=viewport()->height())ensureCursorVisible();
        }
    }

void LiveEditor::insertFromMimeData(const QMimeData *mime){
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            textCursor().insertText(mime->text());return;
        }
        prepareSelection();insertPlainText(mime->text());
        if(sourceExposed)rebuild(-1,0,sourcePosition(textCursor().position()));
    }

void LiveEditor::inputMethodEvent(QInputMethodEvent *event){
        if(textCursor().currentTable()&&bindNativeTable(textCursor())&&nativeSelection()){
            QTextBrowser::inputMethodEvent(event);
            if(nativeTable){QScopedValueRollback<bool> guard(rebuilding,true);layoutNativeTable(nativeTable);}
            return;
        }
        prepareSelection();
        QTextBrowser::inputMethodEvent(event);
        if(sourceExposed&&(!event->commitString().isEmpty()||event->replacementLength()))
            rebuild(-1,0,sourcePosition(textCursor().position()));
    }

std::optional<QPair<int,int>> LiveEditor::listPrefixAt(int at)const{
        const int start=at==0?0:source.lastIndexOf('\n',at-1)+1;
        const auto marker=listMarker().match(source.mid(start).section('\n',0,0));
        if(!marker.hasMatch())return {};
        const int end=start+marker.capturedLength();
        if(sourceExposed){
            for(const auto &span:scanSpans(source))
                if(span.kind==SpanKind::Code&&span.start<=start&&span.start+span.length>start)return {};
            return qMakePair(start,end);
        }
        for(const auto &region:regions)
            if(region.sourceStart<=end&&region.sourceEnd>=start&&!region.codeBlock&&
                (region.listItem||document()->findBlock(region.start).textList()))return qMakePair(start,end);
        return {};
    }

const QRegularExpression &LiveEditor::listMarker(){
        static const QRegularExpression marker(R"(^[ \t]*(?:[-+*]|[0-9]{1,9}[.)])(?:[ \t]+|$)(?:\[[ xX]\][ \t]+)?)");
        return marker;
    }

