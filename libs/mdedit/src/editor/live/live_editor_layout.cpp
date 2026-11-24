#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>
#include <utility>

void LiveEditor::rebuild(int chosen,int offset,int forcedTarget){
        initialRenderTimer.stop();
        const int openingLimit=std::exchange(initialRenderLimit,0);
        nativeTable.clear();nativeStart=nativeEnd=-1;sourceCellRow=sourceCellColumn=-1;
        rebuilding=true;int scroll=verticalScrollBar()->value();
        // Clearing/repopulating the document temporarily collapses the scroll
        // range. Publish only the final layout and restored viewport together.
        // Preserve this widget's explicit state, not a temporary disable inherited
        // from the stacked pages during a source/live mode transition.
        const bool updatesWereEnabled=!testAttribute(Qt::WA_ForceUpdatesDisabled);
        setUpdatesEnabled(false);
        const auto restoreUpdates=qScopeGuard([this,updatesWereEnabled]{setUpdatesEnabled(updatesWereEnabled);});
        // Intermediate document mutations must not repeatedly update the gutter.
        const QSignalBlocker blockEditor(this);
        const bool wasSourceExposed=sourceExposed;
        sourceExposed=false;
        selectionSourceExpanded=false;expandedSelectionPieces.clear();
        const int caretY=cursorRect().top();
        const bool wasEditingResource=isResourceRegion(active);
        const bool wasEditingMath=mathEditSlotHeight>0&&active>=0;
        int candidate=chosen;
        if(forcedTarget>=0)for(int i=0;i<regions.size();++i)
            if(forcedTarget>=regions[i].sourceStart&&forcedTarget<regions[i].sourceEnd){candidate=i;break;}
        qreal previousMathSlot=0;
        if(!wasSourceExposed&&candidate>=0&&candidate<regions.size()){
            const auto &region=regions[candidate];
            bool displayMath=false;
            for(const auto &span:scanSpans(source.mid(region.sourceStart,region.sourceEnd-region.sourceStart)))
                if(span.kind==SpanKind::DisplayMath)displayMath=true;
            if(candidate==active&&mathEditSourceStart==region.sourceStart)
                previousMathSlot=mathEditSlotHeight;
            else if(displayMath){
                auto *layout=document()->documentLayout();
                const auto first=document()->findBlock(region.start);
                const auto last=document()->findBlock(region.end);
                const qreal bottom=last.next().isValid()?layout->blockBoundingRect(last.next()).top():document()->size().height()-document()->documentMargin();
                previousMathSlot=bottom-(first.previous().isValid()?layout->blockBoundingRect(first.previous()).top():document()->documentMargin());
            }
        }
        qreal previousResourceSlot=0;
        if(isResourceRegion(candidate)){
            const auto block=document()->findBlock(regions[candidate].start);
            const qreal top=document()->documentLayout()->blockBoundingRect(block).top();
            const auto last=document()->findBlock(regions[candidate].end);
            previousResourceSlot=(last.next().isValid()?document()->documentLayout()->blockBoundingRect(last.next()).top():document()->size().height()-document()->documentMargin())-top;
        }
        const int topRegion=regionAt(cursorForPosition(QPoint(0,0)).position());
        const int topSource=topRegion>=0?regions[topRegion].sourceStart:-1;
        const qreal topOffset=topRegion>=0?document()->documentLayout()->blockBoundingRect(document()->findBlock(regions[topRegion].start)).top()-scroll:0;
        const int preferredX=movingVertically?textCursor().verticalMovementX():-1;
        if(renderedFont!=font()||renderedBaseUrl!=document()->baseUrl()){
            renderedMarkdown.clear();
            renderedFont=font();
            renderedBaseUrl=document()->baseUrl();
        }
        // Give each source blank line its own editable block. Keep fenced
        // code and display formulas intact for their respective renderers.
        // Build real block geometry before scrolling. Estimated placeholders
        // change both the document height and visible content when replaced.
        const auto spans=scanSpans(source);
        QList<QPair<int,int>> units;
        int begin=0;
        static const QRegularExpression blankLine("(?:^|(?<=\\n))[ \\t]*\\n");
        auto matches=blankLine.globalMatch(source);
        auto span=spans.cbegin();
        while(matches.hasNext()){
            const auto match=matches.next();
            const int start=match.capturedStart();
            while(span!=spans.cend()&&span->start+span->length<=start)++span;
            if(span!=spans.cend()&&span->start<=start)continue;
            if(begin<start)units.append({begin,start});
            units.append({start,match.capturedEnd()});
            begin=match.capturedEnd();
        }
        if(begin<source.size())units.append({begin,int(source.size())});
        if(source.isEmpty()||source.endsWith('\n'))units.append({int(source.size()),int(source.size())});
        if(openingLimit>0){
            // Never truncate a fence, formula, table or paragraph midway.
            qsizetype count=0;
            while(count<units.size()){
                const int end=units[count++].second;
                if(end>=openingLimit)break;
            }
            units=units.mid(0,count);
        }
        // Split text and headings when each source line can render independently.
        struct ListItem { QString listSource; int blockNumber; };
        QHash<int,ListItem> listItems;
        QList<QPair<int,int>> lineUnits;
        QSet<int> continuations;
        QSet<int> codeUnits;
        QHash<int,int> fenceOffsets;
        QList<QPair<int,int>> fenceUnits;
        for(const auto &code:spans){
            if(code.kind==SpanKind::DisplayMath){
                const int end=code.start+code.length;
                fenceUnits.append({code.start,end<source.size()&&source[end]=='\n'?end+1:end});
                continue;
            }
            if(!code.fenced)continue;
            const int start=code.start==0?0:source.lastIndexOf('\n',code.start-1)+1;
            const int end=code.start+code.length;
            fenceUnits.append({start,end<source.size()?end+1:end});
            codeUnits.insert(start);
            fenceOffsets.insert(start,code.start-start);
        }
        QList<QPair<int,int>> splitUnits;
        auto fence=fenceUnits.cbegin();
        for(const auto &unit:units){
            int start=unit.first;
            for(;fence!=fenceUnits.cend()&&fence->first<unit.second;++fence){
                if(start<fence->first)splitUnits.append({start,fence->first});
                splitUnits.append(*fence);
                start=fence->second;
            }
            if(start<unit.second||unit.first==unit.second)splitUnits.append({start,unit.second});
        }
        for(qsizetype index=0;index<splitUnits.size();++index){
            auto unit=splitUnits[index];
            QString text=source.mid(unit.first,unit.second-unit.first);
            if(text.endsWith('\n'))text.chop(1);
            if(!codeUnits.contains(unit.first)&&(text.startsWith("    ")||text.startsWith('\t')||
                (text.startsWith(' ')&&text.left(4).contains('\t')))){
                const auto *rendered=renderedDocument(text,false);
                auto block=rendered->begin();
                int end=0;
                while(block.isValid()&&isCodeBlock(block.blockFormat())){
                    const int newline=text.indexOf('\n',end);
                    end=newline<0?int(text.size()):newline+1;
                    block=block.next();
                }
                if(end>0){
                    codeUnits.insert(unit.first);
                    if(end<text.size()){
                        splitUnits.insert(index+1,{unit.first+end,unit.second});
                        unit.second=unit.first+end;
                        text.truncate(end-1);
                    }
                }
            }
            // A tight table and its neighbouring paragraph need separate edit
            // regions even after Backspace removes their separating blank line.
            if(!codeUnits.contains(unit.first)){
                const auto rows=text.split('\n');
                int start=unit.first;
                for(int row=0;row+1<rows.size();++row){
                    if(rows[row].contains('|')&&
                       mdedit::readMarkdownTable(rows[row]+"\n"+rows[row+1]).error.isEmpty()){
                        int end=start+rows[row].size()+1+rows[row+1].size();
                        int next=row+2;
                        while(next<rows.size()&&rows[next].contains('|'))end+=1+rows[next++].size();
                        if(end<unit.second&&source[end]=='\n')++end;
                        QList<QPair<int,int>> parts;
                        if(unit.first<start)parts.append({unit.first,start});
                        parts.append({start,end});
                        if(end<unit.second)parts.append({end,unit.second});
                        if(parts.size()>1){
                            for(int part=parts.size()-1;part>0;--part)splitUnits.insert(index+1,parts[part]);
                            unit=parts.first();
                            text=source.mid(unit.first,unit.second-unit.first);
                            if(text.endsWith('\n'))text.chop(1);
                        }
                        break;
                    }
                    start+=rows[row].size()+1;
                }
            }
            // Rules adjacent to other blocks must use the same one-block
            // path as isolated rules. Qt fragments add an empty paragraph.
            if(!codeUnits.contains(unit.first)){
                const auto nodes=mdedit::MarkdownParser::parse(text);
                int ruleCount=0;
                for(const auto &node:nodes)if(node.kind==mdedit::BlockKind::Rule)++ruleCount;
                if(ruleCount&&nodes.size()>1){
                    const auto *view=renderedDocument(text,false);
                    int renderedRules=0;
                    for(auto block=view->begin();block.isValid();block=block.next())
                        if(block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))++renderedRules;
                    // Keep contextual syntax consistent with the shared renderer.
                    if(renderedRules==ruleCount){
                        QList<QPair<int,int>> parts;
                        int precedingStart=0;
                        for(const auto &node:nodes){
                            if(node.kind!=mdedit::BlockKind::Rule)continue;
                            if(precedingStart<node.source.begin)parts.append({unit.first+precedingStart,unit.first+node.source.begin});
                            const int end=qMin(int(text.size()),node.source.end+1);
                            parts.append({unit.first+node.source.begin,unit.first+end});
                            precedingStart=end;
                        }
                        if(precedingStart<text.size())parts.append({unit.first+precedingStart,unit.second});
                        else if(!parts.isEmpty())parts.last().second=unit.second;
                        if(parts.size()>1){
                            for(int part=parts.size()-1;part>0;--part)splitUnits.insert(index+1,parts[part]);
                            unit=parts.first();
                            text=source.mid(unit.first,unit.second-unit.first);
                            if(text.endsWith('\n'))text.chop(1);
                        }
                    }
                }
            }
            const auto lines=text.split('\n');
            if(codeUnits.contains(unit.first)||(lines.size()<2&&!listMarker().match(text).hasMatch())||std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
                return span.start>=unit.first&&span.start<unit.second&&
                    (span.kind==SpanKind::DisplayMath||QStringView(source).mid(span.start,span.length).contains('\n'));
            })){
                lineUnits.append(unit);
                continue;
            }
            const auto *rendered=renderedDocument(text,false);
            {
                const auto previousUnitCount=lineUnits.size();
                QList<QPair<int,int>> items;
                QHash<int,ListItem> renderedItems;
                int line=0;
                int start=unit.first;
                for(auto block=rendered->begin();block.isValid();block=block.next()){
                    if(line>=lines.size())break;
                    if(block.textList()){
                        if(!listMarker().match(lines[line]).hasMatch())break;
                        renderedItems.insert(start,{text,block.blockNumber()});
                    }else if(!isTextBlock(block))break;
                    const int next=line+block.text().count(QChar::LineSeparator)+1;
                    if(next>lines.size())break;
                    int end=start;
                    for(;line<next;++line)end+=lines[line].size()+1;
                    items.append({start,qMin(end,unit.second)});
                    start=end;
                    if(!block.next().isValid()&&line==lines.size()&&!renderedItems.isEmpty()){
                        listItems.insert(renderedItems);
                        lineUnits.append(items);
                    }
                }
                if(lineUnits.size()>previousUnitCount)continue;
            }
            bool independent=true;
            for(auto block=rendered->begin();block.isValid();block=block.next()){
                if(!isTextBlock(block)){independent=false;break;}
            }
            if(!independent){
                lineUnits.append(unit);
                continue;
            }
            QStringList renderedLines;
            QList<bool> headings;
            for(const auto &line:lines){
                const auto *renderedLine=renderedDocument(line,false);
                renderedLines.append(renderedLine->toPlainText());
                headings.append(renderedLine->begin().blockFormat().headingLevel()>0);
            }
            if(renderedLines.join('\n')!=rendered->toPlainText()){
                lineUnits.append(unit);
                continue;
            }
            int start=unit.first;
            for(qsizetype i=0;i<lines.size();++i){
                const int end=i+1==lines.size()?unit.second:start+lines[i].size()+1;
                lineUnits.append({start,end});
                if(i+1<lines.size()&&!headings[i]&&!headings[i+1])continuations.insert(end);
                start=end;
            }
        }
        units.swap(lineUnits);
        // Retain the working document's fragments across caret changes. A
        // fixed 128-entry LRU thrashes when a document has more than 128 units.
        renderedMarkdown.setMaxCost(qBound(128,int(units.size())*4,8192));
        // Recover chosen unit by source position after edits changed the boundaries.
        int target=forcedTarget>=0?forcedTarget:(chosen>=0&&chosen<regions.size()?regions[chosen].sourceStart+offset:-1);
        // Defer layout until all fragments and formats have been installed.
        // Otherwise each insertion lays out the growing document again.
        const bool layoutEnabled=document()->isLayoutEnabled();
        // QTextEdit listens to documentSizeChanged even while widget painting
        // is disabled. Clearing the document would collapse the scroll ranges,
        // hide transient scrollbars and resize the viewport during every click.
        // Publish geometry only after the replacement document is laid out.
        QSignalBlocker layoutSignals(document()->documentLayout());
        document()->setLayoutEnabled(false);
        mathPreviewVisible=false;
        active=-1;regions.clear();document()->clear();QTextCursor out(document());
        for(qsizetype unitIndex=0;unitIndex<units.size();++unitIndex){
            const auto &unit=units[unitIndex];
            int i=regions.size();bool editing=target>=unit.first&&(target<unit.second||unitIndex==units.size()-1)&&active<0;
            if(i)out.insertBlock(QTextBlockFormat(),QTextCharFormat());int from=out.position();
            QString text=source.mid(unit.first,unit.second-unit.first);
            // The boundary between units already inserts one newline.
            if(text.endsWith('\n'))text.chop(1);
            const auto resourceNodes=mdedit::MarkdownParser::parse(text);
            if(resourceNodes.size()==1&&resourceNodes.first().resource&&resourceSourceEditStart!=unit.first)editing=false;
            const int contentEnd=unit.first+text.size();
            // Native tables stay as QTextTable objects, including when the
            // caret is inside them. Their edits are serialized into the buffer.
            if(editing&&mdedit::readMarkdownTable(text).error.isEmpty()){
                const auto *view=renderedDocument(text,false);
                for(auto block=view->begin();block.isValid();block=block.next())
                    if(QTextCursor(block).currentTable()){editing=false;break;}
            }
            if(codeUnits.contains(unit.first)){
                const auto body=codeBody(text,fenceOffsets.value(unit.first,-1));
                out.setBlockFormat(codeBlockFormat());
                const auto characters=codeCharacters();
                out.setBlockCharFormat(characters);
                out.insertText(text,characters);
                if(!editing&&body.start>0){
                    // Keep fence rows and their layout, hiding only their ink.
                    // The same source offsets then work in both editor states.
                    auto hide=[&](int start,int end){
                        QTextCursor fence(document());fence.setPosition(from+start);
                        fence.setPosition(from+end,QTextCursor::KeepAnchor);
                        QTextCharFormat invisible;invisible.setForeground(Qt::transparent);
                        fence.mergeCharFormat(invisible);
                    };
                    hide(0,body.start);
                    hide(body.end,int(text.size()));
                }
                colorCode(text.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
                    QTextCursor token(document());
                    token.setPosition(from+body.start+start);
                    token.setPosition(token.position()+length,QTextCursor::KeepAnchor);
                    QTextCharFormat format;format.setForeground(mdedit::syntaxColor(theme,color));token.mergeCharFormat(format);
                });
                if(editing)active=i;
                regions.append({unit.first,unit.second,from,out.position(),contentEnd,false,false,false,{},true,fenceOffsets.value(unit.first,-1)});
                continue;
            }
            const bool continuesBefore=continuations.contains(unit.first);
            const bool continuesAfter=continuations.contains(unit.second)&&unit.first!=unit.second;
            const auto item=listItems.constFind(unit.first);
            std::optional<QTextBlockFormat> listItemFormat;
            int bodyOffset=0;
            auto pieces=inlinePieces(text);
            if(item!=listItems.cend()&&!text.contains('\n')){
                bodyOffset=listMarker().match(text).capturedLength();
                const QString body=text.mid(bodyOffset);
                pieces=inlinePieces(body);
                // Plain list text is also an independent editing region.
                // Keep complex Markdown on the parser's existing fallback.
                if(pieces.isEmpty()&&renderedDocument(body,false,true)->toPlainText()==body)
                    pieces.append({int(body.size()),false});
            }
            if(!pieces.isEmpty()){
                const bool editingPrefix=editing&&bodyOffset>0&&target<unit.first+bodyOffset;
                QTextDocument body;body.setMarkdown("Body");
                auto format=body.begin().blockFormat();
                if(item!=listItems.cend()){
                    const auto *rendered=renderedDocument(item->listSource,false);
                    const auto block=rendered->findBlockByNumber(item->blockNumber);
                    format=block.blockFormat();
                    format.setObjectIndex(-1);
                    out.setBlockFormat(format);
                    auto listFormat=block.textList()->format();
                    listFormat.setStart(listFormat.start()+block.textList()->itemNumber(block));
                    out.createList(listFormat);
                    format=out.blockFormat();
                    if(editingPrefix){
                        // Replace the painted bullet with its editable source,
                        // keeping the list body at the same horizontal position.
                        format.setObjectIndex(-1);
                        format.clearProperty(QTextFormat::BlockMarker);
                        format.setLeftMargin(format.leftMargin()+listFormat.indent()*document()->indentWidth());
                        format.setTextIndent(-QFontMetricsF(font()).horizontalAdvance(text.left(bodyOffset)));
                    }
                }
                setBodyLineHeight(format,font());
                if(continuesBefore)format.setTopMargin(0);
                if(continuesAfter)format.setBottomMargin(0);
                out.setBlockFormat(format);
                QTextCharFormat normal;normal.setFont(font());
                if(editingPrefix){
                    active=regions.size();
                    const int start=out.position();
                    out.insertText(text.left(bodyOffset),normal);
                    regions.append({unit.first,unit.first+bodyOffset,start,out.position(),unit.first+bodyOffset,true});
                    regions.last().listItem=true;
                }
                int pieceStart=bodyOffset;
                for(const auto &piece:pieces){
                    const int end=bodyOffset+piece.first;
                    const int start=out.position();
                    const bool selected=editing&&!editingPrefix&&target>=unit.first+pieceStart&&
                        (target<unit.first+end||end==text.size());
                    const QString raw=text.mid(pieceStart,end-pieceStart);
                    if(selected||!piece.second){
                        if(selected)active=regions.size();
                        QString displayed=raw;
                        displayed.replace('\n',QChar::LineSeparator);
                        out.insertText(displayed,normal);
                    }else{
                        const auto *rendered=renderedDocument(raw,false,true);
                        for(auto it=rendered->begin().begin();!it.atEnd();++it){
                            const auto fragment=it.fragment();
                            auto characters=fragment.charFormat();
                            if(characters.isImageFormat()){
                                const QUrl url(characters.toImageFormat().name());
                                document()->addResource(QTextDocument::ImageResource,url,
                                    rendered->resource(QTextDocument::ImageResource,url));
                            }
                            out.insertText(fragment.text(),characters);
                        }
                    }
                    regions.append({unit.first+pieceStart,end==text.size()?unit.second:unit.first+end,
                        start,out.position(),unit.first+end,true});
                    regions.last().listItem=item!=listItems.cend();
                    pieceStart=end;
                }
                continue;
            }
            if(editing){
                active=i;QTextCharFormat f;f.setFont(font());out.setBlockFormat(QTextBlockFormat());out.insertText(text,f);
                if(item!=listItems.cend()){
                    const auto *rendered=renderedDocument(item->listSource,false);
                    const auto block=rendered->findBlockByNumber(item->blockNumber);
                    listItemFormat=block.blockFormat();
                    listItemFormat->setObjectIndex(-1);
                    listItemFormat->clearProperty(QTextFormat::BlockMarker);
                }
            }else if(resourceNodes.size()==1&&resourceNodes.first().kind==mdedit::BlockKind::Rule){
                // Importing Qt's Markdown fragment adds empty paragraphs around
                // <hr>. A rule owns exactly one source line and one live block.
                QTextBlockFormat format;
                setBodyLineHeight(format,font());
                format.setTopMargin(0);format.setBottomMargin(0);
                format.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth,
                    QVariant::fromValue(QTextLength(QTextLength::PercentageLength,100)));
                out.setBlockFormat(format);
            }else if(text.trimmed().isEmpty()){
                QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
                auto format=bodyStyle.begin().blockFormat();setBodyLineHeight(format,font());
                out.setBlockFormat(format);
                QTextCharFormat f;f.setFont(font());out.setBlockCharFormat(f);
            }else if(item!=listItems.cend()){
                const auto *rendered=renderedDocument(item->listSource,false);
                const auto block=rendered->findBlockByNumber(item->blockNumber);
                auto blockFormat=block.blockFormat();
                blockFormat.setObjectIndex(-1);
                out.setBlockFormat(blockFormat);
                auto listFormat=block.textList()->format();
                listFormat.setStart(listFormat.start()+block.textList()->itemNumber(block));
                out.createList(listFormat);
                for(auto it=block.begin();!it.atEnd();++it){
                    const auto fragment=it.fragment();
                    const auto format=fragment.charFormat();
                    if(format.isImageFormat()){
                        const QUrl url(format.toImageFormat().name());
                        document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                    }
                    out.insertText(fragment.text(),format);
                }
            }else{
                const auto *rendered=renderedDocument(text,isMultilineParagraph(text));
                for(auto block=rendered->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
                    auto format=it.fragment().charFormat();if(format.isImageFormat()){
                        QUrl url(format.toImageFormat().name());document()->addResource(QTextDocument::ImageResource,url,rendered->resource(QTextDocument::ImageResource,url));
                    }
                }
                out.insertFragment(QTextDocumentFragment(rendered));
                // Qt can leave the empty destination block before a list.
                const auto destination=document()->findBlock(from);
                const auto firstItem=destination.next();
                if(rendered->begin().textList()&&destination.text().isEmpty()&&
                   !destination.textList()&&firstItem.isValid()&&firstItem.textList()){
                    const auto itemFormat=firstItem.blockFormat();
                    QTextCursor start(firstItem);
                    start.deletePreviousChar();
                    start.setBlockFormat(itemFormat);
                }
                // Qt can keep the destination block format for the first
                // paragraph of a fragment inserted into an empty block.
                QTextCursor first(document()->findBlock(from));
                auto format=rendered->begin().blockFormat();
                if(continuesBefore)format.setTopMargin(0);
                if(continuesAfter)format.setBottomMargin(0);
                if(format.objectIndex()<0&&first.blockFormat().objectIndex()<0)
                    first.setBlockFormat(format);
            }
            regions.append({unit.first,unit.second,from,out.position(),contentEnd,false,continuesBefore,continuesAfter,listItemFormat});
        }
        layoutNativeTables();
        mathEditSlotHeight=active>=0?previousMathSlot:0;
        mathEditSourceStart=mathEditSlotHeight>0?regions[active].sourceStart:-1;
        styleActiveSource();
        document()->setLayoutEnabled(layoutEnabled);
        preserveMathSlot();
        const bool editingResource=isResourceRegion(active);
        resourceEditSlotHeight=editingResource?previousResourceSlot:0;
        preserveResourceSlot();
        layoutSignals.unblock();
        if(layoutEnabled){
            auto *layout=document()->documentLayout();
            emit layout->documentSizeChanged(layout->documentSize());
            emit layout->update(QRectF(0,0,document()->size().width(),document()->size().height()));
        }
        if(target>=0&&active>=0){QTextCursor caret(document());caret.setPosition(regions[active].start+qBound(0,target-regions[active].sourceStart,regions[active].end-regions[active].start));caret.setVerticalMovementX(preferredX);setTextCursor(caret);}
        if(target>=0&&active<0)for(const auto &region:regions){
            if(target>=region.sourceStart&&target<region.sourceEnd){
                QTextCursor caret(document());caret.setPosition(target>=region.contentEnd?region.end:region.start);
                for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next()){
                    auto *table=QTextCursor(block).currentTable();if(!table)continue;
                    const QString prefix=source.mid(region.sourceStart,qMax(0,target-region.sourceStart));
                    const int line=prefix.count('\n');const int row=qBound(0,line>0?line-1:0,table->rows()-1);
                    const QString rawLine=prefix.section('\n',-1);int pipes=0;bool escape=false;
                    for(QChar ch:rawLine){if(ch=='|'&&!escape)++pipes;if(ch=='\\')escape=!escape;else escape=false;}
                    const int column=qBound(0,pipes-1,table->columns()-1);
                    caret=table->cellAt(row,column).firstCursorPosition();break;
                }
                setTextCursor(caret);break;
            }
        }
        oldText=toPlainText();verticalScrollBar()->setValue(scroll);
        if(mouseActivating&&(previousMathSlot>0||wasEditingMath)){
            // Mouse activation must preserve the viewport, not the previous
            // caret's screen Y (which may belong to a distant paragraph).
            verticalScrollBar()->setValue(scroll);
        }else if((backgroundRefresh||target<0||wasEditingResource||editingResource)&&topSource>=0){
            for(const auto &region:regions)if(region.sourceStart==topSource){
                const qreal top=document()->documentLayout()->blockBoundingRect(document()->findBlock(region.start)).top();
                verticalScrollBar()->setValue(qRound(top-topOffset));break;
            }
        }else if(forcedTarget>=0)ensureCursorVisible();
        else if(target>=0)verticalScrollBar()->setValue(scroll+cursorRect().top()-caretY);
        rebuilding=false;refreshLineNumbers();resourceWindowTimer.start();
    }

