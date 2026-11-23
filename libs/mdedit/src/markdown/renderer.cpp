#include "renderer.h"
#include "scanner.h"
#include "../platform.h"
#if !MDEDIT_LITE
#include "latex.h"
#include "core/formula.h"
#include "platform/qt/graphic_qt.h"
#endif
#include <memory>
#include <QPicture>

// Markdown destinations may omit the scheme for a www address.
QUrl normalizedWebLink(const QUrl &url){
    if(!url.isRelative())return url;
    QString address=url.toString().replace("\\.",".");
    if(address.startsWith("www.",Qt::CaseInsensitive))return QUrl("https://"+address);
    return url;
}

// Color source syntax without turning editable text into clickable anchors.
QList<InlineEditingStyle> linkEditingStyles(const QString &source, const std::optional<QPalette> &theme){
    QList<InlineEditingStyle> styles;
    const auto spans=scanSpans(source);
    static const QRegularExpression links(R"((?<![!\\])\[(?:\\.|[^\]\n])+\]\((?:\\.|[^()\n]|\([^()\n]*\))*\)|(?<![!\\])\[[^\]\n]+\]\[[^\]\n]*\]|<(?:(?:https?://|mailto:)[^<>\s]+)>|https?://[^\s<>\[\]]+)");
    auto matches=links.globalMatch(source);
    while(matches.hasNext()){
        const auto match=matches.next();
        if(escaped(source,match.capturedStart()))continue;
        const bool protectedText=std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
            return match.capturedStart()<span.start+span.length&&match.capturedEnd()>span.start;
        });
        if(protectedText)continue;
        QTextCharFormat format;format.setForeground(mdedit::themeColor(theme,QPalette::Link,QColor("#2468b4")));
        styles.append({int(match.capturedStart()),int(match.capturedLength()),format});
    }
    return styles;
}

// Editing deliberately accepts unfinished delimiters, bounded by a source line.
// Source positions and visible delimiters stay intact; only formats change.
QList<InlineEditingStyle> inlineEditingStyles(const QString &source, const std::optional<QPalette> &theme){
    QList<InlineEditingStyle> styles;
    struct Open { QString delimiter; int begin; QTextCharFormat format; };
    QList<Open> open;
    const auto finish=[&](int end){
        for(const auto &entry:open)if(end>entry.begin)styles.append({entry.begin,end-entry.begin,entry.format});
        open.clear();
    };
    const auto spans=scanSpans(source);
    int protectedIndex=0;
    for(int i=0;i<source.size();){
        if(source[i]=='\n'){finish(i);++i;continue;}
        while(protectedIndex<spans.size()&&spans[protectedIndex].start+spans[protectedIndex].length<=i)++protectedIndex;
        if(protectedIndex<spans.size()&&spans[protectedIndex].start<=i){
            const auto &span=spans[protectedIndex];
            const int end=span.start+span.length;
            if(span.kind==SpanKind::Code&&!span.fenced){
                QTextCharFormat code;code.setFontFamilies({QString::fromLatin1(editorFont)});
                styles.append({span.start,span.length,code});
            }
            // A protected multiline span also ends any unfinished line style.
            if(QStringView(source).mid(i,end-i).contains('\n'))finish(i);
            i=end;continue;
        }
        if(escaped(source,i)){++i;continue;}
        const QChar ch=source[i];
        if(ch!='*'&&ch!='_'&&ch!='~'&&ch!='='&&ch!='`'){++i;continue;}
        int run=1;while(i+run<source.size()&&source[i+run]==ch)++run;
        const bool emphasis=ch=='*'||ch=='_';
        if((emphasis&&run>3)||((ch=='~'||ch=='=')&&run!=2)){i+=run;continue;}
        if(ch=='_'&&i>0&&i+run<source.size()&&source[i-1].isLetterOrNumber()&&source[i+run].isLetterOrNumber()){
            i+=run;continue;
        }
        const QString delimiter=source.mid(i,run);
        int matching=-1;
        for(int n=open.size()-1;n>=0;--n)if(open[n].delimiter==delimiter){matching=n;break;}
        if(matching>=0){
            const auto entry=open.takeAt(matching);
            if(i>entry.begin)styles.append({entry.begin,i-entry.begin,entry.format});
        }else if(i+run==source.size()||source[i+run]=='\n'||!source[i+run].isSpace()){
            QTextCharFormat format;
            if(emphasis){if(run>=2)format.setFontWeight(QFont::Bold);if(run!=2)format.setFontItalic(true);}
            else if(ch=='~')format.setFontStrikeOut(true);
            else if(ch=='=')format.setBackground(mdedit::themeColor(theme,QPalette::Light,highlightBackground));
            else format.setFontFamilies({QString::fromLatin1(editorFont)});
            open.append({delimiter,i+run,format});
            if(ch=='`'){
                int end=source.indexOf('\n',i+run);if(end<0)end=source.size();
                finish(end);i=end;continue;
            }
        }
        i+=run;
    }
    finish(source.size());
    styles.append(linkEditingStyles(source,theme));
    return styles;
}

