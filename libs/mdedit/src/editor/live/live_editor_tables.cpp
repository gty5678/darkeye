#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>


std::optional<mdedit::BlockViewNode> LiveEditor::tableNodeAt(const QString &source,int offset){
    for(const auto &node:mdedit::MarkdownParser::parse(source)){
        if(offset<node.source.begin||offset>node.source.end)continue;
        if(node.kind==mdedit::BlockKind::Table)return node;
        if(node.resource&&QFileInfo(QUrl(node.resource->uri).path()).suffix().compare("csv",Qt::CaseInsensitive)==0)return node;
    }
    return {};
}
bool LiveEditor::moveTableBlockAt(int offset,int direction){
    if(direction!=1&&direction!=-1)return false;
    if(!tableNodeAt(source,offset))return false;
    const auto nodes=mdedit::MarkdownParser::parse(source);int index=-1;
    for(int i=0;i<nodes.size();++i)if(offset>=nodes[i].source.begin&&offset<=nodes[i].source.end){index=i;break;}
    if(index<0||index+direction<0||index+direction>=nodes.size())return false;
    const auto &a=nodes[qMin(index,index+direction)],&b=nodes[qMax(index,index+direction)];
    const QString first=source.mid(a.source.begin,a.source.end-a.source.begin),second=source.mid(b.source.begin,b.source.end-b.source.begin);
    const QString gap=source.mid(a.source.end,b.source.begin-a.source.end);
    source.replace(a.source.begin,b.source.end-a.source.begin,second+gap+first);
    const int scroll=verticalScrollBar()->value();rebuild(-1,0);verticalScrollBar()->setValue(scroll);
    if(sourceChanged)sourceChanged(source);return true;
}
namespace {
constexpr int sourceCellProperty=QTextFormat::UserProperty+41;
QString cellMarkdown(const QTextTableCell &cell){
    auto cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);
    if(cell.firstCursorPosition().blockFormat().boolProperty(sourceCellProperty))
        return cursor.selectedText().replace(QChar::ParagraphSeparator,QStringLiteral("<br>")).replace(QChar::LineSeparator,QStringLiteral("<br>"));
    QString markdown;
    if(cursor.selectedText().contains(QChar::LineSeparator)){
        // Markdown export trims trailing soft breaks. Protect them before export
        // so even an empty next line survives a cell change or a reload.
        QTextDocument copy;QTextCursor(&copy).insertFragment(QTextDocumentFragment(cursor));
        QString marker="MDEDITCELLBREAK";
        while(copy.toPlainText().contains(marker))marker+='X';
        auto found=copy.find(QString(QChar::LineSeparator));
        while(!found.isNull()){
            found.insertText(marker);found=copy.find(QString(QChar::LineSeparator));
        }
        markdown=copy.toMarkdown().trimmed().replace(marker,QStringLiteral("<br>"));
    }else markdown=QTextDocumentFragment(cursor).toMarkdown().trimmed();
    for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next())
        for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(!format.isImageFormat()||!format.hasProperty(formulaMarkdown))continue;
            const QString name=format.toImageFormat().name();
            const QRegularExpression image("!\\[[^\\]]*\\]\\("+QRegularExpression::escape(name)+"[^)]*\\)");
            const auto match=image.match(markdown);
            if(match.hasMatch())markdown.replace(match.capturedStart(),match.capturedLength(),format.property(formulaMarkdown).toString());
        }
    return markdown;
}
QList<QStringList> tableExports(QTextTable *table){
    QList<QStringList> result;
    for(int r=0;r<table->rows();++r){QStringList row;for(int c=0;c<table->columns();++c)row.append(cellMarkdown(table->cellAt(r,c)));result.append(row);}
    return result;
}
}
bool LiveEditor::formatNativeSelection(const QString &left,const QString &right){
    if(!textCursor().currentTable()||!bindNativeTable(textCursor())||!nativeSelection())return false;
    auto cursor=textCursor();
    if(cursor.blockFormat().boolProperty(sourceCellProperty)){
        const QString selected=cursor.selectedText();
        cursor.insertText(left+selected+right);setTextCursor(cursor);return true;
    }
    QTextCharFormat format;
    if(left=="**"&&right=="**")format.setFontWeight(cursor.charFormat().fontWeight()>=QFont::Bold?QFont::Normal:QFont::Bold);
    else if(left=="*"&&right=="*")format.setFontItalic(!cursor.charFormat().fontItalic());
    else return false;
    cursor.mergeCharFormat(format);mergeCurrentCharFormat(format);return true;
}
bool LiveEditor::editSelectedCells(bool copy,bool clear){
    if(!cellRangeTable||!cellRangeDragging)return false;
    const auto node=tableNodeAt(source,cellRangeSource);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
    const int top=qMin(cellAnchorRow,cellEndRow),bottom=qMax(cellAnchorRow,cellEndRow);
    const int left=qMin(cellAnchorColumn,cellEndColumn),right=qMax(cellAnchorColumn,cellEndColumn);
    if(!data.error.isEmpty()||bottom>=data.rows.size())return false;
    QStringList lines;
    for(int r=top;r<=bottom;++r){
        if(right>=data.rows[r].size())return false;
        lines.append(data.rows[r].mid(left,right-left+1).join('\t'));
        if(clear)for(int c=left;c<=right;++c)data.rows[r][c].clear();
    }
    if(copy)QApplication::clipboard()->setText(lines.join('\n'));
    if(clear){
        const int scroll=verticalScrollBar()->value();
        const bool wholeTable=top==0&&left==0&&bottom==data.rows.size()-1&&right==data.rows.first().size()-1;
        source.replace(node->source.begin,node->source.end-node->source.begin,
                       wholeTable?QString():mdedit::writeMarkdownTable(data));
        cellRangeTable.clear();cellRangeDragging=cellRangePressed=false;
        rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);
    }
    return true;
}
bool LiveEditor::clipboardNativeCell(bool cut){
    if(cellRangeTable&&cellRangeDragging)return editSelectedCells(true,cut);
    if(axisSource>=0&&axisIndex>=0){
        const auto node=tableNodeAt(source,axisSource);if(!node||node->resource)return false;
        const auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
        if(!data.error.isEmpty())return false;QStringList lines;
        const int first=qMin(axisIndex,axisLast),last=qMax(axisIndex,axisLast);
        if(first<0)return false;
        if(axisRow){
            if(last>=data.rows.size())return false;
            for(int i=first;i<=last;++i)lines.append(data.rows[i].join('\t'));
        }else for(const auto &row:data.rows)lines.append(row.mid(first,last-first+1).join('\t'));
        QApplication::clipboard()->setText(lines.join('\n'));
        if(cut)removeSelectedAxes();
        return true;
    }
    if(!textCursor().currentTable()||!bindNativeTable(textCursor())||!nativeSelection())return false;
    auto cursor=textCursor();
    if(!cursor.hasSelection()){const auto cell=nativeTable->cellAt(cursor);cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);}
    QApplication::clipboard()->setText(cursor.selectedText().replace(QChar::ParagraphSeparator,'\n'));
    if(cut)cursor.removeSelectedText();return true;
}
bool LiveEditor::nativeSelection() const{
    if(!nativeTable)return false;
    auto cursor=textCursor();const auto first=nativeTable->cellAt(cursor.selectionStart()),last=nativeTable->cellAt(cursor.selectionEnd());
    return first.isValid()&&last.isValid()&&first.row()==last.row()&&first.column()==last.column();
}
bool LiveEditor::bindNativeTable(const QTextCursor &cursor){
    auto *table=cursor.currentTable();if(!table)return false;
    if(nativeTable==table){revealNativeCell(cursor);return true;}
    const int index=regionAt(table->firstPosition());if(index<0)return false;
    const auto node=tableNodeAt(source,regions[index].sourceStart);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));if(!data.error.isEmpty())return false;
    nativeTable=table;nativeStart=node->source.begin;nativeEnd=node->source.end;nativeData=data;nativeExports=tableExports(table);active=-1;
    revealNativeCell(cursor);
    return true;
}
void LiveEditor::revealNativeCell(const QTextCursor &cursor){
    if(!nativeTable||cursor.hasSelection())return;
    const auto next=nativeTable->cellAt(cursor);
    if(next.isValid()&&next.row()==sourceCellRow&&next.column()==sourceCellColumn)return;
    QScopedValueRollback<bool> guard(rebuilding,true);
    QTextCursor caret=cursor;
    const int index=regionAt(nativeTable->firstPosition());
    const int before=toPlainText().size();
    if(sourceCellRow>=0){
        const auto previous=nativeTable->cellAt(sourceCellRow,sourceCellColumn);
        if(previous.isValid()){
            auto out=previous.firstCursorPosition();out.setPosition(previous.lastPosition(),QTextCursor::KeepAnchor);
            const QString raw=nativeData.rows.value(sourceCellRow).value(sourceCellColumn);
            out.removeSelectedText();
            auto format=out.blockFormat();format.clearProperty(sourceCellProperty);out.setBlockFormat(format);
            const auto *rendered=renderedDocument(raw,false,true);
            for(auto it=rendered->begin().begin();!it.atEnd();++it){
                const auto fragment=it.fragment();const auto f=fragment.charFormat();
                if(f.isImageFormat()){
                    const QUrl url(f.toImageFormat().name());
                    document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                }
                out.insertText(fragment.text(),f);
            }
        }
    }
    sourceCellRow=sourceCellColumn=-1;
    if(next.isValid()){
        const QString raw=nativeData.rows.value(next.row()).value(next.column());
        if(!scanSpans(raw).isEmpty()||!inlineEditingStyles(raw).isEmpty()||!highlightRanges(raw).isEmpty()){
            const bool atEnd=cursor.position()==next.lastPosition();
            const int offset=cursor.position()-next.firstPosition();
            auto out=next.firstCursorPosition();out.setPosition(next.lastPosition(),QTextCursor::KeepAnchor);
            QTextCharFormat normal;normal.setFont(font());out.insertText(raw,normal);
            auto format=out.blockFormat();format.setProperty(sourceCellProperty,true);out.setBlockFormat(format);
            sourceCellRow=next.row();sourceCellColumn=next.column();
            caret=next.firstCursorPosition();caret.setPosition(atEnd?next.lastPosition():next.firstPosition()+qMin(offset,int(raw.size())));
        }
    }
    const int delta=toPlainText().size()-before;
    if(index>=0){
        regions[index].end+=delta;
        for(int i=index+1;i<regions.size();++i){regions[i].start+=delta;regions[i].end+=delta;}
    }
    nativeExports=tableExports(nativeTable);oldText=toPlainText();
    layoutNativeTable(nativeTable);setTextCursor(caret);
}

