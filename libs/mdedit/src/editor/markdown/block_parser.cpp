#include "block_parser.h"
namespace mdedit {
QList<BlockViewNode> MarkdownParser::parse(const QString &source) {
    struct Line { QString text; int begin,end; };
    QList<Line> lines;
    int pos=0;
    for(const auto &line:source.split('\n')) { lines.append({line,pos,pos+int(line.size())});pos+=line.size()+1; }
    static const QRegularExpression fence(R"(^ {0,3}(`{3,}|~{3,})(.*)$)");
    static const QRegularExpression heading(R"(^ {0,3}#{1,6}\s)");
    static const QRegularExpression list(R"(^\s*(?:[-+*]|\d+[.)])\s)");
    static const QRegularExpression rule(R"(^ {0,3}(?:(?:\*\s*){3,}|(?:-\s*){3,}|(?:_\s*){3,})$)");
    static const QRegularExpression image(R"rx(^\s*!\[([^\]]*)\]\(\s*(<[^>]*>|[^\s]+?)(?:\s+"([^"]*)")?\s*\)\s*$)rx");
    QList<BlockViewNode> result;
    for(int i=0;i<lines.size();) {
        if(lines[i].text.trimmed().isEmpty()){++i;continue;}
        const int first=i;
        BlockViewNode node;
        const auto fm=fence.match(lines[i].text);
        if(fm.hasMatch()) {
            node.kind=BlockKind::Code; const QString marker=fm.captured(1);
            ++i;
            while(i<lines.size()) {
                const auto end=fence.match(lines[i++].text);
                if(end.hasMatch()&&end.captured(1)[0]==marker[0]&&end.captured(1).size()>=marker.size()&&end.captured(2).trimmed().isEmpty())break;
            }
        } else {
            const auto im=image.match(lines[i].text);
            if(im.hasMatch()) {
                node.kind=BlockKind::Resource;QString uri=im.captured(2);
                if(uri.startsWith('<'))uri=uri.mid(1,uri.size()-2);
                QString alt=im.captured(1);
                static const QRegularExpression dimensions(R"(\|([1-9][0-9]*)$)");
                const auto size=dimensions.match(alt);
                bool valid=false;const int width=size.captured(1).toInt(&valid);
                if(valid)alt.truncate(size.capturedStart());
                node.resource=ResourceRef{uri,alt,im.captured(3),valid?width:0};++i;
            } else if(heading.match(lines[i].text).hasMatch()) {node.kind=BlockKind::Heading;++i;}
            else if(rule.match(lines[i].text).hasMatch()){node.kind=BlockKind::Rule;++i;}
            else {
                if(list.match(lines[i].text).hasMatch())node.kind=BlockKind::List;
                else if(lines[i].text.trimmed().startsWith('>'))node.kind=BlockKind::Quote;
                else if(i+1<lines.size()&&lines[i].text.contains('|')&&QRegularExpression(R"(^\s*\|?\s*:?-{3,}:?\s*\|)").match(lines[i+1].text).hasMatch())node.kind=BlockKind::Table;
                ++i;
                while(i<lines.size()&&!lines[i].text.trimmed().isEmpty()&&!heading.match(lines[i].text).hasMatch()&&!fence.match(lines[i].text).hasMatch()&&!image.match(lines[i].text).hasMatch()&&!rule.match(lines[i].text).hasMatch())++i;
            }
        }
        node.source={lines[first].begin,lines[i-1].end};result.append(node);
    }
    return result;
}
}