qreal bodyLineHeight(const QFont &font){
    return qCeil(QFontMetricsF(font).lineSpacing()*1.5);
}

void setBodyLineHeight(QTextBlockFormat &format,const QFont &font){
    // Source blank lines already provide paragraph spacing in the live
    // editor. Markdown's default paragraph margins would count it twice.
    format.setTopMargin(0);
    format.setBottomMargin(0);
    format.setLineHeight(bodyLineHeight(font),QTextBlockFormat::FixedHeight);
}

bool isCodeBlock(const QTextBlockFormat &format){
    return format.hasProperty(QTextFormat::BlockCodeLanguage)||format.hasProperty(QTextFormat::BlockCodeFence);
}

class FormulaImageHandler : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
    QTextObjectInterface *imageHandler;
public:
    explicit FormulaImageHandler(QAbstractTextDocumentLayout *layout)
        :QObject(layout),imageHandler(layout->handlerForObject(QTextFormat::ImageObject)){
        layout->registerHandler(QTextFormat::ImageObject,this);
    }
    QSizeF intrinsicSize(QTextDocument *document,int pos,const QTextFormat &format)override{
        return imageHandler->intrinsicSize(document,pos,format);
    }
    void drawObject(QPainter *painter,const QRectF &rect,QTextDocument *document,int pos,const QTextFormat &format)override{
        QRectF painted=rect;
        if(format.hasProperty(formulaPaintAscent)){
            painted.moveTop(rect.bottom()-format.doubleProperty(formulaPaintAscent));
            painted.setHeight(format.doubleProperty(formulaPaintHeight));
        }
        if(format.hasProperty(formulaVector)){
            const QPicture picture=format.property(formulaVector).value<QPicture>();
            const QSizeF size=format.property(formulaVectorSize).toSizeF();
            painter->save();
            painter->translate(painted.topLeft());
            // QPicture playback adjusts for device DPI; our geometry is
            // already in the document's logical coordinates, so cancel it.
            painter->scale(painted.width()/size.width()*picture.logicalDpiX()/painter->device()->logicalDpiX(),
                           painted.height()/size.height()*picture.logicalDpiY()/painter->device()->logicalDpiY());
            painter->drawPicture(QPointF(0,0),picture);
            painter->restore();
            return;
        }
        imageHandler->drawObject(painter,painted,document,pos,format);
    }
};

void installFormulaImageHandler(QTextDocument *document){
    new FormulaImageHandler(document->documentLayout());
}

static QString protectMath(const QString &source, QList<QPair<QString,QString>> &math) {
#if MDEDIT_LITE
    Q_UNUSED(math);
    return source;
#else
    QString result;int end=0;
    for(const auto &span:scanSpans(source)){
        if(span.kind==SpanKind::Code)continue;
        result+=source.mid(end,span.start-end);
        QString token="MDEDITMATH"+QString::number(math.size())+"TOKEN";while(source.contains(token))token+="X";
        math.append({token,source.mid(span.start,span.length)});
        // A display formula gets its own paragraph even when typed beside text.
        result+=(span.kind==SpanKind::DisplayMath?"\n\n"+token+"\n\n":token);
        end=span.start+span.length;
    }
    return result+source.mid(end);
#endif
}

// Adapt only parser input; the saved source and cursor offsets stay untouched.
QString markdownWithoutSetext(const QString &source){
    const auto spans=scanSpans(source);
    static const QRegularExpression underline(R"(^((?: {0,3}>[ \t]?)* {0,3})(-+|=+)[ \t]*$)");
    QStringList lines=source.split('\n');
    int offset=0;
    for(auto &line:lines){
        const int length=line.size();
        const auto match=underline.match(line);
        if(match.hasMatch()){
            const int marker=offset+match.capturedStart(2);
            const bool protectedText=std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
                return marker>=span.start&&marker<span.start+span.length;
            });
            if(!protectedText){
                const QString run=match.captured(2);
                // Spaced dashes remain a thematic break, never a heading.
                if(run[0]=='-'&&run.size()>=3)
                    line.replace(match.capturedStart(2),run.size(),"- - -");
                else line.insert(match.capturedStart(2),'\\');
            }
        }
        offset+=length+1;
    }
    return lines.join('\n');
}