void LiveEditor::layoutNativeTable(QTextTable *table){
    // Keep short columns compact; cap wide columns evenly when space runs out.
    QList<qreal> preferred(table->columns(),80);
    const auto tableFormat=table->format();
    for(int row=0;row<table->rows();++row)for(int column=0;column<table->columns();++column){
        auto cell=table->cellAt(row,column);
        auto cellFormat=cell.format().toTableCellFormat();
        cellFormat.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        cellFormat.setTopPadding(4);cellFormat.setBottomPadding(4);
        if(cellFormat!=cell.format())cell.setFormat(cellFormat);
        auto cursor=cell.firstCursorPosition();cursor.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);
        QTextDocument measure;measure.setDefaultFont(font());measure.setDocumentMargin(0);
        auto option=measure.defaultTextOption();option.setWrapMode(QTextOption::NoWrap);measure.setDefaultTextOption(option);
        QTextCursor(&measure).insertFragment(QTextDocumentFragment(cursor));
        // IME preedit lives in QTextLayout, outside the committed document
        // fragment. Include it in measurement without publishing it as source.
        for(auto block=cell.lastCursorPosition().block();block.isValid()&&block.position()>=cell.firstPosition();block=block.previous()){
            const auto *layout=block.layout();
            if(layout->preeditAreaPosition()<0||layout->preeditAreaText().isEmpty())continue;
            QTextCursor composing(&measure);
            composing.setPosition(qBound(0,block.position()+layout->preeditAreaPosition()-cell.firstPosition(),measure.characterCount()-1));
            composing.insertText(layout->preeditAreaText());
        }
        preferred[column]=qMax(preferred[column],qCeil(measure.idealWidth())+2*tableFormat.cellPadding()+2*qMax(1.0,tableFormat.border())+2);
        for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next()){
            auto format=block.blockFormat();format.setAlignment(Qt::AlignLeft);format.setNonBreakableLines(false);
            // Symmetric cell padding provides breathing room around natural text lines.
            format.setTopMargin(0);format.setBottomMargin(0);
            format.setLineHeight(0,QTextBlockFormat::SingleHeight);
            if(format!=block.blockFormat())QTextCursor(block).setBlockFormat(format);
        }
    }
    const qreal available=qMax(40.0,viewport()->width()-2*document()->documentMargin()-4);
    qreal natural=0,largest=80;for(qreal value:preferred){natural+=value;largest=qMax(largest,value);}
    const qreal width=qMin(available,natural);
    qreal low=0,high=largest;
    for(int step=0;step<40;++step){
        const qreal cap=(low+high)/2; qreal total=0;
        for(qreal value:preferred)total+=qMin(value,cap);
        if(total>width)high=cap;else low=cap;
    }
    const qreal cap=(low+high)/2;
    auto format=table->format();format.setAlignment(Qt::AlignLeft);format.setWidth(QTextLength(QTextLength::FixedLength,width));
    QList<QTextLength> columns;
    for(int column=0;column<table->columns();++column)columns.append(QTextLength(QTextLength::PercentageLength,100.0*qMin(preferred[column],cap)/width));
    format.setColumnWidthConstraints(columns);
    if(format!=table->format())table->setFormat(format);
    auto option=table->document()->defaultTextOption();
    if(option.wrapMode()!=QTextOption::WrapAtWordBoundaryOrAnywhere){option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);table->document()->setDefaultTextOption(option);}
}

