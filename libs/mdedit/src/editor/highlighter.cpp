#include "highlighter.h"
#include "../markdown/renderer.h"

Highlighter::Highlighter(QTextDocument *d):QSyntaxHighlighter(static_cast<QObject *>(d)){
        connect(d,&QTextDocument::contentsChange,this,[this]{spansDirty=true;});
        setDocument(d);
    }

void Highlighter::setThemePalette(const QPalette &palette)
{
    theme = palette;
    rehighlight();
}

void Highlighter::highlightBlock(const QString &text) {
        if(spansDirty){
            const QString source=document()->toPlainText();
            spans=scanSpans(source);tokens.clear();codeStates.clear();spansDirty=false;
            for(const auto &span:spans)if(span.fenced){
                const QString raw=source.mid(span.start,span.length);
                const auto body=codeBody(raw);
                codeStates.insert(span.start,int(qHash(raw)&0x7fffffff));
                colorCode(raw.mid(body.start,body.end-body.start),body.language,[&](int start,int length,const QColor &color){
                    tokens.append({span.start+body.start+start,length,color});
                });
            }
        }
        static const QList<QPair<QRegularExpression,QString>> rules={{QRegularExpression("^#{1,6} .*$"),"#623e9c"},{QRegularExpression("\\*\\*.+?\\*\\*"),"#825018"}};
        for(const auto &r:rules){auto matches=r.first.globalMatch(text);while(matches.hasNext()){auto m=matches.next();QTextCharFormat f;f.setForeground(mdedit::syntaxColor(theme,QColor(r.second)));setFormat(m.capturedStart(),m.capturedLength(),f);}}
        for(const auto &style:linkEditingStyles(text,theme)){
            for(int at=style.start;at<style.start+style.length;++at){
                auto characters=format(at);characters.merge(style.format);setFormat(at,1,characters);
            }
        }
        for(const auto &range:highlightRanges(text)){
            const int position=currentBlock().position()+range.first;
            const bool protectedText=std::any_of(spans.cbegin(),spans.cend(),[&](const Span &span){
                return position>=span.start&&position<span.start+span.length;
            });
            if(!protectedText)for(int at=range.first;at<range.second;++at){
                auto characters=format(at);characters.setBackground(mdedit::themeColor(theme,QPalette::Light,highlightBackground));setFormat(at,1,characters);
            }
        }
        const int start=currentBlock().position(),end=start+int(text.size());int state=0;
        auto first=std::lower_bound(spans.cbegin(),spans.cend(),start,
            [](const Span &span,int position){return span.start+span.length<=position;});
        for(auto it=first;it!=spans.cend()&&it->start<=end;++it){
            const auto &span=*it;
            int from=qMax(start,span.start),to=qMin(end,span.start+span.length);
            if(to>from){QTextCharFormat f;f.setForeground(mdedit::syntaxColor(theme,QColor(span.kind==SpanKind::Code?"#276746":"#14616b")));if(span.kind!=SpanKind::Code)f.setBackground(mdedit::themeColor(theme,QPalette::AlternateBase,QColor("#e7f3f5")));setFormat(from-start,to-from,f);}
            if(span.start<=end&&span.start+span.length>end)state=span.fenced?codeStates.value(span.start):span.kind==SpanKind::Code?1:2;
        }
        for(const auto &token:tokens){
            if(token.start>end)break;
            const int from=qMax(start,token.start),to=qMin(end,token.start+token.length);
            if(to>from){QTextCharFormat format;format.setForeground(mdedit::syntaxColor(theme,token.color));setFormat(from-start,to-from,format);}
        }
        // Propagate edits to opening/closing delimiters through following blocks.
        setCurrentBlockState(state);
    }