// Convert wiki links before Qt parses Markdown; code and math remain literal.
static QString expandWikiLinks(const QString &source){
    const auto spans=scanSpans(source);
    static const QRegularExpression pattern(R"((?<![!\\])\[\[([^\[\]\n]+)\]\])");
    QString result=source;
    auto matches=pattern.globalMatch(source);
    QList<QRegularExpressionMatch> links;
    while(matches.hasNext())links.append(matches.next());
    for(auto it=links.crbegin();it!=links.crend();++it){
        const auto &m=*it;
        if(escaped(source,m.capturedStart()))continue;
        if(std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
            return m.capturedStart()<span.start+span.length&&m.capturedEnd()>span.start;
        }))continue;
        const QString body=m.captured(1);
        const int separator=body.indexOf('|');
        const QString target=(separator<0?body:body.left(separator)).trimmed();
        if(target.isEmpty())continue;
        QString label=separator<0?target:body.mid(separator+1).trimmed();
        if(label.isEmpty())label=target;
        for(const QChar ch:QString("\\`*_{}[]<>"))label.replace(QString(ch),"\\"+QString(ch));
        const QString href="mdedit-wiki:/"+QString::fromLatin1(QUrl::toPercentEncoding(target));
        result.replace(m.capturedStart(),m.capturedLength(),"["+label+"]("+href+")");
    }
    return result;
}

static QString preserveLineBreaks(const QString &source){
    const auto spans=scanSpans(source);
    auto span=spans.cbegin();
    QString result;
    int start=0;
    for(int end=source.indexOf('\n');end>=0;end=source.indexOf('\n',start)){
        while(span!=spans.cend()&&span->start+span->length<=end)++span;
        const bool insideCode=span!=spans.cend()&&span->start<=end&&span->kind==SpanKind::Code;
        const auto line=QStringView(source).mid(start,end-start);
        result+=line;
        const bool hardBreak=end>start&&source[end-1]=='\\'&&!escaped(source,end-1);
        if(!insideCode&&!hardBreak&&!line.trimmed().isEmpty())result+="  ";
        result+='\n';
        start=end+1;
    }
    return result+source.mid(start);
}

// Use an explicit hierarchy: Qt's default H4-H6 share the body size.
QTextCharFormat headingCharacters(int level,const QFont &base){
    static constexpr qreal scales[]={2.0,1.75,1.5,1.3,1.2,1.1};
    QFont font=base;
    const qreal scale=scales[qBound(1,level,6)-1];
    if(base.pointSizeF()>0)font.setPointSizeF(base.pointSizeF()*scale);
    else font.setPixelSize(qRound(base.pixelSize()*scale));
    font.setWeight(QFont::Bold);
    QTextCharFormat characters;characters.setFont(font);
    return characters;
}

// Qt's Markdown importer drops the enclosing anchor on linked images.
void restoreImageLinks(QTextDocument *document,const QString &source){
    static const QRegularExpression linked(R"rx(\[!\[([^\]\n]*)\]\(\s*(<[^>\n]+>|[^\s\n]+?)(?:\s+"[^"\n]*")?\s*\)\]\(\s*(<[^>\n]+>|(?:[^\s()\n]|\([^()\n]*\))+)(?:\s+"[^"\n]*")?\s*\))rx");
    auto matches=linked.globalMatch(source);
    const auto spans=scanSpans(source);
    int after=0;
    while(matches.hasNext()){
        const auto match=matches.next();
        if(escaped(source,match.capturedStart()))continue;
        bool code=false;
        for(const auto &span:spans)if(span.kind==SpanKind::Code&&match.capturedStart()>=span.start&&match.capturedStart()<span.start+span.length){code=true;break;}
        if(code)continue;
        auto unwrap=[](QString value){return value.startsWith('<')?value.mid(1,value.size()-2):value;};
        for(auto block=document->begin();block.isValid();block=block.next()){
            bool found=false;
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();auto format=fragment.charFormat();
                if(fragment.position()<after||!format.isImageFormat())continue;
                if(QUrl(format.toImageFormat().name())!=QUrl(unwrap(match.captured(2)))||format.stringProperty(QTextFormat::ImageAltText)!=match.captured(1))continue;
                format.setAnchor(true);format.setAnchorHref(unwrap(match.captured(3)));
                QTextCursor cursor(document);cursor.setPosition(fragment.position());cursor.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);cursor.setCharFormat(format);
                after=fragment.position()+fragment.length();found=true;break;
            }
            if(found)break;
        }
    }
}