void LiveEditor::layoutNativeTables(){
    QScopedValueRollback<bool> guard(rebuilding,true);
    QSet<QTextTable *> seen;
    for(auto block=document()->begin();block.isValid();block=block.next()){
        auto *table=QTextCursor(block).currentTable();
        if(table&&!seen.contains(table)){seen.insert(table);layoutNativeTable(table);}
    }
}

bool LiveEditor::syncNativeTable(){
    if(!nativeTable||nativeStart<0)return false;
    const int index=regionAt(nativeTable->firstPosition());if(index<0)return false;
    const auto current=tableExports(nativeTable);
    if(current==nativeExports){oldText=toPlainText();return true;}
    QList<QStringList> rows;
    for(int r=0;r<current.size();++r){QStringList row;
        for(int c=0;c<current[r].size();++c){
            const bool unchanged=r<nativeExports.size()&&c<nativeExports[r].size()&&current[r][c]==nativeExports[r][c];
            row.append(unchanged&&r<nativeData.rows.size()?nativeData.rows[r].value(c):current[r][c]);
        }rows.append(row);
    }
    nativeData.rows=rows;const QString replacement=mdedit::writeMarkdownTable(nativeData);
    const int delta=replacement.size()-(nativeEnd-nativeStart),visualDelta=toPlainText().size()-oldText.size();
    source.replace(nativeStart,nativeEnd-nativeStart,replacement);nativeEnd+=delta;
    auto &region=regions[index];region.sourceEnd+=delta;region.end+=visualDelta;if(region.contentEnd>=0)region.contentEnd+=delta;
    for(int i=index+1;i<regions.size();++i){regions[i].sourceStart+=delta;regions[i].sourceEnd+=delta;if(regions[i].contentEnd>=0)regions[i].contentEnd+=delta;regions[i].start+=visualDelta;regions[i].end+=visualDelta;}
    {QScopedValueRollback<bool> guard(rebuilding,true);layoutNativeTable(nativeTable);}
    nativeExports=current;oldText=toPlainText();refreshLineNumbers();if(sourceChanged)sourceChanged(source);return true;
}
bool LiveEditor::changeMarkdownTableAt(int offset,int row,int column,const QString &operation){
    const auto node=tableNodeAt(source,offset);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
    if(!data.error.isEmpty()||row<0||row>=data.rows.size())return false;
    int columns=1;for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));
    if(column<0||column>=columns)return false;
    for(auto &cells:data.rows)while(cells.size()<columns)cells.append(QString());
    while(data.separators.size()<columns)data.separators.append("---");
    if(operation=="insertRowBefore"||operation=="insertRowAfter"){
        if(data.rows.size()>=10000)return false;
        QStringList cells;for(int c=0;c<columns;++c)cells.append(QString());
        data.rows.insert(row+(operation=="insertRowAfter"?1:0),cells);
    }else if(operation=="removeRow"){
        if(data.rows.size()<=1)return false;data.rows.removeAt(row);
    }else if(operation=="insertColumnBefore"||operation=="insertColumnAfter"){
        if(columns>=256)return false;const int at=column+(operation=="insertColumnAfter"?1:0);
        for(auto &cells:data.rows)cells.insert(at,QString());data.separators.insert(at,"---");
    }else if(operation=="removeColumn"){
        if(columns<=1)return false;for(auto &cells:data.rows)cells.removeAt(column);data.separators.removeAt(column);
    }else if(operation=="moveRowUp"||operation=="moveRowDown"){
        const int to=row+(operation=="moveRowUp"?-1:1);if(to<0||to>=data.rows.size())return false;data.rows.swapItemsAt(row,to);
    }else if(operation=="moveColumnLeft"||operation=="moveColumnRight"){
        const int to=column+(operation=="moveColumnLeft"?-1:1);if(to<0||to>=columns)return false;
        for(auto &cells:data.rows)cells.swapItemsAt(column,to);data.separators.swapItemsAt(column,to);
    }else return false;
    const int scroll=verticalScrollBar()->value();
    source.replace(node->source.begin,node->source.end-node->source.begin,mdedit::writeMarkdownTable(data));
    rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}
