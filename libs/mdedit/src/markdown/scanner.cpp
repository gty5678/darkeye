#include "scanner.h"

bool escaped(const QString &s,int pos){int n=0;while(pos>0&&s[--pos]=='\\')++n;return n%2;}

static int quotePrefixLength(QStringView line,int depth){
    static const QRegularExpression prefix(R"( {0,3}>[ \t]?)");
    int offset=0;
    for(int i=0;i<depth;++i){
        const auto marker=prefix.matchView(line,offset,QRegularExpression::NormalMatch,
            QRegularExpression::AnchorAtOffsetMatchOption);
        if(!marker.hasMatch())return -1;
        offset=marker.capturedEnd();
    }
    return offset;
}

static int enclosingListIndent(const QString &source,int lineStart,int indent){
    static const QRegularExpression item(R"(^( *)(?:[-+*]|[0-9]{1,9}[.)])[ \t]+)");
    int end=lineStart-1;
    while(end>=0){
        const int start=end==0?0:source.lastIndexOf('\n',end-1)+1;
        const auto line=QStringView(source).mid(start,end-start);
        const auto marker=item.matchView(line);
        if(marker.hasMatch()&&marker.capturedEnd()<=indent)return marker.capturedEnd();
        int spaces=0;
        while(spaces<line.size()&&line[spaces]==' ')++spaces;
        if(!line.trimmed().isEmpty()&&spaces<indent)return 0;
        end=start-1;
    }
    return 0;
}

// One scanner defines the boundaries for both syntax highlighting and preview.
QList<Span> scanSpans(const QString &s){
    QList<Span> spans;
    for(int i=0;i<s.size();){
        if(escaped(s,i)){++i;continue;}
        const QChar ch=s[i];
        if(ch=='`'||ch=='~'){
            int count=1;while(i+count<s.size()&&s[i+count]==ch)++count;
            int lineStart=i==0?0:int(s.lastIndexOf('\n',i-1))+1;
            const auto prefix=QStringView(s).mid(lineStart,i-lineStart);
            const int quoteDepth=count>=3?prefix.count('>'):0;
            const int quoteLength=quotePrefixLength(prefix,quoteDepth);
            const int listIndent=count>=3&&!quoteDepth&&!prefix.isEmpty()&&prefix.count(' ')==prefix.size()?
                enclosingListIndent(s,lineStart,prefix.size()):0;
            if(count>=3&&quoteLength>=0&&prefix.size()-quoteLength-listIndent<=3&&
               prefix.mid(quoteLength).count(' ')==prefix.size()-quoteLength){
                const int openingEnd=s.indexOf('\n',i+count);
                const auto info=QStringView(s).mid(i+count,openingEnd<0?-1:openingEnd-i-count);
                if(ch!='`'||!info.contains('`')){
                    int end=int(s.size());int next=openingEnd;
                    while(next>=0&&next+1<s.size()){
                        int start=next+1;next=int(s.indexOf('\n',start));
                        int lineEnd=next<0?int(s.size()):next;
                        auto line=QStringView(s).mid(start,lineEnd-start);
                        if(quoteDepth){
                            const int length=quotePrefixLength(line,quoteDepth);
                            if(length<0){end=start-1;break;}
                            line=line.mid(length);
                        }
                        int indent=0;while(indent<line.size()&&line[indent]==' ')++indent;
                        if(indent<listIndent&&!line.trimmed().isEmpty()){end=start-1;break;}
                        int run=0;while(indent+run<line.size()&&line[indent+run]==ch)++run;
                        if(indent-listIndent<=3&&run>=count&&line.mid(indent+run).trimmed().isEmpty()){end=lineEnd;break;}
                    }
                    spans.append({i,end-i,SpanKind::Code,true});i=end;continue;
                }
            }
            if(ch=='`'){
                int j=i+count,end=-1;
                while(j<s.size()){
                    // Inline code may wrap within a paragraph, but cannot
                    // close in another block across a blank line.
                    if(s[j]=='\n'){
                        int next=j+1;
                        while(next<s.size()&&(s[next]==' '||s[next]=='\t'||s[next]=='\r'))++next;
                        if(next<s.size()&&s[next]=='\n')break;
                    }
                    if(s[j]!='`'){++j;continue;}int run=1;while(j+run<s.size()&&s[j+run]=='`')++run;
                    if(run==count){end=j+run;break;}j+=run;
                }
                if(end>=0){spans.append({i,end-i,SpanKind::Code});i=end;continue;}
            }
            i+=count;continue;
        }
#if !MDEDIT_LITE
        if(ch=='$'){
            int count=1;while(i+count<s.size()&&s[i+count]=='$')++count;
            if(count>2){i+=count;continue;}
            int j=i+count,end=-1;
            while(j<s.size()){
                if(count==1&&s[j]=='\n')break;
                if(s[j]!='$'||escaped(s,j)){++j;continue;}
                int run=1;while(j+run<s.size()&&s[j+run]=='$')++run;
                if(run==count){end=j+count;break;}j+=run;
            }
            if(end>=0&&!s.mid(i+count,end-i-2*count).trimmed().isEmpty()){
                spans.append({i,end-i,count==2?SpanKind::DisplayMath:SpanKind::InlineMath});i=end;continue;
            }
            i+=count;continue;
        }
#endif
        ++i;
    }
    return spans;
}

