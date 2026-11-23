#include "../live_editor.h"
#include "../../markdown/scanner.h"
#include "../../markdown/renderer.h"
#include <limits>

#include "../render/renderer_registry.h"

namespace {
// All external image I/O goes through the same asynchronous cache as PDF/CSV.
class ResourceTextDocument final : public QTextDocument {
    mdedit::ResourceManager &resources;
protected:
    QVariant loadResource(int type,const QUrl &url) override {
        if(type!=QTextDocument::ImageResource)return {};
        const auto *data=resources.lookup(resources.request(baseUrl().resolved(url)));
        return data&&!data->image.isNull()?QVariant(data->image):QVariant();
    }
public:
    explicit ResourceTextDocument(mdedit::ResourceManager &manager):resources(manager){}
};
}

const QTextDocument *LiveEditor::renderedDocument(const QString &text,bool joinedParagraph,bool inlinePart){
        // Inline pieces and paragraphs use different base URLs and block formatting.
        const QString key=(inlinePart?"i":joinedParagraph?"j":"p")+text;
        if(auto *cached=renderedMarkdown.object(key))return cached;
        if(text.contains("!["))resourceRenderKeys.insert(key);
        auto *rendered=new ResourceTextDocument(resourceManager);
        // Import and format the entire fragment before laying it out. Formatting
        // individual table cells can otherwise relayout the table for every cell.
        rendered->setLayoutEnabled(false);
        rendered->setDefaultFont(renderedFont);
        rendered->setBaseUrl(renderedBaseUrl);
        QStringList errors;
        const auto nodes=mdedit::MarkdownParser::parse(text);
        const auto resource=nodes.size()==1?nodes.first().resource:std::nullopt;
        const QUrl resolved=resource?mdedit::ResourceResolver::resolve(resource->uri,renderedBaseUrl):QUrl();
        const bool pdf=QFileInfo(resolved.path()).suffix().compare("pdf",Qt::CaseInsensitive)==0;
        const bool csv=QFileInfo(resolved.path()).suffix().compare("csv",Qt::CaseInsensitive)==0;
        if(!inlinePart&&resource&&!mdedit::ResourceResolver::isPreviewResource(resolved)){
            QUrl action("mdedit-pdf:/open");QUrlQuery query;
            query.addQueryItem("file",resolved.toString());action.setQuery(query);
            QTextCursor cursor(rendered);
            QTextBlockFormat block;block.setBackground(mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#f5f2f8")));
            block.setLineHeight(QFontMetricsF(font()).height()+12,QTextBlockFormat::FixedHeight);
            cursor.setBlockFormat(block);
            QTextCharFormat link;link.setAnchor(true);link.setAnchorHref(action.toString());
            link.setForeground(mdedit::themeColor(theme,QPalette::Link,QColor("#655080")));link.setToolTip(QFileInfo(resolved.path()).fileName());
            const QString label=QStringLiteral("  📄  ")+QFileInfo(resolved.path()).fileName();
            cursor.insertText(QFontMetricsF(font()).elidedText(label,Qt::ElideMiddle,qMax(40,viewport()->width()-40)),link);
        }else if(!inlinePart&&resource){
            const QString resourceKey=resourceManager.request(resolved);
            const auto *snapshot=resourceManager.lookup(resourceKey);
            const QString state=!snapshot?"loading":snapshot->error.isEmpty()?"ready":"error";
            const bool textResource=mdedit::ResourceResolver::isTextResource(resolved);
            const bool picture=!pdf&&!csv&&!textResource;
            int width=qBound(320,viewport()->width()-80,960);
            int height=pdf?480:8*24+70;
            if(picture){
                const QSize size=snapshot&&!snapshot->image.isNull()?snapshot->image.size():resourceManager.imageSize(resolved);
                width=resource->width>0?resource->width:qMin(size.width(),width);
                height=qBound(1.0,double(size.height())*width/size.width(),double(std::numeric_limits<int>::max()/4));
            } // Loading and ready snapshots share geometry.
            const bool resident=resourceManager.isActive(resolved)&&activeResourceTexts.contains(text.trimmed());
            const QSize raster=resident?QSize(qMax(1,qCeil(width*devicePixelRatioF())),qMax(1,qCeil(height*devicePixelRatioF()))).boundedTo(QSize(1920,1920)):QSize(1,1);
            QImage image(raster,QImage::Format_ARGB32_Premultiplied);image.fill(picture?Qt::transparent:mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#f5f2f8")));
            QPainter painter(&image);painter.scale(double(raster.width())/width,double(raster.height())/height);painter.setFont(font());painter.setPen(mdedit::themeColor(theme,QPalette::Text,QColor("#524960")));
            painter.save();
            QFont titleFont=font();titleFont.setBold(true);painter.setFont(titleFont);
            const QString title=resource->alt.trimmed().isEmpty()?(pdf?QStringLiteral("PDF 文档"):csv?QStringLiteral("CSV 表格"):QFileInfo(resolved.path()).fileName()):resource->alt;
            if(!picture)painter.drawText(QRectF(12,8,width-24,28),Qt::AlignVCenter|Qt::TextSingleLine,
                QFontMetricsF(titleFont).elidedText(title,Qt::ElideRight,width-24));
            painter.restore();
            const QRectF content=picture?QRectF(0,0,width,height):QRectF(12,42,width-24,height-54);
            if(snapshot)mdedit::RendererRegistry::paint(painter,*snapshot,content,theme);
            else painter.drawText(content,Qt::AlignCenter,pdf?"PDF 正在加载…":csv?"CSV 正在加载…":textResource?"文本正在加载…":"图片正在加载…");
            painter.end();
            const QUrl imageUrl((pdf?"pdf-":csv?"csv-":textResource?"text-":"image-")+state+":/"+QString::number(qHash(resourceKey+text+(resident?"resident":"placeholder"))));
            rendered->addResource(QTextDocument::ImageResource,imageUrl,image);
            QTextImageFormat format;format.setName(imageUrl.toString());format.setWidth(width);format.setHeight(height);
            if(pdf)format.setProperty(QTextFormat::UserProperty+82,resolved);
            QTextCursor cursor(rendered);cursor.insertImage(format);
            if(textResource){
                cursor.insertBlock();
                auto link=[&](const QString &label,const QString &command){
                    QUrl action("mdedit-pdf:/"+command);QUrlQuery query;query.addQueryItem("file",resolved.toString());action.setQuery(query);
                    QTextCharFormat f;f.setAnchor(true);f.setAnchorHref(action.toString());f.setForeground(mdedit::themeColor(theme,QPalette::Link,QColor("#655080")));f.setFontUnderline(true);
                    cursor.insertText(label,f);cursor.insertText("   ",QTextCharFormat());
                };


            }
        }else renderMarkdown(rendered,text,errors,true,theme);
        QSet<QTextTable *> sizedTables;
        for(auto block=rendered->begin();block.isValid();block=block.next()){
            if(auto *table=QTextCursor(block).currentTable();table&&!sizedTables.contains(table)){
                sizedTables.insert(table);
                // Qt imports consecutive empty Markdown cells as a colspan.
                // Native Markdown tables have one independent cell per column.
                if(nodes.size()==1&&nodes.first().kind==mdedit::BlockKind::Table)
                    for(int row=0;row<table->rows();++row)for(int column=0;column<table->columns();++column){
                        const auto cell=table->cellAt(row,column);
                        if(cell.rowSpan()>1||cell.columnSpan()>1)table->splitCell(row,column,1,1);
                    }
                layoutNativeTable(table);
            }
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();auto image=fragment.charFormat().toImageFormat();
                if(!image.isValid()||image.hasProperty(formulaPaintAscent))continue;
                const auto match=QRegularExpression(R"(\|([1-9][0-9]*)$)").match(image.stringProperty(QTextFormat::ImageAltText));
                bool valid=false;const int width=match.captured(1).toInt(&valid);
                const QUrl imageUrl(image.name());
                const bool external=imageUrl.isRelative()||imageUrl.isLocalFile()||imageUrl.scheme()=="http"||imageUrl.scheme()=="https";
                if(!valid&&!external)continue;
                const QImage pixels=qvariant_cast<QImage>(rendered->resource(QTextDocument::ImageResource,QUrl(image.name())));
                const QSize size=external?resourceManager.imageSize(renderedBaseUrl.resolved(imageUrl)):pixels.size();
                const int displayWidth=valid?width:size.width();
                image.setWidth(displayWidth);
                if(size.isValid())image.setHeight(double(size.height())*displayWidth/size.width());
                QTextCursor cursor(rendered);cursor.setPosition(fragment.position());cursor.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);cursor.setCharFormat(image);
            }
            auto format=block.blockFormat();
            if(QTextCursor(block).currentTable())format.setAlignment(Qt::AlignLeft);
            if(!isCodeBlock(format)&&!format.alignment().testFlag(Qt::AlignHCenter)&&!isStandaloneImage(block)){
                if(format.headingLevel()){
                    QTextBlockFormat heading;
                    headingStyle(format.headingLevel(),renderedFont,heading);
                    format.setLineHeight(heading.lineHeight(),QTextBlockFormat::FixedHeight);
                }else if(QTextCursor(block).currentTable()){
                    format.setTopMargin(0);format.setBottomMargin(0);
                    format.setLineHeight(0,QTextBlockFormat::SingleHeight);
                }else setBodyLineHeight(format,renderedFont);
                if(block.text().contains(QChar::ObjectReplacementCharacter))fitInlineImages(block,rendered);
            }
            if(joinedParagraph){
                if(block.previous().isValid())format.setTopMargin(0);
                if(block.next().isValid())format.setBottomMargin(0);
            }
            QTextCursor(block).setBlockFormat(format);
        }
        rendered->setLayoutEnabled(true);
        renderedMarkdown.insert(key,rendered);
        return rendered;
    }