void LiveEditor::mouseDoubleClickEvent(QMouseEvent *event){
    const auto cursor=cursorForPosition(event->position().toPoint());
    const auto *table=cursor.currentTable();
    const int region=table?regionAt(table->firstPosition()):-1;
    const int offset=region>=0?regions[region].sourceStart:sourcePosition(cursor.position());
    const auto node=tableNodeAt(source,offset);
    if(event->button()==Qt::LeftButton&&node&&!node->resource){
        bindNativeTable(cursor);
        QScopedValueRollback<bool> guard(movingCursor,true);QTextBrowser::mouseDoubleClickEvent(event);event->accept();return;
    }
    QTextBrowser::mouseDoubleClickEvent(event);
}
void LiveEditor::contextMenuEvent(QContextMenuEvent *event){
    const auto cursor=cursorForPosition(event->pos());
    if(!textCursor().hasSelection())setTextCursor(cursor);
    const auto *table=cursor.currentTable();
    const int region=table?regionAt(table->firstPosition()):-1;
    const int offset=region>=0?regions[region].sourceStart:sourcePosition(cursor.position());
    const auto node=tableNodeAt(source,offset);
    if(node){
        auto *menu=new QMenu(this);menu->setObjectName("tableContextMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
        if(!node->resource){
            int row=0,column=0,rows=1,columns=1;
            if(const auto *table=cursor.currentTable()){
                const auto cell=table->cellAt(cursor);row=cell.row();column=cell.column();rows=table->rows();columns=table->columns();
            }else{
                const auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
                rows=data.rows.size();for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));
                const int line=source.mid(node->source.begin,qMax(0,offset-node->source.begin)).count('\n');
                row=qBound(0,line>0?line-1:0,qMax(0,rows-1));
                const auto rawLine=source.mid(node->source.begin,node->source.end-node->source.begin).section('\n',line,line);
                const int lineStart=offset-(offset==0?0:source.lastIndexOf('\n',offset-1)+1);
                column=qBound(0,rawLine.left(lineStart).count('|')-1,qMax(0,columns-1));
            }
            auto operation=[&](const QString &label,const QString &name,bool enabled=true){
                auto *action=menu->addAction(label,this,[this,offset,row,column,name]{changeMarkdownTableAt(offset,row,column,name);});
                action->setObjectName(name);action->setEnabled(enabled);
            };
            operation("在上方插入行","insertRowBefore");operation("在下方插入行","insertRowAfter");
            operation("在左侧插入列","insertColumnBefore");operation("在右侧插入列","insertColumnAfter");menu->addSeparator();
            if(cellRangeDragging&&cellRangeTable==table){
                auto selectedOperation=[&](const QString &label,const QString &name,bool rowAxis,int direction){
                    const int first=rowAxis?qMin(cellAnchorRow,cellEndRow):qMin(cellAnchorColumn,cellEndColumn);
                    const int last=rowAxis?qMax(cellAnchorRow,cellEndRow):qMax(cellAnchorColumn,cellEndColumn);
                    const int total=rowAxis?rows:columns;
                    auto *action=menu->addAction(label,this,[this,offset,first,last,rowAxis,direction]{
                        axisSource=offset;axisRow=rowAxis;axisIndex=first;axisLast=last;
                        cellRangeTable.clear();cellRangeDragging=cellRangePressed=false;
                        if(direction==0)removeSelectedAxes();
                        else{
                            const int boundary=direction<0?first-1:last+2;
                            if(reorderMarkdownAxisAt(offset,rowAxis,first,boundary,last-first+1)){
                                axisIndex=first+direction;axisLast=last+direction;
                            }
                        }
                        viewport()->update();
                    });
                    action->setObjectName(name);
                    action->setEnabled(direction==0||(direction<0?first>0:last+1<total));
                };
                selectedOperation("所选行上移","moveSelectedRowsUp",true,-1);
                selectedOperation("所选行下移","moveSelectedRowsDown",true,1);
                selectedOperation("所选列左移","moveSelectedColumnsLeft",false,-1);
                selectedOperation("所选列右移","moveSelectedColumnsRight",false,1);menu->addSeparator();
                selectedOperation("删除所选行","removeSelectedRows",true,0);
                selectedOperation("删除所选列","removeSelectedColumns",false,0);menu->addSeparator();
            }else{
                operation("当前行上移","moveRowUp",row>0);operation("当前行下移","moveRowDown",row+1<rows);
                operation("当前列左移","moveColumnLeft",column>0);operation("当前列右移","moveColumnRight",column+1<columns);menu->addSeparator();
                operation("删除当前行","removeRow",rows>1);operation("删除当前列","removeColumn",columns>1);menu->addSeparator();
            }
        }
        menu->addAction("表格块上移",this,[this,offset]{moveTableBlockAt(offset,-1);});
        menu->addAction("表格块下移",this,[this,offset]{moveTableBlockAt(offset,1);});menu->popup(event->globalPos());event->accept();return;
    }
    auto *menu=createStandardContextMenu();menu->setObjectName("editorContextMenu");
    for(auto *action:menu->actions()){
        const auto name=action->objectName();
        if(name=="edit-undo"||name=="edit-redo"||name=="select-all"){
            menu->removeAction(action);delete action;
        }
    }
    if(populateSelectionMenu)populateSelectionMenu(menu);
    menu->addSeparator();
    auto *insert=menu->addAction("插入 2×2 表格",this,[this,offset]{if(insertTableRequested)insertTableRequested(offset);});
    insert->setObjectName("insertTable");insert->setEnabled(bool(insertTableRequested));
    menu->setAttribute(Qt::WA_DeleteOnClose);menu->popup(event->globalPos());event->accept();
}

