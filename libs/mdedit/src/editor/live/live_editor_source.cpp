#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>

QPair<int,int> LiveEditor::sourceSelection()const{
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1)
            return caret.anchor()<caret.position()?qMakePair(0,int(source.size())):qMakePair(int(source.size()),0);
        return {sourcePosition(caret.anchor()),sourcePosition(caret.position())};
    }

void LiveEditor::restoreSourceSelection(int anchor,int position){
        if(resourceSourceEditStart>=0){
            bool inside=false;
            for(const auto &node:mdedit::MarkdownParser::parse(source))
                if(node.resource&&node.source.begin==resourceSourceEditStart&&anchor>=node.source.begin&&anchor<=node.source.end)inside=true;
            if(!inside)resourceSourceEditStart=-1;
        }
        anchor=qBound(0,anchor,int(source.size()));position=qBound(0,position,int(source.size()));
        rebuild(-1,0,anchor==position?position:qMin(anchor,position));
        if(anchor!=position){
            // Map source endpoints into the rebuilt live document before
            // exposing only the previews touched by the restored selection.
            auto visiblePosition=[&](int sourceOffset){
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
            QTextCursor selected(document());selected.setPosition(visiblePosition(anchor));
            selected.setPosition(visiblePosition(position),QTextCursor::KeepAnchor);
            setTextCursor(selected);
        }
        exposeSelectedRegions();
        // A rendered boundary can represent several source offsets (for
        // example the opening markers of bold text). Nearest visible positions
        // cannot restore an exact range inside that hidden syntax.
        if(anchor!=position&&sourceSelection()!=qMakePair(anchor,position))
            exposeSource(anchor,position);
        ensureCursorVisible();
    }

void LiveEditor::updateLiveLineStarts() const {
        if(numberedSource==source&&!liveLineStarts.isEmpty())return;
        numberedSource=source;
        QList<QPair<int,int>> atomic;
        for(const auto &node:mdedit::MarkdownParser::parse(source)){
            if(node.resource)atomic.append({node.source.begin,node.source.end});
            if(node.kind==mdedit::BlockKind::Table){
                // The parser's paragraph can include text following a tight table.
                // Only the header, divider and actual table rows belong to it.
                int end=source.indexOf('\n',node.source.begin);
                if(end<0)continue;
                end=source.indexOf('\n',end+1);
                if(end<0)end=source.size();
                while(end<node.source.end){
                    int next=source.indexOf('\n',end+1);
                    if(next<0)next=source.size();
                    if(!source.mid(end+1,next-end-1).contains('|'))break;
                    end=next;
                }
                atomic.append({node.source.begin,end});
            }
        }
        for(const auto &span:scanSpans(source))
            if(span.kind==SpanKind::DisplayMath)atomic.append({span.start,span.start+span.length});
        std::sort(atomic.begin(),atomic.end());
        liveLineStarts={0};
        numberedAtomicRanges=atomic;
        for(int at=source.indexOf('\n');at>=0;at=source.indexOf('\n',at+1))
            liveLineStarts.append(at+1);
    }

int LiveEditor::liveLineAtSource(int position) const {
        updateLiveLineStarts();
        // Rendered blocks retain their opening source line; subsequent blocks
        // keep their original source numbers, including hidden lines.
        for(const auto &range:numberedAtomicRanges){
            if(range.first>position)break;
            if(position<range.second){position=range.first;break;}
        }
        return int(std::upper_bound(liveLineStarts.cbegin(),liveLineStarts.cend(),position)-liveLineStarts.cbegin());
    }

int LiveEditor::sourceLineCount()const {
        updateLiveLineStarts();
        return liveLineStarts.size();
    }

int LiveEditor::lineNumberAt(const QTextCursor &cursor)const {
        if(const auto table=cursor.currentTable()){
            const int index=regionAt(table->firstPosition());
            if(index>=0)return liveLineAtSource(regions[index].sourceStart);
        }
        return liveLineAtSource(sourcePosition(cursor.position()));
    }

int LiveEditor::regionAt(int pos)const{
        // At an inline boundary the following span owns the caret.
        for(int i=0;i<regions.size();++i){
            const auto &r=regions[i];
            if(pos<r.start||pos>r.end)continue;
            if(pos==r.end&&i+1<regions.size()&&regions[i+1].start==pos)continue;
            return i;
        }
        return -1;
    }

void LiveEditor::activateCaretRegion(){
        const auto caret=textCursor();
        if(caret.hasSelection())return;
        // A selection may merge several lines into the active source region.
        // Reparse it even if the collapsed caret is still inside that region.
        if(selectionSourceExpanded){
            const int position=sourcePosition(caret.position());
            rebuild(-1,0,position);
            return;
        }
        // Keep exposed inline syntax visible through both outer caret edges.
        // Limit this to activation: source mapping still uses normal ownership.
        if(active>=0&&active<regions.size()&&regions[active].inlinePart&&
           caret.position()>=regions[active].start&&caret.position()<=regions[active].end)return;
        if(caret.currentTable()&&bindNativeTable(caret))return;
        if(nativeTable)revealNativeCell(caret);
        nativeTable.clear();nativeStart=nativeEnd=-1;
        const int i=regionAt(textCursor().position());
        if(isResourceRegion(i)&&regions[i].sourceStart!=resourceSourceEditStart)return;
        if(!isResourceRegion(i))resourceSourceEditStart=-1;
        if(i>=0&&i!=active){
            // Qt's empty paragraphs around a table can share its source
            // region, especially at the document start. Activating that
            // region would map an outside caret straight back into a cell.
            const auto &region=regions[i];
            for(auto block=document()->findBlock(region.start);block.isValid()&&block.position()<=region.end;block=block.next())
                if(QTextCursor(block).currentTable())return;
            rebuild(i,sourcePosition(textCursor().position())-region.sourceStart);
        }
    }

bool LiveEditor::isResourceRegion(int index) const{
        if(index<0||index>=regions.size()||regions[index].codeBlock||regions[index].inlinePart)return false;
        const auto &region=regions[index];
        const auto nodes=mdedit::MarkdownParser::parse(source.mid(region.sourceStart,region.sourceEnd-region.sourceStart));
        return nodes.size()==1&&nodes.first().resource.has_value();
    }

QList<QPair<int,bool>> LiveEditor::inlinePieces(const QString &text){
        // Use independent inline spans only where they don't change the
        // surrounding Markdown structure. Complex blocks use the full parser.
        if(text.startsWith(' ')||text.startsWith('\t'))return {};
        static const QRegularExpression blockMarker(R"((?:^|\n)(?: {0,3}(?:#{1,6}(?: |$)|>|[-+*](?: |$)|\d+[.)] )| {4}|\t))");
        if(blockMarker.match(text).hasMatch())return {};

        if(text.contains('\n')&&!isMultilineParagraph(text))return {};
        // Nested styling needs its surrounding delimiters while editing.
        // Splitting out only the inner token would lose the outer style.
        QList<QPair<int,int>> styledRanges;
        for(const auto &style:inlineEditingStyles(text))
            if(style.format.hasProperty(QTextFormat::FontWeight)||style.format.hasProperty(QTextFormat::FontItalic))
                styledRanges.append({style.start,style.start+style.length});
        std::sort(styledRanges.begin(),styledRanges.end());
        for(int i=1;i<styledRanges.size();++i)
            if(styledRanges[i].first<styledRanges[i-1].second)return {};
        QList<QPair<int,int>> tokens;
        for(const auto &span:scanSpans(text)){
            if(span.kind==SpanKind::DisplayMath)return {};
            if(span.kind==SpanKind::Code&&text.mid(span.start,span.length).contains('\n'))return {};
            tokens.append({span.start,span.start+span.length});
        }
        for(const auto &range:highlightRanges(text))tokens.append(range);
        static const QRegularExpression emphasis(
            R"((?<!\*)(\*{1,3})(?!\*)((?:\\[^\n]|[^\\*\n])+)\1(?!\*)|(?<!~)(~~)(?!~)((?:\\[^\n]|[^\\~\n])+)\3(?!~))");
        auto matches=emphasis.globalMatch(text);
        while(matches.hasNext()){
            const auto match=matches.next();
            if(escaped(text,match.capturedStart()))continue;
            const bool insideToken=std::any_of(tokens.begin(),tokens.end(),[&](const auto &token){
                return match.capturedStart()>=token.first&&match.capturedEnd()<=token.second;
            });
            if(insideToken)continue;
            const auto inner=match.captured(match.capturedStart(2)>=0?2:4);
            if(inner.front().isSpace()||inner.back().isSpace())continue;
            tokens.append({match.capturedStart(),match.capturedEnd()});
        }
        if(tokens.isEmpty())return {};
        std::sort(tokens.begin(),tokens.end());
        static const QRegularExpression otherMarkup(R"([#>*_`~\[\]<&\\$]|^[-+ ]|^\d+[.)] )");
        const auto literalGap=[&](const QString &gap){
            if(!otherMarkup.match(gap.trimmed()).hasMatch())return true;
            // Incomplete delimiters are ordinary text. They must not force
            // unrelated, valid inline spans into the source fallback.
            QTextDocument reference;reference.setMarkdown(gap.trimmed());
            return reference.toPlainText()==gap.trimmed();
        };
        QList<QPair<int,bool>> pieces;
        int at=0;
        for(const auto &token:tokens){
            if(token.first<at)return {};
            if(token.first>at){
                if(!literalGap(text.mid(at,token.first-at)))return {};
                pieces.append({token.first,false});
            }
            pieces.append({token.second,true});
            at=token.second;
        }
        if(at<text.size()){
            if(!literalGap(text.mid(at)))return {};
            pieces.append({text.size(),false});
        }
        return pieces;
    }

bool LiveEditor::isTextBlock(const QTextBlock &block){
        const auto format=block.blockFormat();
        return !block.textList()&&!QTextCursor(block).currentTable()&&!format.indent()&&
            !isCodeBlock(format)&&
            !format.hasProperty(QTextFormat::BlockQuoteLevel)&&!format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    }

bool LiveEditor::isMultilineParagraph(const QString &text){
        if(!text.contains('\n'))return false;
        for(const auto &span:scanSpans(text))
            if(span.kind==SpanKind::DisplayMath||text.mid(span.start,span.length).contains('\n'))return false;
        // A terminal backslash is already a Markdown hard break.
        if(text.contains("\\\n"))return false;
        QTextDocument reference;reference.setMarkdown(markdownWithoutSetext(text));
        const auto block=reference.begin();
        return reference.blockCount()==1&&isTextBlock(block)&&!block.blockFormat().headingLevel();
    }

void LiveEditor::preserveResourceSlot(){
        if(resourceEditSlotHeight<=0||!isResourceRegion(active)||!document()->isLayoutEnabled())return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker signalBlocker(this);
        auto block=document()->findBlock(regions[active].start);
        const qreal top=document()->documentLayout()->blockBoundingRect(block).top();
        const qreal bottom=block.next().isValid()?document()->documentLayout()->blockBoundingRect(block.next()).top():document()->size().height()-document()->documentMargin();
        QTextCursor cursor(block);auto format=block.blockFormat();
        format.setBottomMargin(format.bottomMargin()+qMax(qreal(0),resourceEditSlotHeight-(bottom-top)));cursor.setBlockFormat(format);
    }

void LiveEditor::preserveMathSlot(){
        if(mathEditSlotHeight<=0||active<0||sourceExposed||
           regions[active].sourceStart!=mathEditSourceStart||!document()->isLayoutEnabled())return;
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker signalBlocker(this);
        auto *layout=document()->documentLayout();
        const auto first=document()->findBlock(regions[active].start);
        const auto last=document()->findBlock(regions[active].end);
        // Adjacent paragraph margins collapse; a second pass accounts for
        // the neighbour's margin before the reserved slot reaches its target.
        for(int pass=0;pass<3;++pass){
            const qreal bottom=last.next().isValid()?layout->blockBoundingRect(last.next()).top():document()->size().height()-document()->documentMargin();
            const qreal top=first.previous().isValid()?layout->blockBoundingRect(first.previous()).top():document()->documentMargin();
            const qreal missing=mathEditSlotHeight-(bottom-top);
            if(missing<=0.1)break;
            QTextCursor cursor(last);auto format=last.blockFormat();
            format.setBottomMargin(format.bottomMargin()+missing);
            cursor.setBlockFormat(format);
        }
    }

void LiveEditor::styleActiveSource(){
        mathPreviewVisible=false;
        if(active<0){if(mathPreviewPanel)mathPreviewPanel->hide();return;}
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker blockEditor(this);
        const auto &region=regions[active];
        const QString raw=source.mid(region.sourceStart,region.end-region.start);
        const auto body=codeBody(raw,region.codeFenceStart);
        const bool fenced=body.start>0;
        const bool joinedParagraph=!region.codeBlock&&!fenced&&isMultilineParagraph(raw);
        QTextDocument bodyStyle;bodyStyle.setDefaultFont(font());bodyStyle.setMarkdown("Body");
        static const QRegularExpression heading("^ {0,3}(#{1,6})(?:[ \t]+|$)");
        static const QRegularExpression quote("^ {0,3}(?:>[ \t]*)+");
        if(region.inlinePart){
            QTextCursor part(document());part.setPosition(region.start);
            part.setPosition(region.end,QTextCursor::KeepAnchor);
            QTextCharFormat normal;normal.setFont(font());part.setCharFormat(normal);
        }
        for(auto block=document()->findBlock(region.start);!region.inlinePart&&block.isValid()&&block.position()<=region.end;block=block.next()){
            QTextCharFormat characters;characters.setFont(font());
            QTextBlockFormat paragraph=bodyStyle.begin().blockFormat();
            if(region.listItemFormat){
                paragraph=*region.listItemFormat;
                if(block.position()>region.start)paragraph.setTopMargin(0);
                if(block.next().isValid()&&block.next().position()<=region.end)paragraph.setBottomMargin(0);
            }
            if(region.continuesBefore)paragraph.setTopMargin(0);
            if(region.continuesAfter)paragraph.setBottomMargin(0);
            if(joinedParagraph){
                if(block.position()>region.start)paragraph.setTopMargin(0);
                if(block.next().isValid()&&block.next().position()<=region.end)paragraph.setBottomMargin(0);
            }
            const auto quotePrefix=quote.match(block.text());
            if(!region.codeBlock&&!fenced&&quotePrefix.hasMatch()){
                // Keep the preview's quote indentation and available body width.
                // Hang the editable markers in its margin so revealing them
                // does not rewrap the body and move every following block.
                paragraph=renderedDocument(block.text(),false)->begin().blockFormat();
                paragraph.setObjectIndex(-1);
                paragraph.setTextIndent(-QFontMetricsF(font()).horizontalAdvance(quotePrefix.captured()));
                paragraph.setBackground(mdedit::themeColor(theme,QPalette::AlternateBase,quoteBackground));
            }
            const auto match=heading.match(block.text());
            setBodyLineHeight(paragraph,font());
            if(region.codeBlock||fenced){
                paragraph=codeBlockFormat();
                characters=codeCharacters();
            }else if(match.hasMatch()){
                // Ask Qt for its preview heading style, then apply it to the
                // whole source line, including the visible Markdown markers.
                characters.merge(headingStyle(match.capturedLength(1),font(),paragraph));
            }
            const int lineStart=qMax(block.position(),region.start);
            const int lineEnd=qMin(block.position()+int(block.text().size()),region.end);
            QTextCursor line(block);line.setPosition(lineStart);
            line.setPosition(lineEnd,QTextCursor::KeepAnchor);
            line.setBlockFormat(paragraph);
            // A merged cross-line selection can start or end inside a
            // paragraph. Its remaining previews still own their character
            // formatting, even though they share this block with source.
            if(lineStart==block.position()&&lineEnd==block.position()+block.text().size())
                line.setBlockCharFormat(characters);
            line.setCharFormat(characters);
        }
        if(!region.codeBlock&&!fenced)for(const auto &style:inlineEditingStyles(raw,theme)){
            QTextCursor token(document());token.setPosition(region.start+style.start);
            token.setPosition(token.position()+style.length,QTextCursor::KeepAnchor);
            token.mergeCharFormat(style.format);
        }
        // Preview lives outside the editable document so copying, source offsets
        // and undo history contain only Markdown. Reserve space below the source.
        if(!MDEDIT_LITE&&!sourceExposed&&!region.codeBlock&&!fenced){
            QStringList formulas;
            for(const auto &span:scanSpans(raw))
                if(span.kind==SpanKind::DisplayMath)formulas.append(raw.mid(span.start,span.length));
            if(!formulas.isEmpty()){
                if(!mathPreviewPanel){
                    mathPreviewPanel=new QFrame(viewport());
                    mathPreviewPanel->setObjectName("mathPreviewPanel");
                    mathPreviewPanel->setAttribute(Qt::WA_ShowWithoutActivating);
                    mathPreviewPanel->setFocusPolicy(Qt::NoFocus);
                    mathPreviewPanel->setFrameShape(QFrame::StyledPanel);
                    auto *layout=new QVBoxLayout(mathPreviewPanel);
                    layout->setContentsMargins(12,8,12,12);
                    mathPreviewView=new MathPreviewWidget(mathPreviewPanel);
                    mathPreviewView->setObjectName("mathPreviewView");
                    mathPreviewView->setFocusPolicy(Qt::NoFocus);
                    layout->addWidget(mathPreviewView);
                }
                const int width=qMax(40,viewport()->width()-32);
                const auto margins=mathPreviewPanel->layout()->contentsMargins();
                const int frame=mathPreviewPanel->frameWidth()*2;
                mathPreviewView->setFormula(formulas.join("\n\n"),font(),
                    qMax(1,width-margins.left()-margins.right()-frame));
                const int height=mathPreviewView->height()+margins.top()+margins.bottom()+frame;
                mathPreviewPanel->resize(width,height);
                QTextCursor last(document()->findBlock(region.end));
                auto format=last.blockFormat();
                format.setBottomMargin(format.bottomMargin()+height+16);
                last.setBlockFormat(format);
                mathPreviewVisible=true;
            }
        }
        if(!mathPreviewVisible&&mathPreviewPanel)mathPreviewPanel->hide();
        preserveMathSlot();
        viewport()->update();
        if(region.codeBlock||fenced)colorCode(raw.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
            QTextCursor token(document());token.setPosition(region.start+body.start+start);
            token.setPosition(token.position()+length,QTextCursor::KeepAnchor);
            QTextCharFormat format;format.setForeground(mdedit::syntaxColor(theme,color));token.mergeCharFormat(format);
        });
    }

