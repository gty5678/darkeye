#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>

#include "../view/pdf_preview_panel.h"

QString LiveEditor::clickableAnchorAt(const QPoint &point) const{
        const int index=regionAt(cursorForPosition(point).position());
        if(index>=0){
            const auto &region=regions[index];
            const auto nodes=mdedit::MarkdownParser::parse(source.mid(region.sourceStart,region.sourceEnd-region.sourceStart));
            if(nodes.size()==1&&nodes.first().resource){
                const QUrl file=mdedit::ResourceResolver::resolve(nodes.first().resource->uri,document()->baseUrl());
                const auto block=document()->findBlock(region.start);
                const auto rect=document()->documentLayout()->blockBoundingRect(block).translated(-horizontalScrollBar()->value(),-verticalScrollBar()->value());
                if(!mdedit::ResourceResolver::isPreviewResource(file)&&!block.text().contains("![")&&rect.contains(point)){
                    QUrl action("mdedit-pdf:/open");QUrlQuery query;
                    query.addQueryItem("file",file.toString());action.setQuery(query);return action.toString();
                }
            }
        }
        const QString anchor=anchorAt(point);
        const QUrl url=normalizedWebLink(QUrl(anchor));
        if(url.scheme()=="mdedit-pdf"||url.scheme()=="mdedit-wiki"||url.isLocalFile())return anchor;
        if((url.isRelative()&&anchor.startsWith('#')&&!url.fragment().isEmpty())||
           ((url.scheme()=="http"||url.scheme()=="https")&&url.isValid()&&!url.host().isEmpty()))
            return anchor;
        return {};
    }

QList<LiveEditor::BlockControls> LiveEditor::blockControls() const{
    QList<BlockControls> result;
    if(sourceExposed)return result;
    const QFontMetricsF metrics(font());
    const qreal width=qBound(24.0,metrics.height()+4,30.0);
    const qreal height=width;
    auto *layout=document()->documentLayout();
    for(int i=0;i<regions.size();++i){
        const auto &r=regions[i];
        if(r.inlinePart)continue;
        const QString raw=source.mid(r.sourceStart,r.sourceEnd-r.sourceStart);
        const auto nodes=mdedit::MarkdownParser::parse(raw);
        const auto resource=nodes.size()==1?nodes.first().resource:std::nullopt;
        if(!resource)continue;
        const auto first=document()->findBlock(r.start);
        if(!first.isValid())continue;
        const QRectF bounds=layout->blockBoundingRect(first);
        const qreal y=bounds.top()-verticalScrollBar()->value();
        if(y+height<0||y>viewport()->height())continue;
        const qreal right=qMin(qreal(viewport()->width()-8),bounds.right()-horizontalScrollBar()->value());
        result.append({i,QRectF(right-2*width-6,y,width,height),QRectF(right-width,y,width,height),
            resource?mdedit::ResourceResolver::resolve(resource->uri,document()->baseUrl()):QUrl()});
    }
    return result;
}

QList<LiveEditor::CodeCopyControl> LiveEditor::codeCopyControls() const{
    QList<CodeCopyControl> result;
    if(sourceExposed)return result;
    const QFontMetricsF metrics(font());
    const qreal width=qBound(24.0,metrics.height()+4,30.0);
    const qreal height=width;
    auto *layout=document()->documentLayout();
    for(int i=0;i<regions.size();++i){
        const auto &region=regions[i];
        if(!region.codeBlock)continue;
        const auto first=document()->findBlock(region.start);
        if(!first.isValid())continue;
        const auto bounds=layout->blockBoundingRect(first);
        const qreal top=bounds.top()-verticalScrollBar()->value();
        if(top+height<0||top>viewport()->height())continue;
        const qreal right=qMin(qreal(viewport()->width()-8),bounds.right()-horizontalScrollBar()->value()-6);
        result.append({i,QRectF(right-width,top,width,height)});
    }
    return result;
}

void LiveEditor::handlePdfAction(const QUrl &action){
    const QUrl file(QUrlQuery(action).queryItemValue("file",QUrl::FullyDecoded));
    if(!file.isLocalFile())return;
    if(action.path()=="/open"){QDesktopServices::openUrl(file);return;}
}

bool LiveEditor::editResourceSourceAt(int sourceOffset){
    for(int i=0;i<regions.size();++i){
        if(sourceOffset>=regions[i].sourceStart&&sourceOffset<regions[i].sourceEnd&&isResourceRegion(i)){
            resourceSourceEditStart=regions[i].sourceStart;rebuild(i,0);return true;
        }
    }
    return false;
}

void LiveEditor::syncPdfViews(){
#if !MDEDIT_LITE
    QList<mdedit::PdfPreviewPlacement> placements;
    if(!sourceExposed)for(auto block=document()->begin();block.isValid();block=block.next()){
        const auto bounds=document()->documentLayout()->blockBoundingRect(block).translated(-horizontalScrollBar()->value(),-verticalScrollBar()->value());
        for(auto it=block.begin();!it.atEnd();++it){
            const auto fragment=it.fragment();const auto format=fragment.charFormat();
            if(!format.isImageFormat()||!format.hasProperty(QTextFormat::UserProperty+82))continue;
            const auto image=format.toImageFormat();const QUrl file=format.property(QTextFormat::UserProperty+82).toUrl();
            const QString key=QString::number(sourcePosition(fragment.position()))+"|"+file.toString();
            QTextCursor cursor(document());cursor.setPosition(fragment.position());
            const QRect geometry=QRectF(cursorRect(cursor).left()+12,bounds.top()+42,image.width()-24,image.height()-54).toAlignedRect();
            if(!geometry.intersects(viewport()->rect().adjusted(0,-viewport()->height(),0,viewport()->height())))continue;
            const auto *snapshot=resourceManager.lookup(resourceManager.request(file));
            placements.append({key,file,geometry,snapshot&&snapshot->error.isEmpty()&&file.isLocalFile()});
        }
    }
    pdfViews.sync(this,placements);
#endif
}