QList<LiveEditor::AxisControls> LiveEditor::axisControls() const{
    QList<AxisControls> result;QSet<QTextTable *> seen;
    const QPointF scroll(horizontalScrollBar()->value(),verticalScrollBar()->value());
    auto *layout=document()->documentLayout();
    for(auto block=document()->begin();block.isValid();block=block.next()){
        auto *table=QTextCursor(block).currentTable();if(!table||seen.contains(table))continue;seen.insert(table);
        const int region=regionAt(table->firstPosition());if(region<0)continue;
        AxisControls item;item.table=table;item.start=regions[region].sourceStart;
        const auto format=table->format();
        auto cellBounds=[&](const QTextTableCell &cell){
            QRectF rect;
            for(auto block=cell.firstCursorPosition().block();block.isValid()&&block.position()<=cell.lastPosition();block=block.next()){
                auto bounds=layout->blockBoundingRect(block);
                // blockBoundingRect measures the glyph layout. Fixed line
                // spacing also occupies space below it inside the cell.
                const auto paragraph=block.blockFormat();
                if(paragraph.lineHeightType()==QTextBlockFormat::FixedHeight)
                    bounds.setHeight(qMax(bounds.height(),paragraph.lineHeight()*qMax(1,block.layout()->lineCount())));
                rect=rect.isNull()?bounds:rect.united(bounds);
            }
            const auto cellFormat=cell.format().toTableCellFormat();
            auto padding=[&](int property,qreal value){return cellFormat.hasProperty(property)?value:format.cellPadding();};
            const qreal border=format.borderCollapse()?qMax(1.0,format.border())/2:format.border();
            rect.adjust(-padding(QTextFormat::TableCellLeftPadding,cellFormat.leftPadding())-border,
                        -padding(QTextFormat::TableCellTopPadding,cellFormat.topPadding())-border,
                         padding(QTextFormat::TableCellRightPadding,cellFormat.rightPadding())+border,
                         padding(QTextFormat::TableCellBottomPadding,cellFormat.bottomPadding())+border);
            return rect.translated(-scroll);
        };
        for(int r=0;r<table->rows();++r){
            QRectF rect;
            for(int c=0;c<table->columns();++c){const auto bounds=cellBounds(table->cellAt(r,c));rect=rect.isNull()?bounds:rect.united(bounds);}
            item.rows.append(rect);item.frame=item.frame.isNull()?rect:item.frame.united(rect);
        }
        if(item.frame.bottom()<0||item.frame.top()>viewport()->height())continue;
        for(int c=0;c<table->columns();++c){
            const auto rect=cellBounds(table->cellAt(0,c));
            item.columns.append(QRectF(rect.left(),item.frame.top(),rect.width(),item.frame.height()));
        }
        for(auto &rect:item.rows){rect.setLeft(item.frame.left());rect.setRight(item.frame.right());}
        item.addRow=QRectF(item.frame.left(),item.frame.bottom()+3,item.frame.width(),18);
        item.addColumn=QRectF(item.frame.right()+3,item.frame.top(),18,item.frame.height());result.append(item);
    }return result;
}
void LiveEditor::updateAxisDrop(){
    if(source!=axisSnapshot){cancelAxisDrag();return;}
    for(const auto &item:axisControls())if(item.start==axisSource){
        const auto &rects=axisRow?item.rows:item.columns;qreal nearest=std::numeric_limits<qreal>::max();axisBoundary=-1;
        for(int i=0;i<=rects.size();++i){const qreal position=i<rects.size()?(axisRow?rects[i].top():rects[i].left()):(axisRow?rects.last().bottom():rects.last().right());
            const qreal distance=qAbs((axisRow?axisPoint.y():axisPoint.x())-position);
            if(distance<nearest){nearest=distance;axisBoundary=i;axisDropPosition=position;}}
        return;
    }
}
bool LiveEditor::removeSelectedAxes(){
    const auto node=tableNodeAt(source,axisSource);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));
    if(!data.error.isEmpty())return false;
    const int first=qMin(axisIndex,axisLast),last=qMax(axisIndex,axisLast);
    const int total=axisRow?data.rows.size():data.rows.first().size();
    if(first<0||last>=total)return false;
    if(axisRow)for(int i=last;i>=first;--i)data.rows.removeAt(i);
    else{
        for(auto &cells:data.rows)for(int i=last;i>=first;--i)cells.removeAt(i);
        for(int i=last;i>=first;--i)if(i<data.separators.size())data.separators.removeAt(i);
    }
    const QString replacement=last-first+1==total?QString():mdedit::writeMarkdownTable(data);
    const int scroll=verticalScrollBar()->value();
    source.replace(node->source.begin,node->source.end-node->source.begin,replacement);
    axisSource=axisIndex=axisLast=-1;
    rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}
