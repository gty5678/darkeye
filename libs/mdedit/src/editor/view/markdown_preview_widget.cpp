#include "markdown_preview_widget.h"
#include "../../markdown/renderer.h"
#include "../render/renderer_registry.h"
namespace mdedit {
namespace {
class PreviewTextDocument final:public QTextDocument {
protected:
    QVariant loadResource(int type,const QUrl &url) override {
        if(type!=QTextDocument::ImageResource)return {};
        const auto *data=resources.lookup(resources.request(baseUrl().resolved(url)));
        return data&&!data->image.isNull()?QVariant(data->image):QVariant();
    }
public:
    ResourceManager &resources;
    explicit PreviewTextDocument(ResourceManager &manager):resources(manager){}
    void refreshImages(){
        for(auto block=begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
            const auto format=it.fragment().charFormat();
            if(!format.isImageFormat())continue;
            const QUrl url(format.toImageFormat().name());
            const auto *data=resources.lookup(resources.request(baseUrl().resolved(url)));
            if(data&&!data->image.isNull())addResource(ImageResource,url,data->image);
        }
        markContentsDirty(0,characterCount());
    }
};
}

MarkdownPreviewWidget::MarkdownPreviewWidget(QWidget *parent):QAbstractScrollArea(parent),resources(this) {
    resources.setActiveUrls({});
    windowTimer.setSingleShot(true);windowTimer.setInterval(35);
    connect(&windowTimer,&QTimer::timeout,this,&MarkdownPreviewWidget::updateResourceWindow);
    connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{windowTimer.start();});
    setObjectName("resourcePreview");setFrameShape(QFrame::NoFrame);
    connect(&resources,&ResourceManager::ready,this,[this]{relayout();});
    connect(verticalScrollBar(),&QScrollBar::valueChanged,viewport(),qOverload<>(&QWidget::update));
}
void MarkdownPreviewWidget::setSource(const QString &text,const QUrl &baseUrl) {
    if(source==text&&base==baseUrl&&!entries.empty())return;
    source=text;base=baseUrl;resources.setActiveUrls({});resources.clear();buildEntries();
}
void MarkdownPreviewWidget::buildEntries(){
    entries.clear();
    for(const auto &block:MarkdownParser::parse(source)) {
        Entry entry;entry.block=block;
        if(!block.resource){
            entry.text=std::make_unique<PreviewTextDocument>(resources);
            entry.text->setDefaultFont(font());entry.text->setDocumentMargin(0);
            QString fragment=source.mid(block.source.begin,block.source.end-block.source.begin);
            entry.text->setBaseUrl(base);
            entry.text->setMarkdown(markdownWithoutSetext(fragment),QTextDocument::MarkdownDialectGitHub);
            restoreImageLinks(entry.text.get(),fragment);
        }
        entries.push_back(std::move(entry));
    }
    relayout();
}
QString MarkdownPreviewWidget::resourceKey(const Entry &entry) {
    return resources.request(ResourceResolver::resolve(entry.block.resource->uri,base));
}
void MarkdownPreviewWidget::relayout() {
    qreal y=24;const qreal width=qMax(80,viewport()->width()-48);
    for(auto &entry:entries){
        entry.top=y;
        if(entry.text){static_cast<PreviewTextDocument *>(entry.text.get())->refreshImages();entry.text->setTextWidth(width);entry.height=entry.text->size().height();}
        else {
            const auto &ref=*entry.block.resource;
            if(!ResourceResolver::isPreviewResource(ResourceResolver::resolve(ref.uri,base))){
                entry.height=QFontMetricsF(font()).height()+12;
                y+=entry.height+18;continue;
            }
            entry.height=260;
            const QString suffix=QFileInfo(QUrl(ref.uri).path()).suffix().toLower();
            if(suffix!="pdf"&&suffix!="csv"&&!ResourceResolver::isTextResource(QUrl(ref.uri))){
                const auto *data=resources.lookup(resourceKey(entry));
                const QSize size=data&&!data->image.isNull()?data->image.size():resources.imageSize(ResourceResolver::resolve(ref.uri,base));
                {
                    const qreal w=ref.width>0?ref.width:qMin(width,qreal(size.width()));
                    entry.height=qBound(1.0,w*size.height()/size.width(),1000000.0);
                }
            }
        }
        y+=entry.height+18;
    }
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0,qMax(0,int(y+24-viewport()->height())));viewport()->update();windowTimer.start();
}
void MarkdownPreviewWidget::refreshAppearance(){for(auto &entry:entries)if(entry.text)entry.text->setDefaultFont(font());relayout();}
void MarkdownPreviewWidget::refreshResources(){resources.clear();relayout();}
void MarkdownPreviewWidget::resizeEvent(QResizeEvent *event){QAbstractScrollArea::resizeEvent(event);relayout();}
void MarkdownPreviewWidget::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());painter.fillRect(viewport()->rect(),Qt::white);
    const int scroll=verticalScrollBar()->value();
    auto start=std::lower_bound(entries.begin(),entries.end(),scroll,[](const Entry &e,int y){return e.top+e.height<y;});
    for(auto it=start;it!=entries.end()&&it->top<scroll+viewport()->height();++it){
        auto &entry=*it;painter.save();painter.translate(24,entry.top-scroll);
        QRectF box(0,0,qMax(80,viewport()->width()-48),entry.height);painter.setClipRect(box);
        if(entry.text)entry.text->drawContents(&painter,box);
        else {
            painter.fillRect(box,QColor("#f5f2f8"));painter.setPen(QColor("#524960"));
            const auto &ref=*entry.block.resource;
            const QUrl file=ResourceResolver::resolve(ref.uri,base);
            if(!ResourceResolver::isPreviewResource(file)){
                painter.setPen(QColor("#655080"));
                painter.drawText(box.adjusted(10,0,-10,0),Qt::AlignVCenter|Qt::TextSingleLine,
                    painter.fontMetrics().elidedText(QStringLiteral("📄  ")+QFileInfo(file.path()).fileName(),Qt::ElideMiddle,int(box.width()-20)));
                painter.restore();continue;
            }
            const QString suffix=QFileInfo(QUrl(ref.uri).path()).suffix().toLower();
            const bool csv=suffix=="csv",picture=!csv&&suffix!="pdf"&&!ResourceResolver::isTextResource(QUrl(ref.uri));
            painter.save();
            if(csv){auto titleFont=painter.font();titleFont.setBold(true);painter.setFont(titleFont);}
            const QString title=csv?(ref.alt.trimmed().isEmpty()?QStringLiteral("CSV 表格"):ref.alt):ref.alt+" · "+ref.uri;
            if(!picture)painter.drawText(box.adjusted(12,8,-12,0),Qt::AlignTop|Qt::TextSingleLine,
                painter.fontMetrics().elidedText(title,Qt::ElideRight,qMax(1,int(box.width()-24))));
            painter.restore();
            const auto *data=resources.lookup(resourceKey(entry));
            QRectF content=box.adjusted(12,36,-12,-12);
            if(picture){
                const qreal width=ref.width>0?ref.width:data&&!data->image.isNull()?qMin(box.width(),qreal(data->image.width())):box.width();
                content=QRectF(0,0,width,entry.height);
            }
            if(!data)painter.drawText(content,Qt::AlignCenter,"正在加载…");
            else RendererRegistry::paint(painter,*data,content);
        }
        painter.restore();
    }
}
void MarkdownPreviewWidget::mousePressEvent(QMouseEvent *event){
    const qreal y=event->position().y()+verticalScrollBar()->value();
    for(const auto &entry:entries)if(y>=entry.top&&y<entry.top+entry.height){
        if(entry.text&&event->button()==Qt::LeftButton){
            const QString anchor=entry.text->documentLayout()->anchorAt(QPointF(event->position().x()-24,y-entry.top));
            const QUrl url=base.resolved(normalizedWebLink(QUrl(anchor)));
            if(!anchor.isEmpty()&&(url.scheme()=="https"||url.scheme()=="http")){
                QDesktopServices::openUrl(url);event->accept();return;
            }
        }
        if(entry.block.resource){
            const QUrl file=ResourceResolver::resolve(entry.block.resource->uri,base);
            if(!ResourceResolver::isPreviewResource(file)&&file.isLocalFile()&&event->button()==Qt::LeftButton){QDesktopServices::openUrl(file);event->accept();return;}
        }
        emit sourceActivated(entry.block.source.begin);break;
    }
    QAbstractScrollArea::mousePressEvent(event);
}
}

void mdedit::MarkdownPreviewWidget::updateResourceWindow(){
    QSet<QUrl> urls;
    const qreal top=verticalScrollBar()->value()-viewport()->height();
    const qreal bottom=verticalScrollBar()->value()+2*viewport()->height();
    for(const auto &entry:entries){
        if(entry.top+entry.height<top||entry.top>bottom)continue;
        urls.unite(ResourceManager::referencedUrls(source.mid(entry.block.source.begin,entry.block.source.end-entry.block.source.begin),base));
    }
    if(resources.setActiveUrls(urls)){
        const int scroll=verticalScrollBar()->value();buildEntries();verticalScrollBar()->setValue(scroll);
    }
}
