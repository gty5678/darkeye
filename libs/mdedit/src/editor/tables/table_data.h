#pragma once
#include <QtCore>
namespace mdedit {
struct TableData {
    QList<QStringList> rows;
    QStringList separators;
    QString error;
};
TableData readMarkdownTable(const QString &source);
QString writeMarkdownTable(const TableData &data);
}