void LiveEditor::cancelAxisDrag(){axisPressed=axisStarted=false;axisBoundary=-1;axisDragScroll.stop();viewport()->unsetCursor();viewport()->update();}
bool LiveEditor::reorderMarkdownAxisAt(int offset,bool row,int from,int boundary,int selectedCount){
    const auto node=tableNodeAt(source,offset);if(!node||node->resource)return false;
    auto data=mdedit::readMarkdownTable(source.mid(node->source.begin,node->source.end-node->source.begin));if(!data.error.isEmpty())return false;
    int columns=1;for(const auto &cells:data.rows)columns=qMax(columns,int(cells.size()));const int count=row?data.rows.size():columns;
    if(from<0||selectedCount<1||from+selectedCount>count||boundary<0||boundary>count||
        (boundary>=from&&boundary<=from+selectedCount))return false;
    const int to=boundary>from?boundary-selectedCount:boundary;
    auto moveRange=[&](auto &items){
        const auto selected=items.mid(from,selectedCount);
        items.remove(from,selectedCount);
        for(int i=0;i<selectedCount;++i)items.insert(to+i,selected[i]);
    };
    if(row)moveRange(data.rows);
    else{for(auto &cells:data.rows){while(cells.size()<columns)cells.append(QString());moveRange(cells);}
        while(data.separators.size()<columns)data.separators.append("---");moveRange(data.separators);}
    const int scroll=verticalScrollBar()->value();source.replace(node->source.begin,node->source.end-node->source.begin,mdedit::writeMarkdownTable(data));
    rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}
