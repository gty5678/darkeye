#include "renderer_registry.h"
namespace mdedit {
namespace {
class ImageRenderer final:public IResourceRenderer {
public:
    bool supports(const ResourceSnapshot &data) const override{return !data.image.isNull();}
    void paint(QPainter &painter,const ResourceSnapshot &data,const QRectF &content, const std::optional<QPalette> &) const override {
        QSizeF size=data.image.size();size.scale(content.size(),Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(content.center()-QPointF(size.width()/2,size.height()/2),size),data.image);
    }
};
class TextRenderer final:public IResourceRenderer {
public:
    bool supports(const ResourceSnapshot &data) const override{return data.text.has_value();}
    void paint(QPainter &painter,const ResourceSnapshot &data,const QRectF &content, const std::optional<QPalette> &theme) const override {
        painter.save();painter.setClipRect(content,Qt::IntersectClip);
        QFont textFont=QFontDatabase::systemFont(QFontDatabase::FixedFont);
        textFont.setPointSizeF(painter.font().pointSizeF());
        painter.setFont(textFont);
        painter.setPen(themeColor(theme,QPalette::Text,QColor("#26232d")));
        const QFontMetricsF metrics(painter.font());
        const auto lines=data.text->split('\n');
        const int available=qMax(0,int((content.height()-24)/metrics.lineSpacing()));
        const int count=qMin(available,int(lines.size()));
        bool clipped=data.textTruncated||count<lines.size();
        for(int i=0;i<count;++i){
            QString line=lines[i];line.replace('\t',"    ");
            clipped|=metrics.horizontalAdvance(line)>content.width();
            painter.drawText(QPointF(content.left(),content.top()+metrics.ascent()+i*metrics.lineSpacing()),metrics.elidedText(line,Qt::ElideRight,content.width()));
        }
        painter.setPen(themeColor(theme,QPalette::Text,QColor("#746980")));
        if(data.text->isEmpty())painter.drawText(content,Qt::AlignCenter,"空文本文件");
        else if(clipped)painter.drawText(content.adjusted(0,0,0,-2),Qt::AlignBottom|Qt::AlignLeft,"仅显示部分内容，请打开原文件查看全文");
        painter.restore();
    }
};
class TableRenderer final:public IResourceRenderer {
public:
    bool supports(const ResourceSnapshot &data) const override{return !data.rows.isEmpty();}
    void paint(QPainter &painter,const ResourceSnapshot &data,const QRectF &content, const std::optional<QPalette> &theme) const override {
        const int rows=qMin(8,int(data.rows.size()));int cols=1;
        for(const auto &row:data.rows)cols=qMax(cols,qMin(8,int(row.size())));
        for(int r=0;r<rows;++r)for(int c=0;c<cols;++c){
            QRectF cell(content.left()+c*content.width()/cols,content.top()+r*24,content.width()/cols,24);
            painter.setPen(themeColor(theme,QPalette::Mid,QColor("#ded7e6")));painter.drawRect(cell);painter.setPen(themeColor(theme,QPalette::Text,QColor("#26232d")));
            painter.save();painter.setClipRect(cell,Qt::IntersectClip);
            painter.drawText(cell.adjusted(4,0,-4,0),Qt::AlignVCenter|Qt::TextSingleLine,data.rows[r].value(c));painter.restore();
        }
    }
};
}
void RendererRegistry::paint(QPainter &painter,const ResourceSnapshot &data,const QRectF &content, const std::optional<QPalette> &theme){
    if(!data.error.isEmpty()){painter.drawText(content,Qt::TextWordWrap,data.error);return;}
    static const ImageRenderer image;static const TableRenderer table;static const TextRenderer text;
    const IResourceRenderer *renderers[]={&image,&table,&text};
    for(const auto *renderer:renderers)if(renderer->supports(data)){renderer->paint(painter,data,content,theme);return;}
    painter.drawText(content,Qt::AlignCenter,"资源没有可显示的内容");
}
}
