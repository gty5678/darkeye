#pragma once
#include <QtGui>
#include <functional>

enum class SpanKind { Code, InlineMath, DisplayMath };
struct Span { int start, length; SpanKind kind; bool fenced=false; };
struct CodeBody {
    int start=0,end=0;
    QString language;
};

bool escaped(const QString &s,int pos);
QList<Span> scanSpans(const QString &s);
QList<QPair<int,int>> highlightRanges(const QString &source);
CodeBody codeBody(const QString &text,int fenceStart=-1);
void colorCode(const QString &text,QString language,const std::function<void(int,int,const QColor &)> &apply);