static QString normalizeFileLinkSeparators(QString source){
    static const QRegularExpression links(R"(\]\([ \t]*(?:<(file:[^<>\r\n]+)>|(file:[^<>\s)]+)))",
        QRegularExpression::CaseInsensitiveOption);
    auto matches=links.globalMatch(source);
    if(!matches.hasNext())return source;

    const auto spans=scanSpans(source);
    static const QRegularExpression drive(R"(^file:/*([A-Za-z]:/))",QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> destinations;
    while(matches.hasNext()){
        const auto match=matches.next();
        const int group=match.capturedStart(1)>=0?1:2;
        if(!match.capturedView(group).contains('\\')||escaped(source,match.capturedStart()))continue;
        if(std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
            return match.capturedStart()<span.start+span.length&&match.capturedEnd()>span.start;
        }))continue;
        destinations.append(match);
    }
    for(auto it=destinations.crbegin();it!=destinations.crend();++it){
        const int group=it->capturedStart(1)>=0?1:2;
        QString url=it->captured(group);
        url.replace('\\','/');
        url.replace(drive,QStringLiteral("file:///\\1"));
        source.replace(it->capturedStart(group),it->capturedLength(group),url);
    }
    return source;
}

int renderMarkdown(QTextDocument *document,const QString &source,QStringList &errors,bool live, const std::optional<QPalette> &theme){
    document->setIndentWidth(24);
    auto *layout=document->documentLayout();
    if(!live&&!dynamic_cast<FormulaImageHandler *>(layout->handlerForObject(QTextFormat::ImageObject)))
        new FormulaImageHandler(layout);
    QList<QPair<QString,QString>> math;
    // Keep Markdown parsing inside marked text (bold, links, math, etc.).
    // Unique delimiter placeholders survive Qt's parser without HTML injection.
    QString marked=source;
    QList<QPair<QString,QString>> markers;
    const auto ranges=highlightRanges(source);
    QString prefix="MDEDITMARK"+QUuid::createUuid().toString(QUuid::Id128);
    for(int i=ranges.size()-1;i>=0;--i){
        const QString begin=prefix+QString::number(i)+"BEGIN:";
        const QString end=":"+prefix+QString::number(i)+"END";
        marked.replace(ranges[i].second-2,2,end);
        marked.replace(ranges[i].first,2,begin);
        markers.append({begin,end});
    }
    document->setMarkdown(preserveLineBreaks(protectMath(markdownWithoutSetext(expandWikiLinks(normalizeFileLinkSeparators(marked))),math)));
    restoreImageLinks(document,source);
    for(const auto &marker:markers){
        auto begin=document->find(marker.first),end=document->find(marker.second);
        if(begin.isNull()||end.isNull())continue;
        const int start=begin.selectionStart();
        const int length=end.selectionStart()-begin.selectionEnd();
        end.removeSelectedText();begin.removeSelectedText();
        QTextCursor highlight(document);highlight.setPosition(start);
        highlight.setPosition(start+length,QTextCursor::KeepAnchor);
        QTextCharFormat format;format.setBackground(mdedit::themeColor(theme,QPalette::Light,highlightBackground));
        highlight.mergeCharFormat(format);
    }
    for(auto block=document->begin();block.isValid();block=block.next()){
        auto format=block.blockFormat();
        if(format.headingLevel()){
            // FontSizeAdjustment takes precedence over FontPointSize in Qt's
            // text layout. Remove the relative size imported by setMarkdown.
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();auto characters=fragment.charFormat();
                characters.merge(headingCharacters(format.headingLevel(),document->defaultFont()));
                characters.clearProperty(QTextFormat::FontSizeAdjustment);
                QTextCursor heading(document);heading.setPosition(fragment.position());
                heading.setPosition(fragment.position()+fragment.length(),QTextCursor::KeepAnchor);
                heading.setCharFormat(characters);
            }
        }
        if (theme) {
            QList<QPair<int,int>> links;
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (fragment.charFormat().isAnchor())
                    links.append({fragment.position(), fragment.length()});
            }
            QTextCharFormat link;
            link.setForeground(theme->color(QPalette::Link));
            for (const auto &[start, length] : links) {
                QTextCursor cursor(document);
                cursor.setPosition(start);
                cursor.setPosition(start + length, QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(link);
            }
        }
        if(isCodeBlock(format)){
            format.setBackground(mdedit::themeColor(theme,QPalette::Button,codeBlockBackground));
            QTextCursor(block).setBlockFormat(format);
        }else if(format.intProperty(QTextFormat::BlockQuoteLevel)>0){
            format.setBackground(mdedit::themeColor(theme,QPalette::AlternateBase,quoteBackground));
            QTextCursor(block).setBlockFormat(format);
        }
    }
    int rendered=0;