bool LiveEditor::moveMarkdownTableTo(int offset,int boundary){
    const auto nodes=mdedit::MarkdownParser::parse(source);int from=-1;
    for(int i=0;i<nodes.size();++i)if(nodes[i].kind==mdedit::BlockKind::Table&&offset>=nodes[i].source.begin&&offset<=nodes[i].source.end){from=i;break;}
    if(from<0||boundary<0||boundary>nodes.size()||boundary==from||boundary==from+1)return false;
    QStringList blocks,gaps;
    for(int i=0;i<nodes.size();++i){blocks.append(source.mid(nodes[i].source.begin,nodes[i].source.end-nodes[i].source.begin));
        if(i+1<nodes.size())gaps.append(source.mid(nodes[i].source.end,nodes[i+1].source.begin-nodes[i].source.end));}
    const int to=boundary>from?boundary-1:boundary;const QString table=blocks.takeAt(from);blocks.insert(to,table);
    QList<int> order;for(int i=0;i<nodes.size();++i)order.append(i);order.move(from,to);
    QString result=source.left(nodes.first().source.begin);
    for(int i=0;i<blocks.size();++i){result+=blocks[i];if(i<gaps.size())result+=(order[i+1]==order[i]+1||gaps[i].contains("\n\n"))?gaps[i]:QString("\n\n");}
    result+=source.mid(nodes.last().source.end);const int scroll=verticalScrollBar()->value();
    source=result;rebuild(-1,0);verticalScrollBar()->setValue(scroll);if(sourceChanged)sourceChanged(source);return true;
}