int LiveEditor::sourcePosition(int pos)const{
        const int i=regionAt(pos);
        if(i>=0){const auto &r=regions[i];
            if(i==active)return r.sourceStart+pos-r.start;
            if(r.codeBlock)return r.sourceStart+pos-r.start;
            if(r.start==r.end)return r.sourceStart;
            if(pos==r.end)return r.contentEnd>=0?r.contentEnd:r.sourceEnd;
            QString visible=oldText.mid(r.start,r.end-r.start);int at=r.sourceStart;
            // Walk one character ahead to skip leading Markdown delimiters too.
            for(int j=0;j<=pos-r.start&&j<visible.size();++j){
                if(j==0||visible[j-1]=='\n'){
                    const auto block=document()->findBlock(r.start+j);
                    if(block.textList()&&block.position()==r.start+j){
                        const auto match=listMarker().match(source.mid(at,r.sourceEnd-at).section('\n',0,0));
                        if(match.hasMatch())at+=match.capturedLength();
                    }
                }
                QChar ch=visible[j];
                if(ch==QChar::ObjectReplacementCharacter){
                    for(const auto &span:scanSpans(source))if(span.start>=at&&span.start<r.sourceEnd&&span.kind!=SpanKind::Code){
                        if(j==pos-r.start)return span.start;
                        at=span.start+span.length;break;
                    }
                    continue;
                }
                int found=source.indexOf(ch,at);
                if(found>=0&&found<r.sourceEnd){if(j==pos-r.start)return found;at=found+1;}
            }
            return at;
        }return int(source.size());
    }