#if !MDEDIT_LITE
    for(const auto &entry:math){
        auto cursor=document->find(entry.first);if(cursor.isNull())continue;
        try {
            const bool block=entry.second.startsWith("$$");
            const int delimiter=block?2:1;
            tex::Formula parsed(entry.second.mid(delimiter,entry.second.size()-delimiter*2).toStdWString());
            const QFontMetricsF textMetrics(block?document->defaultFont():
                cursor.charFormat().font().resolve(document->defaultFont()));
            tex::TeXRenderBuilder builder;
            builder.setStyle(block?tex::TexStyle::display:tex::TexStyle::text)
                .setTextSize(float(textMetrics.height()))
                .setForeground(0xff26232d).setTrueValues(true);
            // Natural width for inline math; display alignment belongs to its paragraph.
            std::unique_ptr<tex::TeXRender> formula(builder.build(parsed));
            const int padding=block?4:1;
            const int width=qMax(1,formula->getWidth()+2*padding);
            const int height=qMax(1,formula->getHeight()+2*padding);
            if(width>10000||height>10000)throw std::runtime_error("Formula too large");
            // Measure from the TeX baseline so descenders can paint below the text baseline.
            const qreal baseline=formula->getBaseline();
            const qreal ascent=padding+(qIsFinite(baseline)?qBound(0.0,baseline,1.0):0.0)*(height-2*padding);
            const qreal descent=height-ascent;
            // TeX glyphs sit visually low beside the body font, especially CJK text.
            const qreal lift=block?0.0:textMetrics.height()*0.22;
            const qreal scale=block?1.0:qMin(1.0,qMin(
                (textMetrics.ascent()-lift)/ascent,(textMetrics.descent()+lift)/descent));
            const qreal imageWidth=width*scale;
            const qreal imageHeight=height*scale;
            // Keep scalable glyph and rule commands in the fragment itself,
            // so cached fragments preserve vectors when copied between documents.
            QPicture picture;
            {
                QPainter vectorPainter(&picture);
                vectorPainter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
                vectorPainter.scale(scale,scale);
                tex::Graphics2D_qt graphics(&vectorPainter);
                formula->draw(graphics,padding,padding);
            }
            QImage image(QSize(qCeil(imageWidth*2),qCeil(imageHeight*2)),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.scale(image.width()/imageWidth,image.height()/imageHeight);
            painter.scale(scale,scale);
            painter.setRenderHint(QPainter::Antialiasing);
            tex::Graphics2D_qt graphics(&painter);formula->draw(graphics,padding,padding);painter.end();
            const QUrl url("math:/"+QUuid::createUuid().toString(QUuid::WithoutBraces));document->addResource(QTextDocument::ImageResource,url,image);
            QTextImageFormat format;format.setName(url.toString());format.setWidth(imageWidth);format.setHeight(imageHeight);format.setVerticalAlignment(QTextCharFormat::AlignBaseline);
            format.setProperty(formulaMarkdown,entry.second);
            format.setProperty(formulaVector,QVariant::fromValue(picture));
            format.setProperty(formulaVectorSize,QSizeF(imageWidth,imageHeight));
            if(!block){
                const qreal imageAscent=ascent*scale+lift;
                format.setHeight(imageAscent);
                format.setProperty(formulaPaintAscent,imageAscent);
                format.setProperty(formulaPaintHeight,imageHeight);
            }
            cursor.insertImage(format);if(block){auto f=cursor.blockFormat();f.setAlignment(Qt::AlignHCenter);f.setTopMargin(14);f.setBottomMargin(14);cursor.setBlockFormat(f);}++rendered;
        } catch(const std::exception &e){cursor.insertText(entry.second);errors.append(QString::fromUtf8(e.what()));}
        catch(...){cursor.insertText(entry.second);errors.append("无法解析公式");}
    }
#endif
    return rendered;
}


#include "renderer.moc"
