#pragma once
#include "../markdown/scanner.h"
#include "theme_palette.h"

class Highlighter : public QSyntaxHighlighter {
    bool spansDirty=true;
    QList<Span> spans;
    struct Token { int start,length; QColor color; };
    QList<Token> tokens;
    QHash<int,int> codeStates;
    std::optional<QPalette> theme;
public:
    explicit Highlighter(QTextDocument *d);
    void setThemePalette(const QPalette &palette);
    void highlightBlock(const QString &text) override;
};