void LiveEditor::exposeSource(int begin,int end){
        rebuilding=true;setPlainText(source);
        sourceExposed=true;selectionSourceExpanded=true;
        expandedSelectionPieces={{0,int(source.size())}};
        regions={{0,int(source.size()),0,int(source.size())}};active=0;oldText=source;
        QTextCursor selected(document());selected.setPosition(qBound(0,begin,int(source.size())));
        selected.setPosition(qBound(0,end,int(source.size())),QTextCursor::KeepAnchor);
        setTextCursor(selected);styleActiveSource();rebuilding=false;
    }

void LiveEditor::exposeSelectedRegions(){
        const auto caret=textCursor();
        if(!caret.hasSelection()||cellRangeDragging||axisPressed||caret.currentTable())return;
        if(selectionSourceExpanded){
            const auto range=sourceSelection();
            const int begin=qMin(range.first,range.second),end=qMax(range.first,range.second);
            // Keep a touched fragment exposed while moving within its syntax.
            // Rebuilding on every character shrink changes the hit-test geometry
            // underneath the mouse and repeatedly hides/reveals delimiters.
            const bool withdrawn=std::any_of(expandedSelectionPieces.cbegin(),expandedSelectionPieces.cend(),
                [&](const QPair<int,int> &piece){return end<piece.first||begin>piece.second;});
            if(withdrawn||sourceExposed){
                // Merged source regions cannot shed individual previews. Rebuild
                // from the current selection so withdrawn fragments render again.
                const int scroll=verticalScrollBar()->value();
                restoreSourceSelection(range.first,range.second);
                verticalScrollBar()->setValue(scroll);
                return;
            }
        }
        if(sourceExposed)return;
        if(caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1){
            exposeSource(caret.anchor()<caret.position()?0:int(source.size()),
                         caret.anchor()<caret.position()?int(source.size()):0);
            return;
        }
        // The first inline region of a list item starts after its painted
        // marker. Include that hidden prefix when exposing the item's source.
        auto regionSourceStart=[&](const Region &region){
            if(!region.listItem||document()->findBlock(region.start).position()!=region.start)
                return region.sourceStart;
            const int lineStart=region.sourceStart==0?0:source.lastIndexOf('\n',region.sourceStart-1)+1;
            const auto marker=listMarker().match(source.mid(lineStart).section('\n',0,0));
            return marker.hasMatch()&&lineStart+marker.capturedLength()==region.sourceStart?
                lineStart:region.sourceStart;
        };
        int first=-1,last=-1;
        bool rendered=false;
        for(int i=0;i<regions.size();++i){
            const auto &r=regions[i];
            if(caret.selectionStart()>=r.end||caret.selectionEnd()<=r.start)continue;
            if(first<0)first=i;
            last=i;
            if(i!=active||regionSourceStart(r)<r.sourceStart)rendered=true;
        }
        if(!rendered||first<0)return;
        // Merge touched previews into one editable source range. Keep the
        // selection's source offsets rather than selecting entire constructs.
        int begin=sourcePosition(caret.selectionStart()),finish=sourcePosition(caret.selectionEnd());
        for(int i=first;i<=last;++i){
            const auto &r=regions[i];
            if((i!=active||regionSourceStart(r)<r.sourceStart)&&caret.selectionStart()<=r.start&&caret.selectionEnd()>=r.end){
                begin=qMin(begin,regionSourceStart(r));
                finish=qMax(finish,r.contentEnd>=0?r.contentEnd:r.sourceEnd);
            }
        }
        const int anchor=caret.anchor()<caret.position()?begin:finish;
        const int position=caret.anchor()<caret.position()?finish:begin;
        // QTextCursor follows document mutations. Snapshot both visible ends
        // before replacing previews, including anchors on empty regions.
        const int visibleAnchor=caret.anchor(),visiblePosition=caret.position();
        const int start=regions[first].start,end=regions[last].end;
        const int sourceStart=regionSourceStart(regions[first]);
        const int sourceEnd=regions[last].contentEnd>=0?regions[last].contentEnd:regions[last].sourceEnd;
        const QString raw=source.mid(sourceStart,sourceEnd-sourceStart);
        const int delta=raw.size()-(end-start);
        // Preserve the original fragment boundaries before merging them. They
        // determine when a shrinking selection actually leaves a preview.
        for(int i=first;i<=last;++i){
            if(selectionSourceExpanded&&i==active)continue;
            const auto &region=regions[i];
            expandedSelectionPieces.append({regionSourceStart(region),
                region.contentEnd>=0?region.contentEnd:region.sourceEnd});
        }
        QScopedValueRollback<bool> guard(rebuilding,true);
        const QSignalBlocker blockSignals(this);
        QTextCursor replacement(document());replacement.setPosition(start);
        replacement.setPosition(end,QTextCursor::KeepAnchor);
        replacement.insertText(raw,QTextCharFormat());
        Region merged=regions[first];merged.sourceStart=sourceStart;
        merged.end=start+raw.size();merged.sourceEnd=regions[last].sourceEnd;
        merged.contentEnd=sourceEnd;
        // Inline source must not restyle the entire paragraph: untouched bold,
        // links and formulas on either side still belong to their previews.
        merged.inlinePart=regions[first].inlinePart&&regions[last].inlinePart&&
            sourceStart==regions[first].sourceStart&&
            document()->findBlock(start)==document()->findBlock(start+raw.size());
        merged.codeBlock=false;
        for(int i=last;i>=first;--i)regions.removeAt(i);
        regions.insert(first,merged);active=first;selectionSourceExpanded=true;
        for(int i=first+1;i<regions.size();++i){regions[i].start+=delta;regions[i].end+=delta;}
        oldText=toPlainText();
        auto visible=[&](int sourceOffset,int previousPosition){
            if(sourceOffset>=sourceStart&&sourceOffset<=sourceEnd)return start+sourceOffset-sourceStart;
            return previousPosition<=start?previousPosition:previousPosition+delta;
        };
        QTextCursor selected(document());selected.setPosition(visible(anchor,visibleAnchor));
        selected.setPosition(visible(position,visiblePosition),QTextCursor::KeepAnchor);setTextCursor(selected);
        styleActiveSource();refreshLineNumbers();
    }

