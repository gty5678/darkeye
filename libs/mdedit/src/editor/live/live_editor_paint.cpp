#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>

QTextCharFormat LiveEditor::codeCharacters()const{
        auto codeFont=font();
        codeFont.setFamily(QString::fromLatin1(editorFont));
        codeFont.setStyleHint(QFont::Monospace);
        QTextCharFormat characters;
        characters.setFont(codeFont);
        characters.setForeground(mdedit::themeColor(theme,QPalette::Text,QColor("#37474f")));
        return characters;
    }

bool LiveEditor::viewportEvent(QEvent *event){
    if(event->type()==QEvent::Resize||event->type()==QEvent::Show){
        QTimer::singleShot(0,this,[this]{if(!rebuilding){layoutNativeTables();styleActiveSource();resourceWindowTimer.start();}});
    }
    if(event->type()==QEvent::Hide)resourceWindowTimer.start();
    if(event->type()==QEvent::Hide&&mathPreviewPanel)mathPreviewPanel->hide();
    if(event->type()==QEvent::Leave){tableHoverPoint=QPoint(-100,-100);viewport()->update();}
    return QTextBrowser::viewportEvent(event);
}

void LiveEditor::paintEvent(QPaintEvent *event){
        syncPdfViews();
        syncMathPreview();
        // Paint one panel per source region, including the leading, trailing
        // and inter-line space that paragraph backgrounds leave uncovered.
        {
            QPainter painter(viewport());
            painter.setClipRegion(event->region());
            auto *layout=document()->documentLayout();
            const QPointF scroll(horizontalScrollBar()->value(),verticalScrollBar()->value());
            for(const auto &region:regions){
                if(!region.codeBlock)continue;
                const auto first=document()->findBlock(region.start);
                const auto last=document()->findBlock(region.end);
                if(!first.isValid()||!last.isValid())continue;
                const auto top=layout->blockBoundingRect(first);
                const auto bottom=layout->blockBoundingRect(last);
                QRectF panel(top.left(),top.top(),top.width(),bottom.bottom()-top.top());
                panel.translate(-scroll);
                if(panel.intersects(event->rect()))painter.fillRect(panel,mdedit::themeColor(theme,QPalette::Button,codeBlockBackground));
            }
        }
        NumberedEditor<QTextBrowser>::paintEvent(event);
        QColor selectionTint=mdedit::themeColor(theme,QPalette::Highlight,QColor(137,102,194));
        selectionTint.setAlpha(40);
        QPainter controls(viewport());controls.setRenderHint(QPainter::Antialiasing);
        // Qt's built-in task marker is a small, hairline square. Replace its
        // ink in the list gutter without changing the document or source map.
        for(auto block=document()->begin();block.isValid();block=block.next()){
            const auto marker=block.blockFormat().marker();
            if(!block.textList()||marker==QTextBlockFormat::MarkerType::NoMarker)continue;
            const auto *layout=block.layout();
            if(!layout||!layout->lineCount())continue;
            const auto line=layout->lineAt(0);
            const QRect caret=cursorRect(QTextCursor(block));
            if(caret.bottom()<event->rect().top()||caret.top()>event->rect().bottom())continue;
            const QFontMetricsF metrics(block.charFormat().font());
            const qreal size=qBound(14.0,metrics.height()*0.78,22.0);
            const qreal gap=6;
            const bool rtl=block.textDirection()==Qt::RightToLeft;
            const qreal edge=caret.left();
            const qreal baseline=caret.top()+line.ascent();
            const QRectF box(rtl?edge+gap:edge-gap-size,
                             baseline-(metrics.ascent()-metrics.descent()+size)/2,size,size);
            // Cover the native marker, including its antialiased border.
            const qreal gutter=qMax(size+gap+2,metrics.lineSpacing()+metrics.horizontalAdvance(' '));
            QRectF erase(rtl?edge+1:edge-gutter,caret.top()-2,gutter-1,
                         qMax(qreal(caret.height()),metrics.height())+4);
            QBrush background=block.blockFormat().background();
            if(background.style()==Qt::NoBrush)background=palette().brush(QPalette::Base);
            controls.fillRect(erase,background);
            const bool checked=marker==QTextBlockFormat::MarkerType::Checked;
            controls.save();
            controls.setPen(QPen(mdedit::themeColor(theme,checked?QPalette::Link:QPalette::Mid,QColor(checked?"#8b72aa":"#aaa2b5")),1.5));
            controls.setBrush(checked?mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#eee8f5")):background);
            controls.drawRoundedRect(box,3,3);
            if(checked){
                controls.setPen(QPen(mdedit::themeColor(theme,QPalette::Link,QColor("#79569e")),1.9,Qt::SolidLine,Qt::RoundCap));
                const QRectF cross=box.adjusted(size*0.28,size*0.28,-size*0.28,-size*0.28);
                controls.drawLine(cross.topLeft(),cross.bottomRight());
                controls.drawLine(cross.topRight(),cross.bottomLeft());
            }
            controls.restore();
        }
        for(const auto &item:axisControls()){
            controls.setPen(Qt::NoPen);
            if(axisSource==item.start&&axisIndex>=0){const auto &rects=axisRow?item.rows:item.columns;
                if(axisLast>=0&&qMax(axisIndex,axisLast)<rects.size()){
                    controls.save();
                    controls.setBrush(selectionTint);
                    const QRectF selected=rects[qMin(axisIndex,axisLast)].united(rects[qMax(axisIndex,axisLast)]);
                    controls.drawRect(selected);
                    controls.setBrush(Qt::NoBrush);controls.setPen(QPen(mdedit::themeColor(theme,QPalette::Highlight,QColor("#8856bc")),2));
                    controls.drawRect(selected);
                    controls.restore();
                }}
            if(cellRangeDragging&&cellRangeTable==item.table){
                const QRectF rows=item.rows[qMin(cellAnchorRow,cellEndRow)].united(item.rows[qMax(cellAnchorRow,cellEndRow)]);
                const QRectF columns=item.columns[qMin(cellAnchorColumn,cellEndColumn)].united(item.columns[qMax(cellAnchorColumn,cellEndColumn)]);
                const QRectF selected=rows.intersected(columns);
                controls.save();controls.setBrush(selectionTint);
                controls.setPen(QPen(mdedit::themeColor(theme,QPalette::Highlight,QColor("#8856bc")),2));controls.drawRect(selected);controls.restore();
            }
            auto grip=[&](QRectF rect,bool row){controls.setPen(Qt::NoPen);controls.setBrush(mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#eee9f7")));controls.drawRoundedRect(rect,3,3);controls.setBrush(mdedit::themeColor(theme,QPalette::Text,QColor("#80708f")));
                for(int i=0;i<3;++i)controls.drawEllipse(row?QPointF(rect.center().x(),rect.top()+3+i*4):QPointF(rect.left()+3+i*4,rect.center().y()),1,1);};
            for(int i=0;i<item.rows.size();++i){const auto &rect=item.rows[i];
                if(QRectF(item.frame.left()-22,rect.top()-2,23,rect.height()+4).contains(tableHoverPoint)||(axisPressed&&axisSource==item.start&&axisRow&&axisIndex==i))
                    grip(QRectF(item.frame.left()-19,rect.center().y()-7,15,14),true);
            }
            for(int i=0;i<item.columns.size();++i){const auto &rect=item.columns[i];
                if(QRectF(rect.left()-2,item.frame.top()-22,rect.width()+4,23).contains(tableHoverPoint)||(axisPressed&&axisSource==item.start&&!axisRow&&axisIndex==i))
                    grip(QRectF(rect.center().x()-7,item.frame.top()-19,14,15),false);
            }
            auto plus=[&](QRectF rect){if(!rect.adjusted(-3,-3,3,3).contains(tableHoverPoint))return;
                controls.setPen(Qt::NoPen);controls.setBrush(mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#eee9f7")));controls.drawRoundedRect(rect,4,4);
                controls.setPen(QPen(mdedit::themeColor(theme,QPalette::Link,QColor("#79529d")),2));const QPointF center=rect.center();controls.drawLine(center+QPointF(-4,0),center+QPointF(4,0));controls.drawLine(center+QPointF(0,-4),center+QPointF(0,4));};
            plus(item.addRow);plus(item.addColumn);
            if(axisPressed&&axisStarted&&axisBoundary>=0&&axisSource==item.start){controls.setPen(QPen(mdedit::themeColor(theme,QPalette::Highlight,QColor("#8856bc")),3));
                if(axisRow)controls.drawLine(QPointF(item.frame.left(),axisDropPosition),QPointF(item.frame.right(),axisDropPosition));
                else controls.drawLine(QPointF(axisDropPosition,item.frame.top()),QPointF(axisDropPosition,item.frame.bottom()));}
        }

        for(const auto &item:codeCopyControls()){
            const bool copied=copiedCodeSource==regions[item.region].sourceStart;
            const bool hovered=item.rect.contains(tableHoverPoint);
            controls.save();
            controls.setPen(Qt::NoPen);
            if(copied||hovered){
                controls.setBrush(mdedit::themeColor(theme,QPalette::AlternateBase,QColor(copied?"#e2efe7":"#ddd8e5")));
                controls.drawRoundedRect(item.rect,6,6);
            }
            // Draw in logical coordinates so the icon stays crisp on Retina displays.
            controls.translate(item.rect.center()-QPointF(8,8));
            controls.setBrush(Qt::NoBrush);
            controls.setPen(QPen(mdedit::themeColor(theme,(copied||hovered)?QPalette::Link:QPalette::Text,QColor(copied?"#3c8060":hovered?"#655080":"#82798d")),
                                 1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            if(copied){
                QPainterPath check;check.moveTo(3,8);check.lineTo(6.5,11.5);check.lineTo(13,4.5);
                controls.drawPath(check);
            }else{
                QPainterPath back;back.moveTo(5,10);back.lineTo(3.5,10);
                back.quadTo(2,10,2,8.5);back.lineTo(2,3.5);back.quadTo(2,2,3.5,2);
                back.lineTo(8.5,2);back.quadTo(10,2,10,3.5);back.lineTo(10,5);
                controls.drawPath(back);
                controls.drawRoundedRect(QRectF(6,6,8,8),1.5,1.5);
            }
            controls.restore();
        }
        for(const auto &item:blockControls()){
            auto button=[&](const QRectF &rect,bool open,bool enabled){
                const bool hovered=enabled&&rect.contains(tableHoverPoint);
                controls.save();
                controls.setPen(Qt::NoPen);
                controls.setBrush(mdedit::themeColor(theme,hovered?QPalette::AlternateBase:QPalette::Button,QColor(hovered?"#ddd8e5":"#efedf1")));
                controls.drawRoundedRect(rect,6,6);
                controls.translate(rect.center()-QPointF(8,8));
                controls.setBrush(Qt::NoBrush);
                controls.setPen(QPen(mdedit::themeColor(theme,!enabled?QPalette::Mid:hovered?QPalette::Link:QPalette::Text,QColor(!enabled?"#aaa3b2":hovered?"#655080":"#82798d")),
                                     1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
                QPainterPath icon;
                if(open){
                    // Open the original file in its external application.
                    icon.moveTo(7,3);icon.lineTo(3,3);icon.lineTo(3,13);
                    icon.lineTo(13,13);icon.lineTo(13,9);
                    icon.moveTo(9,2);icon.lineTo(14,2);icon.lineTo(14,7);
                    icon.moveTo(14,2);icon.lineTo(7,9);
                }else{
                    // Source brackets and slash identify Markdown source editing.
                    icon.moveTo(5,4);icon.lineTo(1,8);icon.lineTo(5,12);
                    icon.moveTo(11,4);icon.lineTo(15,8);icon.lineTo(11,12);
                    icon.moveTo(9.5,2.5);icon.lineTo(6.5,13.5);
                }
                controls.drawPath(icon);
                controls.restore();
            };
            button(item.open,true,!item.file.isEmpty());button(item.edit,false,true);
        }
    }

QTextBlockFormat LiveEditor::codeBlockFormat()const{
        QTextBlockFormat format;
        setBodyLineHeight(format,font());
        format.setBackground(mdedit::themeColor(theme,QPalette::Button,codeBlockBackground));
        return format;
    }

QTextCharFormat LiveEditor::headingStyle(int level,const QFont &font,QTextBlockFormat &format){
        QTextDocument reference;
        reference.setDefaultFont(font);
        reference.setMarkdown(QString(level,'#')+" Heading");
        const auto heading=reference.begin();
        format=heading.blockFormat();
        const auto characters=headingCharacters(level,font);
        format.setLineHeight(qCeil(QFontMetricsF(characters.font()).lineSpacing()),QTextBlockFormat::FixedHeight);
        return characters;
    }

bool LiveEditor::isStandaloneImage(const QTextBlock &block){
        if(block.text().trimmed()!=QString(QChar::ObjectReplacementCharacter))return false;
        for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(format.isImageFormat())return !format.hasProperty(formulaPaintAscent);
        }
        return false;
    }

void LiveEditor::fitInlineImages(const QTextBlock &block,QTextDocument *document){
        const qreal ascent=qFloor(QFontMetricsF(document->defaultFont()).ascent());
        auto *handler=document->documentLayout()->handlerForObject(QTextFormat::ImageObject);
        const QString text=block.text();
        QTextCursor cursor(block);
        for(int offset=text.indexOf(QChar::ObjectReplacementCharacter);offset>=0;
            offset=text.indexOf(QChar::ObjectReplacementCharacter,offset+1)){
            const int position=block.position()+offset;
            cursor.setPosition(position);
            cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            auto image=cursor.charFormat().toImageFormat();
            if(!image.isValid()||image.hasProperty(formulaPaintAscent))continue;
            if(QRegularExpression(R"(\|[1-9][0-9]*$)").match(image.stringProperty(QTextFormat::ImageAltText)).hasMatch())continue;
            const QSizeF size=handler->intrinsicSize(document,position,image);
            // Small web images (README badges) retain readable intrinsic dimensions.
            const QUrl url(image.name());
            if((url.scheme()=="https"||url.scheme()=="http")&&size.height()<=32){
                image.setVerticalAlignment(QTextCharFormat::AlignMiddle);cursor.setCharFormat(image);continue;
            }
            if(size.height()<=ascent)continue;
            image.setWidth(size.width()*ascent/size.height());
            image.setHeight(ascent);
            cursor.setCharFormat(image);
        }
    }


// The preview occupies reserved space immediately below the editable source.
void LiveEditor::syncMathPreview(){
    if(!mathPreviewPanel)return;
    if(!mathPreviewVisible||active<0||!isVisible()){
        mathPreviewPanel->hide();return;
    }
    const auto block=document()->findBlock(regions[active].end);
    const QRectF rect=document()->documentLayout()->blockBoundingRect(block);
    mathPreviewPanel->move(16-horizontalScrollBar()->value(),qRound(rect.bottom())+8-verticalScrollBar()->value());
    mathPreviewPanel->setVisible(mathPreviewPanel->geometry().intersects(viewport()->rect()));
    mathPreviewPanel->raise();
}
