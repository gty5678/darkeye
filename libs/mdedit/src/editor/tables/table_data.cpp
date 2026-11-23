#include "table_data.h"
namespace mdedit {
static QStringList markdownCells(QString line){
    line=line.trimmed();if(line.startsWith('|'))line.remove(0,1);
    if(line.endsWith('|')&&(line.size()<2||line[line.size()-2]!='\\'))line.chop(1);
    QStringList cells;QString cell;bool escaped=false;
    for(const QChar c:line){if(c=='|'&&!escaped){cells.append(cell.trimmed());cell.clear();}else cell+=c;
        if(c=='\\')escaped=!escaped;else escaped=false;}
    cells.append(cell.trimmed());return cells;
}
TableData readMarkdownTable(const QString &source){
    TableData data;const auto lines=source.trimmed().split('\n');
    if(lines.size()<2){data.error="未找到 Markdown 表格。";return data;}
    data.separators=markdownCells(lines[1]);
    static const QRegularExpression separator("^:?-{3,}:?$");
    for(const auto &cell:data.separators)if(!separator.match(cell).hasMatch()){data.error="表格分隔行格式不正确。";return data;}
    for(int i=0;i<lines.size();++i)if(i!=1)data.rows.append(markdownCells(lines[i]));
    return data;
}
QString writeMarkdownTable(const TableData &data){
    QStringList lines;
    for(int r=0;r<data.rows.size();++r){
        QStringList cells;
        for(QString cell:data.rows[r]){cell.replace('\n',"<br>");cell.replace('\r',"");
            QString escaped;int slashes=0;
            for(QChar c:cell){if(c=='|'&&slashes%2==0)escaped+='\\';escaped+=c;if(c=='\\')++slashes;else slashes=0;}
            cells.append(escaped);
        }
        lines.append("| "+cells.join(" | ")+" |");
        if(r==0){QStringList separators;for(int c=0;c<cells.size();++c)separators.append(data.separators.value(c,"---"));lines.append("| "+separators.join(" | ")+" |");}
    }
    return lines.join('\n');
}
}