// Return complete, single-line ==highlight== spans, excluding code/math.
QList<QPair<int,int>> highlightRanges(const QString &source){
    const auto protectedSpans=scanSpans(source);
    const auto delimiter=[&](int at){
        if(at<0||at+1>=source.size()||source[at]!='='||source[at+1]!='='||escaped(source,at)||
           (at>0&&source[at-1]=='=')||(at+2<source.size()&&source[at+2]=='='))return false;
        for(const auto &span:protectedSpans)
            if(at<span.start+span.length&&at+2>span.start)return false;
        return true;
    };
    QList<QPair<int,int>> result;
    for(int start=0;start<source.size();){
        int end=source.indexOf('\n',start);if(end<0)end=source.size();
        const auto line=QStringView(source).mid(start,end-start);
        if(!line.startsWith(u"    ")&&!line.startsWith(u"\t")){
            int open=-1;
            for(int at=start;at+1<end;++at){
                if(!delimiter(at))continue;
                if(open<0)open=at;
                else{
                    if(!QStringView(source).mid(open+2,at-open-2).trimmed().isEmpty())
                        result.append({open,at+2});
                    open=-1;
                }
                ++at;
            }
        }
        start=end+1;
    }
    return result;
}

CodeBody codeBody(const QString &text,int fenceStart){
    if(fenceStart>=0&&text.mid(fenceStart,3)!="```"&&text.mid(fenceStart,3)!="~~~")fenceStart=-1;
    const auto spans=fenceStart>=0?QList<Span>{{fenceStart,int(text.size())-fenceStart,SpanKind::Code,true}}:scanSpans(text);
    for(const auto &span:spans){
        if(!span.fenced)continue;
        const int openingEnd=text.indexOf('\n',span.start);
        if(openingEnd<0)return {int(text.size()),int(text.size()),{}};
        int count=0;
        while(span.start+count<text.size()&&text[span.start+count]==text[span.start])++count;
        const QString language=text.mid(span.start+count,openingEnd-span.start-count).trimmed().section(' ',0,0).toLower();
        int end=span.start+span.length;
        const int closingStart=text.lastIndexOf('\n',end-1)+1;
        const auto closing=text.mid(closingStart,end-closingStart);
        const QRegularExpression closer("^[ >\\t]*"+QString(text[span.start])+"{"+QString::number(count)+",}[ \\t]*$");
        if(closingStart>openingEnd&&closer.match(closing).hasMatch())end=qMax(openingEnd+1,closingStart-1);
        return {openingEnd+1,end,language};
    }
    return {0,int(text.size()),{}};
}

// Small offline lexer shared by Live and full-source editing. Unknown
// languages stay plain; comments and strings take precedence over keywords.
void colorCode(const QString &text,QString language,
        const std::function<void(int,int,const QColor &)> &apply){
    if(language=="c++"||language=="c"||language=="cc"||language=="h"||language=="hpp")language="cpp";
    if(language=="py")language="python";
    if(language=="js"||language=="ts"||language=="typescript"||language=="jsx"||language=="tsx")language="javascript";
    if(language=="sh"||language=="shell"||language=="zsh")language="bash";
    if(language=="tex")language="latex";
    const bool python=language=="python",shell=language=="bash",latex=language=="latex";
    const bool cLike=language=="cpp"||language=="javascript",json=language=="json";
    if(!python&&!shell&&!latex&&!cLike&&!json)return;
    static const QSet<QString> words=[] {
        const auto list=QString("alignas alignof auto bool break case catch char class const constexpr continue decltype default delete do double else enum explicit export extern false float for friend if inline int long namespace new noexcept nullptr operator private protected public register return short signed sizeof static static_assert struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while and as assert async await def del elif except False finally from global import in is lambda None nonlocal not or pass raise True with yield abstract arguments debugger extends function implements instanceof interface let null package super typeof var undefined of then fi esac done until select local echo").split(' ',Qt::SkipEmptyParts);
        return QSet<QString>(list.cbegin(),list.cend());
    }();
    const QColor keyword("#8959a8"),string("#718c00"),comment("#8e908c"),number("#c76b29"),command("#4271ae");
    for(int i=0;i<text.size();){
        const int start=i;
        if((cLike&&text.mid(i,2)=="//")||((python||shell)&&text[i]=='#')||(latex&&text[i]=='%'&&!escaped(text,i))){
            i=text.indexOf('\n',i);if(i<0)i=text.size();apply(start,i-start,comment);
        }else if(cLike&&text.mid(i,2)=="/*"){
            i=text.indexOf("*/",i+2);i=i<0?int(text.size()):i+2;apply(start,i-start,comment);
        }else if(latex&&text[i]=='\\'){
            ++i;while(i<text.size()&&text[i].isLetter())++i;if(i==start+1&&i<text.size())++i;apply(start,i-start,command);
        }else if(!latex&&(text[i]=='\"'||text[i]=='\''||(language=="javascript"&&text[i]=='`'))){
            const QChar quote=text[i];
            const int count=python&&text.mid(i,3)==QString(3,quote)?3:1;
            i+=count;
            while(i<text.size()){
                if(text[i]=='\\'){i=qMin(i+2,int(text.size()));continue;}
                if(text.mid(i,count)==QString(count,quote)){i+=count;break;}
                if(count==1&&quote!='`'&&text[i]=='\n')break;
                ++i;
            }
            apply(start,i-start,string);
        }else if(text[i].isDigit()&&(i==0||(!text[i-1].isLetterOrNumber()&&text[i-1]!='_'))){
            ++i;while(i<text.size()&&(text[i].isLetterOrNumber()||text[i]=='.'||text[i]=='_'))++i;apply(start,i-start,number);
        }else if(text[i].isLetter()||text[i]=='_'){
            ++i;while(i<text.size()&&(text[i].isLetterOrNumber()||text[i]=='_'))++i;
            if(words.contains(text.mid(start,i-start)))apply(start,i-start,keyword);
        }else ++i;
    }
}