void LiveEditor::prepareSelection(){
        const auto caret=textCursor();
        if(caret.hasSelection()&&caret.selectionStart()==0&&caret.selectionEnd()==document()->characterCount()-1){
            exposeSource(caret.anchor()<caret.position()?0:int(source.size()),
                         caret.anchor()<caret.position()?int(source.size()):0);return;
        }
        if(caret.hasSelection()){
            int begin=sourcePosition(caret.selectionStart());
            int end=sourcePosition(caret.selectionEnd());
            // A complete preview belongs to its complete source, including
            // invisible delimiters. Resolve both edges before exposing source;
            // regionAt assigns shared boundaries to the following region.
            for(int i=0;i<regions.size();++i){
                const auto &region=regions[i];
                if(i==active||region.start==region.end)continue;
                if(caret.selectionStart()<=region.start&&caret.selectionEnd()>=region.end){
                    begin=qMin(begin,region.sourceStart);
                    end=qMax(end,region.contentEnd>=0?region.contentEnd:region.sourceEnd);
                }
            }
            if(begin!=sourcePosition(caret.selectionStart())||end!=sourcePosition(caret.selectionEnd())){
                exposeSource(caret.anchor()<caret.position()?begin:end,
                             caret.anchor()<caret.position()?end:begin);
                return;
            }
        }
        if(active<0||caret.selectionStart()<regions[active].start||caret.selectionEnd()>regions[active].end)
            exposeSource(sourcePosition(caret.anchor()),sourcePosition(caret.position()));
    }

