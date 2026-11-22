#include "ui/components/WikiHighlighter.h"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>

namespace darkeye {
namespace {
QTextCharFormat colored(const char *color)
{
    QTextCharFormat format;
    format.setForeground(QColor(QString::fromLatin1(color)));
    return format;
}
}

WikiHighlighter::WikiHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    QTextCharFormat link = colored("#3498db");
    link.setFontUnderline(true);
    link.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression(QStringLiteral(R"(\[\[([^\]]+)\]\])")), link});

    QTextCharFormat h1 = colored("#e67e22");
    h1.setFontWeight(QFont::Bold);
    h1.setFontPointSize(24);
    m_rules.append({QRegularExpression(QStringLiteral(R"(^#\s.*$)")), h1});

    QTextCharFormat h2 = colored("#d35400");
    h2.setFontWeight(QFont::Bold);
    h2.setFontPointSize(18);
    m_rules.append({QRegularExpression(QStringLiteral(R"(^##\s.*$)")), h2});

    QTextCharFormat h3 = colored("#c0392b");
    h3.setFontWeight(QFont::Bold);
    h3.setFontPointSize(14);
    m_rules.append({QRegularExpression(QStringLiteral(R"(^###+\s.*$)")), h3});

    QTextCharFormat bold = colored("#e74c3c");
    bold.setFontWeight(QFont::Bold);
    m_rules.append({QRegularExpression(QStringLiteral(R"(\*\*.*?\*\*)")), bold});

    QTextCharFormat italic = colored("#9b59b6");
    italic.setFontItalic(true);
    m_rules.append({QRegularExpression(QStringLiteral(R"(\*.*?\*)")), italic});
}

void WikiHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

} // namespace darkeye
